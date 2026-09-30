#include "Slic3r/App/Plater/SlaSupportPointsGizmo.hpp"
#include "Slic3r/App/Plater/SlaSupportPointsEditing.hpp"
#include "Slic3r/App/Plater/SlaSupportToolShortcuts.hpp"

#include "Slic3r/App/Plater/SlaSupportPointsDialog.hpp"
#include "Slic3r/App/Plater/PlaterScenePresenter.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/IDialogManager.hpp"
#include "Slic3r/App/DisplayStrings.hpp"
#include "Slic3r/App/Scene/Scene.hpp"
#include "Slic3r/App/Scene/NodeBuilder.hpp"
#include "Slic3r/App/Scene/GeometryDataFactory.hpp"
#include "Slic3r/App/Scene/Ray.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Render/GeometryBuilder.hpp"
#include "Slic3r/App/Plater/PlaterSceneLayer.hpp"
#include "Slic3r/App/Plater/SlaSupportPreviewService.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/StatusCache.hpp"
#include "Slic3r/Biz/IUndoProvider.hpp"
#include "Slic3r/Biz/Utils/MeshRaycaster.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "jthread/JThread.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/Domain/ConfigContainer.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Math.hpp"
#include "Slic3r/App/Platform/KeyboardEvent.hpp"
#include "Slic3r/App/Platform/KeyModifers.hpp"
#include "Slic3r/App/Platform/KeyCode.hpp"

#include <Eigen/Geometry>
#include <fmt/format.h>
#include <imgui/imgui.h>
#include <magic_enum/magic_enum_flags.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>

using namespace Slic3r;
using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;
using namespace Slic3r::Biz::Slicing;
using namespace Slic3r::Biz::Utils;
using namespace Slic3r::Biz::Algorithms;
using namespace magic_enum::bitwise_operators;

using Slic3r::Domain::SlicingId;
using Slic3r::Domain::ObjectID;
using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec3f;
using Slic3r::Domain::ColorRGBA;
using Slic3r::Domain::SLA::SupportPoint;
using Slic3r::Domain::SLA::SupportPointType;
using Slic3r::Domain::SLA::SupportPoints;
using Slic3r::Domain::SLA::PointsStatus;
using Slic3r::sla::generate_support_points_for_tool;
using Slic3r::sla::support_tool_elevation;
using Slic3r::sla::SupportToolStop;

namespace Slic3r::App::Plater {

SlaSupportPointsGizmo::SlaSupportPointsGizmo(
    PlaterScenePresenter& scene_presenter,
    Biz::ProjectInteractor& project_interactor,
    Render::Device& device,
    SlaSupportPreviewService& support_preview_service
) :
    m_scene_presenter(scene_presenter),
    m_project_interactor(project_interactor),
    m_device(device),
    m_support_preview_service(support_preview_service)
{
    m_dialog.reset(std::make_unique<SlaSupportPointsDialog>());
    m_dialog->set_title(_u8L("SLA Support Points"));
    m_dialog->set_shortcut("P");

    m_dialog->callbacks().generate = [this]() { this->start_generation(); };
    m_dialog->callbacks().apply = [this]() { this->apply_generated_points(); };
    m_dialog->callbacks().discard = [this]() { this->discard_generated_points(); };
    m_dialog->callbacks().auto_support_all = [this]() { this->auto_support(); };
    m_dialog->callbacks().density_changed = [this](double value)
    {
        if (m_syncing_dialog) {
            return;
        }
        const int density = static_cast<int>(value);
        if (m_selected_object_id.valid()) {
            Domain::Project& project = m_project_interactor.selected_project();
            Domain::ModelObject* model_object = project.find_object_by_id(m_selected_object_id.id);
            if (model_object) {
                m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::SlaSupportPointsEdit);
                auto result = model_object->object_settings_sla.find("support_points_density_relative");
                if (result.item) {
                    result.item->set<int>(density);
                }
            }
        }
    };
    m_dialog->callbacks().head_diameter_changed = [this](double value)
    {
        if (m_syncing_dialog) {
            return;
        }
        if (m_edit_state.has_value()) {
            // The tip diameter is the head diameter of the tool (M2.24), one of the per-point
            // geometry fields, so it goes on the points that are selected like the others.
            m_edit_state->editing.support_geometry.tip_diameter_mm = value;
            this->apply_support_geometry_to_selected(SupportGeometryField::TipDiameter);
        }
    };
    m_dialog->callbacks().pillar_diameter_changed = [this](double value)
    {
        if (m_syncing_dialog) {
            return;
        }
        if (m_edit_state.has_value()) {
            m_edit_state->editing.pillar_diameter_mm = value;
            m_edit_state->editing.pillar_diameter_use_global = false;
            m_dialog->set_pillar_diameter_use_global(false);
            this->apply_pillar_diameter_to_selected();
        }
    };
    m_dialog->callbacks().base_diameter_changed = [this](double value)
    {
        if (m_syncing_dialog) {
            return;
        }
        if (m_edit_state.has_value()) {
            m_edit_state->editing.base_diameter_mm = value;
            m_edit_state->editing.base_diameter_use_global = false;
            m_dialog->set_base_diameter_use_global(false);
            this->apply_base_diameter_to_selected();
        }
    };
    m_dialog->callbacks().base_height_changed = [this](double value)
    {
        if (m_syncing_dialog) {
            return;
        }
        if (m_edit_state.has_value()) {
            m_edit_state->editing.base_height_mm = value;
            m_edit_state->editing.base_height_use_global = false;
            m_dialog->set_base_height_use_global(false);
            this->apply_base_height_to_selected();
        }
    };
    m_dialog->callbacks().head_diameter_use_global_changed = [this](bool value)
    {
        if (m_syncing_dialog) {
            return;
        }
        if (m_edit_state.has_value()) {
            m_edit_state->editing.head_diameter_use_global = value;
            if (value) {
                this->apply_support_geometry_to_selected(SupportGeometryField::TipDiameter);
            }
        }
    };
    m_dialog->callbacks().pillar_diameter_use_global_changed = [this](bool value)
    {
        if (m_syncing_dialog) {
            return;
        }
        if (m_edit_state.has_value()) {
            m_edit_state->editing.pillar_diameter_use_global = value;
            if (value) {
                this->apply_pillar_diameter_to_selected();
            }
        }
    };
    m_dialog->callbacks().base_diameter_use_global_changed = [this](bool value)
    {
        if (m_syncing_dialog) {
            return;
        }
        if (m_edit_state.has_value()) {
            m_edit_state->editing.base_diameter_use_global = value;
            if (value) {
                this->apply_base_diameter_to_selected();
            }
        }
    };
    m_dialog->callbacks().base_height_use_global_changed = [this](bool value)
    {
        if (m_syncing_dialog) {
            return;
        }
        if (m_edit_state.has_value()) {
            m_edit_state->editing.base_height_use_global = value;
            if (value) {
                this->apply_base_height_to_selected();
            }
        }
    };
    m_dialog->callbacks().preset_mini = [this]() { this->apply_preset_mini(); };
    // The per-point support geometry (M2.16c). Every one of them is a value of its own, so unlike
    // the sizes above there is no "use global" checkbox: the value is written on the points that
    // are selected, and the one the fields show is the one those points carry. One field at a time,
    // so setting the tip shape keeps the knot, the cross-section and the taper a point has.
    m_dialog->callbacks().tip_shape_changed = [this](Domain::SLA::SupportPoint::TipShape shape)
    {
        if (m_syncing_dialog) {
            return;
        }
        if (m_edit_state.has_value()) {
            m_edit_state->editing.support_geometry.tip_shape = shape;
            this->apply_support_geometry_to_selected(SupportGeometryField::TipShape);
        }
    };
    m_dialog->callbacks().tip_length_changed = [this](double value)
    {
        if (m_syncing_dialog) {
            return;
        }
        if (m_edit_state.has_value()) {
            m_edit_state->editing.support_geometry.tip_length_mm = value;
            this->apply_support_geometry_to_selected(SupportGeometryField::TipLength);
        }
    };
    m_dialog->callbacks().knot_diameter_changed = [this](double value)
    {
        if (m_syncing_dialog) {
            return;
        }
        if (m_edit_state.has_value()) {
            m_edit_state->editing.support_geometry.knot_diameter_mm = value;
            this->apply_support_geometry_to_selected(SupportGeometryField::KnotDiameter);
        }
    };
    m_dialog->callbacks().stem_sides_changed = [this](double value)
    {
        if (m_syncing_dialog) {
            return;
        }
        if (m_edit_state.has_value()) {
            m_edit_state->editing.support_geometry.stem_sides = static_cast<int>(value);
            this->apply_support_geometry_to_selected(SupportGeometryField::StemSides);
        }
    };
    m_dialog->callbacks().stem_taper_changed = [this](double value)
    {
        if (m_syncing_dialog) {
            return;
        }
        if (m_edit_state.has_value()) {
            m_edit_state->editing.support_geometry.stem_taper = value;
            this->apply_support_geometry_to_selected(SupportGeometryField::StemTaper);
        }
    };
    // The per-point "may this support end on the model" switch (M2.26). Like the geometry above it
    // is a value of its own, written on the points that are selected.
    m_dialog->callbacks().on_model_changed = [this](SupportOnModel on_model)
    {
        if (m_syncing_dialog) {
            return;
        }
        if (m_edit_state.has_value()) {
            this->apply_support_on_model_to_selected(on_model);
        }
    };
    // The shape of the foot of the selected points (M2.23b), a value of its own like the rest of the
    // per-point geometry: the foot of a point is a cone, a cylinder or a flat disc, and the tree is
    // built again for the ones that changed.
    m_dialog->callbacks().base_shape_changed = [this](Domain::SLA::SupportPoint::BaseShape shape)
    {
        if (m_syncing_dialog) {
            return;
        }
        if (m_edit_state.has_value()) {
            m_edit_state->editing.support_geometry.base_shape = shape;
            this->apply_support_geometry_to_selected(SupportGeometryField::BaseShape);
        }
    };
    m_dialog->callbacks().preset_light = [this]() { this->apply_preset_light(); };
    m_dialog->callbacks().preset_medium = [this]() { this->apply_preset_medium(); };
    m_dialog->callbacks().preset_heavy = [this]() { this->apply_preset_heavy(); };
    m_dialog->callbacks().clipping_plane_changed = [this](double value)
    {
        if (m_syncing_dialog) {
            return;
        }
        if (!m_selected_object_id.valid()) {
            return;
        }
        m_clipping_plane_presenter.set_position_by_ratio(value, true);
        update_clipping_plane();
    };
    m_dialog->callbacks().lock_island_supports_changed = [this](bool value)
    {
        if (m_syncing_dialog) {
            return;
        }
        if (m_edit_state.has_value()) {
            m_edit_state->editing.lock_island_supports = value;
        }
    };
    m_dialog->callbacks().clipping_plane_reset = [this]()
    {
        this->reset_clipping_plane();
    };

    m_dialog->set_generate_enabled(false);
    m_dialog->set_apply_enabled(false);
}

void SlaSupportPointsGizmo::provide_clipper(Scene::Clipper& clipper)
{
    m_clipping_plane_presenter = Scene::ClipperPresenter(&clipper, &m_device, &m_scene_presenter);
}

SlaSupportPointsGizmo::~SlaSupportPointsGizmo()
{
    cancel_worker_job();
}

Scene::ToolType SlaSupportPointsGizmo::type() const
{
    return Scene::ToolType::SlaSupportPoints;
}

bool SlaSupportPointsGizmo::supports_printer(Domain::PrinterTechnology pt) const
{
    return pt == Domain::PrinterTechnology::SLA;
}

bool SlaSupportPointsGizmo::enabled() const
{
    const Biz::Scene::ObjectSelection& selection =
        m_project_interactor.scene_interactor().object_selection();
    return selection.state() == Biz::Scene::SelectionState::WholeInstance;
}

void SlaSupportPointsGizmo::provide_gizmo_controller(Scene::IGizmoController& controller)
{
    m_gizmo_controller = &controller;
}

void SlaSupportPointsGizmo::on_activated()
{
    m_gizmo_active = true;
    m_project_interactor.scene_interactor().add_listener<Biz::Scene::ISceneSelectionChangedListener>(this);
    m_project_interactor.sla_object_cache().add_listener<Biz::ISLAObjectCacheChangedListener>(this);

    // The tool is where the support settings are changed, so it opens with them open (M2.17d4).
    m_dialog->set_settings_expanded(true);

    const Biz::Scene::ObjectSelection& selection =
        m_project_interactor.scene_interactor().object_selection();
    this->on_scene_selection_changed(m_project_interactor.selected_project_id(), selection);
}

void SlaSupportPointsGizmo::on_deactivated()
{
    m_gizmo_active = false;
    cancel_worker_job();
    m_project_interactor.scene_interactor().remove_listener<Biz::Scene::ISceneSelectionChangedListener>(this);
    m_project_interactor.sla_object_cache().remove_listener<Biz::ISLAObjectCacheChangedListener>(this);

    m_has_generated_points = false;
    m_generated_support_points.reset();

    // Cancel auto-support all queue
    if (!m_auto_support_queue.empty()) {
        m_auto_support_queue.clear();
        m_auto_support_keep_existing.reset();
    }

    if (m_edit_state.has_value()) {
        end_editing();
    }

    m_paintable_volumes.clear();

    // Deactivate clipping plane presenter
    m_clipping_plane_presenter.deactivate();

    // Clear point visuals
    clear_point_visuals();
    m_hovered_point_idx.reset();

    DialogSyncGuard guard(*this);
    m_dialog->set_generate_enabled(false);
    m_dialog->set_apply_enabled(false);
    m_dialog->set_auto_support_all_enabled(false);
    m_dialog->set_point_count(0);
}

void SlaSupportPointsGizmo::collect_paintable_volumes(const Domain::SelectionId project_id, const Domain::ElementRef& element)
{
    m_paintable_volumes.clear();

    const Domain::Project& project = m_project_interactor.project(project_id);
    const Domain::ModelObject* model_object = project.find_object_by_id(element.object_id);
    const Domain::ModelInstance* model_instance = project.find_instance_by_id(element.object_id, element.instance_id);

    if (!model_object || !model_instance) {
        return;
    }

    using MeshManager = PlaterScenePresenter::MeshManager;
    const MeshManager& mesh_manager = m_scene_presenter.model_triangle_mesh_manager(project_id);

    const double elevation = support_elevation();
    const Domain::Transform3d lift = Domain::translation_transform(Domain::Vec3d(0., 0., elevation));

    for (Domain::ModelVolume* model_volume : model_object->volumes) {
        if (!model_volume->is_model_part()) {
            continue;
            }

        const Scene::AuxiliaryElementId volume_id{
            Scene::AuxiliaryElementId::Type::Volume,
            model_volume->id().id
        };
        const Scene::TriangleMesh* scene_mesh = mesh_manager.get(volume_id);
        if (!scene_mesh) {
            continue;
        }

        const Domain::Transform3d instance_trafo = model_instance->get_matrix();
        const Domain::Transform3d lifted_instance_trafo = lift * instance_trafo;

        m_paintable_volumes.push_back({
            *model_object,
            *model_instance,
            *model_volume,
            *scene_mesh,
            scene_mesh->aabb_mesh(),
            lifted_instance_trafo * model_volume->get_matrix(),
            model_instance->get_matrix_no_offset() * model_volume->get_matrix_no_offset()
        });
    }
}

void SlaSupportPointsGizmo::on_scene_selection_changed(
    Domain::SelectionId project_id,
    const Biz::Scene::ObjectSelection& selection
)
{
    DialogSyncGuard guard(*this);

    cancel_worker_job();
    m_has_generated_points = false;
    m_generated_support_points.reset();

    // Cancel auto-support all queue on selection change
    if (!m_auto_support_queue.empty()) {
        m_auto_support_queue.clear();
        m_auto_support_keep_existing.reset();
    }

    if (m_edit_state.has_value()) {
        discard_edited_points();
    }
    m_hovered_point_idx.reset();

    if (!enabled() || selection.elements.empty()) {
        m_dialog->set_generate_enabled(false);
        m_dialog->set_apply_enabled(false);
        m_dialog->set_auto_support_all_enabled(false);
        m_dialog->set_point_count(0);
        return;
    }

    const Domain::ElementRef& element = selection.elements.front();
    if (element.volume_id != 0) {
        m_dialog->set_generate_enabled(false);
        m_dialog->set_apply_enabled(false);
        m_dialog->set_auto_support_all_enabled(false);
        m_dialog->set_point_count(0);
        return;
    }

    const Domain::Project& project = m_project_interactor.project(project_id);
    const Domain::ModelObject* model_object = project.find_object_by_id(element.object_id);
    if (!model_object) {
        m_dialog->set_generate_enabled(false);
        m_dialog->set_apply_enabled(false);
        m_dialog->set_auto_support_all_enabled(false);
        m_dialog->set_point_count(0);
        return;
    }

    m_selected_object_id = model_object->id();
    m_selected_instance_id = element.instance_id;

    const Domain::ModelInstance* instance = project.find_instance_by_id(element.object_id, element.instance_id);
    if (!instance) {
        m_dialog->set_generate_enabled(false);
        m_dialog->set_apply_enabled(false);
        m_dialog->set_auto_support_all_enabled(false);
        m_dialog->set_point_count(0);
        return;
    }

    const Domain::BedRef bed_ref = instance->get_last_bed();
    if (project.find_bed_instance_by_id(bed_ref.instance_id) == nullptr) {
        m_dialog->set_generate_enabled(false);
        m_dialog->set_apply_enabled(false);
        m_dialog->set_auto_support_all_enabled(false);
        m_dialog->set_point_count(0);
        return;
    }

    const SlicingId slicing_id{project_id, bed_ref.instance_id};

    int density = 100;
    auto result = model_object->object_settings_sla.find("support_points_density_relative");
    if (result.item) {
        density = result.item->get<int>();
    }
    m_dialog->set_density(density);

    size_t existing_count = model_object->sla_support_points.size();
    m_dialog->set_point_count(existing_count);

    double head_diameter = 0.4;
    auto head_result = model_object->object_settings_sla.find("support_head_front_diameter");
    if (head_result.item) {
        head_diameter = head_result.item->get<double>();
    }
    m_dialog->set_head_diameter(head_diameter);

    // Auto support all is available when we have a valid selection
    m_dialog->set_auto_support_all_enabled(true);

    // Collect paintable volumes for raycasting
    this->collect_paintable_volumes(project_id, element);

    // Initialize point visuals scene nodes first (clipping plane presenter needs m_main_node)
    Scene::Scene& scene = m_scene_presenter.scene();
    Scene::NodeBuilder main_builder{scene};
    main_builder.set_debug_name("SlaSupportPointsGizmo - Main");
    std::unique_ptr<Scene::Node> main_node = main_builder.build();
    m_main_node = main_node.get();
    scene.add_child(main_node.release(), &scene.root());

    Scene::NodeBuilder points_builder{scene};
    points_builder.set_debug_name("SlaSupportPointsGizmo - Points");
    std::unique_ptr<Scene::Node> points_node = points_builder.build();
    m_points_node = points_node.get();
    scene.add_child(points_node.release(), m_main_node);

    // Initialize clipping plane presenter
    // Yes: activate() hides every scene node outside the presenter's node, and this tool does not
    // draw the model itself, so the presenter must draw the selected object.
    m_clipping_plane_presenter.activate(
        model_object,
        instance,
        m_main_node,
        support_elevation(),
        Scene::BuildMeshesNodes::Yes
    );
    m_clipping_plane_presenter.set_behavior(true, true, 0.);
    m_clipping_plane_presenter.set_position_by_ratio(m_clipping_plane_presenter.clipper().get_position(), true);
    m_dialog->set_clipping_plane_position(m_clipping_plane_presenter.clipper().get_position());

    m_dialog->set_generate_enabled(true);
    m_dialog->set_auto_support_all_enabled(true);
    m_dialog->set_apply_enabled(false);
}

void SlaSupportPointsGizmo::on_sla_object_cache_changed(const Domain::SlicingId& id, Domain::ObjectID object_id)
{
    // No longer rebuild support geometry from SLA object cache.
    // Support geometry is now built on-demand via the worker using the new engine API.
    // Keep listener for potential future use.
    (void)id;
    (void)object_id;
}

void SlaSupportPointsGizmo::start_generation()
{
    if (m_points_job_running || !m_selected_object_id.valid()) {
        return;
    }

    Domain::Project& project = m_project_interactor.selected_project();
    Domain::ModelObject* model_object = project.find_object_by_id(m_selected_object_id.id);
    if (!model_object) {
        return;
    }

    const Domain::ModelInstance* instance = project.find_instance_by_id(m_selected_object_id.id, m_selected_instance_id);
    if (!instance || !instance->is_printable()) {
        AppServices::instance().dialog_manager().show_warning_dialog(
            _u8L("Automatic generation requires printable object."),
            _u8L("Warning")
        );
        return;
    }

    const Domain::BedRef bed_ref = instance->get_last_bed();
    if (project.find_bed_instance_by_id(bed_ref.instance_id) == nullptr) {
        AppServices::instance().dialog_manager().show_warning_dialog(
            _u8L("Automatic generation requires the object to be placed on the build plate."),
            _u8L("Warning")
        );
        return;
    }

    const std::optional<ObjectSlaConfig> config_opt = build_object_sla_config(model_object, instance);
    if (!config_opt.has_value()) {
        AppServices::instance().dialog_manager().show_warning_dialog(
            _u8L("Automatic generation requires a full SLA print configuration for the object."),
            _u8L("Warning")
        );
        return;
    }

    const SlicingId slicing_id{m_project_interactor.selected_project_id(), bed_ref.instance_id};

    {
        DialogSyncGuard guard(*this);
        m_dialog->set_generate_enabled(false);
        m_dialog->set_apply_enabled(false);
    }

    // Run the points generation on the worker thread. Nothing is sliced for the support tool.
    WorkerJobData job_data;
    job_data.cloned_object = std::unique_ptr<Domain::ModelObject>(Domain::ModelObject::new_clone(*model_object));
    job_data.instance_matrix = instance->get_matrix();
    job_data.config = *config_opt;
    job_data.object_id = m_selected_object_id;
    job_data.instance_id = m_selected_instance_id;
    job_data.slicing_id = slicing_id;
    job_data.job_type = WorkerJobType::Points;
    job_data.for_auto_support_all = false;
    job_data.job_counter = ++m_job_counter;

    m_points_job_running = true;
    start_worker_job(std::move(job_data));
}

void SlaSupportPointsGizmo::on_generation_completed(std::optional<Domain::SLA::SupportPoints> support_points)
{
    DialogSyncGuard guard(*this);

    m_points_job_running = false;

    if (support_points.has_value() && !support_points->empty()) {
        // The generator only fills the position and the head diameter, so a generated point takes
        // the tip shape, tip length, knot, stem cross-section and stem taper of the settings as well
        // (M2.16c, M2.24). The head radius it fills is the one of the tree type it generated for.
        const SlaSupportGeometry geometry = support_geometry_defaults(
            m_project_interactor.selected_project().find_object_by_id(m_selected_object_id.id)
        );
        for (Domain::SLA::SupportPoint& point : *support_points) {
            apply_support_geometry(point, geometry);
        }
        m_generated_support_points = *support_points;
        m_has_generated_points = true;

        size_t count = m_generated_support_points->size();

        m_dialog->set_point_count(count);
        m_dialog->set_apply_enabled(true);
        m_dialog->set_generate_enabled(true);
        m_dialog->set_auto_support_all_enabled(true);
    } else {
        m_has_generated_points = false;
        m_generated_support_points.reset();
        m_dialog->set_point_count(0);
        m_dialog->set_apply_enabled(false);
        m_dialog->set_generate_enabled(true);
        m_dialog->set_auto_support_all_enabled(true);

        AppServices::instance().dialog_manager().show_warning_dialog(
            _u8L("Failed to generate support points: no support points could be generated."),
            _u8L("Warning")
        );
    }
}

void SlaSupportPointsGizmo::apply_generated_points()
{
    if (!m_has_generated_points || !m_selected_object_id.valid() || !m_generated_support_points.has_value()) {
        return;
    }

    Domain::Project& project = m_project_interactor.selected_project();
    Domain::ModelObject* model_object = project.find_object_by_id(m_selected_object_id.id);
    if (!model_object) {
        return;
    }

    Domain::SLA::SupportPoints domain_points = std::move(*m_generated_support_points);

    m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::SlaSupportPointsApply);

    const Domain::ElementRef object_ref{m_selected_object_id.id, m_selected_instance_id};
    m_project_interactor.scene_interactor().modify_sla_support_points(object_ref, [&](Domain::ModelObject& mo) {
        mo.sla_support_points = std::move(domain_points);
        mo.sla_points_status = PointsStatus::AutoGenerated;
    });

    DialogSyncGuard guard(*this);
    m_has_generated_points = false;
    m_dialog->set_apply_enabled(false);
    m_dialog->set_point_count(model_object->sla_support_points.size());
}

void SlaSupportPointsGizmo::discard_generated_points()
{
    DialogSyncGuard guard(*this);

    m_has_generated_points = false;
    m_generated_support_points.reset();
    m_dialog->set_apply_enabled(false);
    m_dialog->set_generate_enabled(true);
    m_dialog->set_auto_support_all_enabled(true);

    if (m_gizmo_controller) {
        m_gizmo_controller->deactivate_current_tool();
    }
}

bool SlaSupportPointsGizmo::auto_support(const std::vector<ObjectID>& object_ids)
{
    if (!m_gizmo_active || m_points_job_running || !m_auto_support_queue.empty()) {
        return false;
    }

    Domain::Project& project = m_project_interactor.selected_project();

    // A model can only be supported when a printable instance of it sits on a build plate.
    const auto is_supportable = [&project](const Domain::ModelObject* model_object) {
        if (!model_object) {
            return false;
        }
        return std::ranges::any_of(model_object->instances, [&project](const Domain::ModelInstance* instance) {
            if (!instance || !instance->is_printable()) {
                return false;
            }
            return project.find_bed_instance_by_id(instance->get_last_bed().instance_id) != nullptr;
        });
    };

    // Without a list the action covers every printable model of the project, the tool's own button.
    if (object_ids.empty()) {
        for (Domain::ModelObject* model_object : project.model().objects) {
            if (is_supportable(model_object)) {
                m_auto_support_queue.push_back(model_object->id());
            }
        }
    } else {
        for (const ObjectID& obj_id : object_ids) {
            const Domain::ModelObject* model_object = project.find_object_by_id(obj_id.id);
            if (is_supportable(model_object)) {
                m_auto_support_queue.push_back(model_object->id());
            }
        }
    }

    if (m_auto_support_queue.empty()) {
        return false;
    }

    // Check if any queued object already has support points
    const bool any_has_points = std::ranges::any_of(m_auto_support_queue, [&project](const ObjectID& obj_id) {
        const Domain::ModelObject* model_object = project.find_object_by_id(obj_id.id);
        return model_object && !model_object->sla_support_points.empty();
    });

    if (any_has_points) {
        // Ask user once: keep existing points or replace them
        // Using show_yesno_dialog since show_yesnocancel_dialog has a different API
        AppServices::instance().dialog_manager().show_yesno_dialog(
            _u8L("Auto support"),
            _u8L("Some models already have supports. Keep them and add around them? (No = replace)"),
            [this](bool answer) {
                m_auto_support_keep_existing = answer;
                this->process_auto_support_queue();
            }
        );
    } else {
        m_auto_support_keep_existing = true; // Keep (no existing points to worry about)
        process_auto_support_queue();
    }

    return true;
}

void SlaSupportPointsGizmo::process_auto_support_queue()
{
    if (m_auto_support_queue.empty()) {
        // Queue finished, re-enable buttons
        DialogSyncGuard guard(*this);
        m_dialog->set_generate_enabled(true);
        m_dialog->set_auto_support_all_enabled(true);
        m_auto_support_keep_existing.reset();

        if (m_selected_object_id.valid()) {
            const Domain::Project& project = m_project_interactor.selected_project();
            const Domain::ModelObject* model_object = project.find_object_by_id(m_selected_object_id.id);
            if (model_object) {
                m_dialog->set_point_count(model_object->sla_support_points.size());
            }
        }
        return;
    }

    Domain::ObjectID obj_id = m_auto_support_queue.front();
    m_auto_support_queue.pop_front();

    Domain::Project& project = m_project_interactor.selected_project();
    Domain::ModelObject* model_object = project.find_object_by_id(obj_id.id);
    if (!model_object) {
        SPDLOG_WARN("Auto support all: Model object {} not found, skipping", obj_id.id);
        process_auto_support_queue();
        return;
    }

    // Find a printable instance on a bed for this object
    const Domain::ModelInstance* instance = nullptr;
    for (const Domain::ModelInstance* inst : model_object->instances) {
        if (!inst || !inst->is_printable()) {
            continue;
        }
        const Domain::BedRef bed_ref = inst->get_last_bed();
        if (project.find_bed_instance_by_id(bed_ref.instance_id) != nullptr) {
            instance = inst;
            break;
        }
    }

    if (!instance) {
        SPDLOG_WARN("Auto support all: No printable instance on bed for object {}, skipping", obj_id.id);
        process_auto_support_queue();
        return;
    }

    const std::optional<ObjectSlaConfig> config_opt = build_object_sla_config(model_object, instance);
    if (!config_opt.has_value()) {
        SPDLOG_WARN("Auto support all: No full SLA print configuration for object {}, skipping", obj_id.id);
        process_auto_support_queue();
        return;
    }

    // Clear existing points if user chose to replace
    if (m_auto_support_keep_existing.has_value() && !*m_auto_support_keep_existing) {
        model_object->sla_support_points.clear();
    }

    {
        DialogSyncGuard guard(*this);
        m_dialog->set_generate_enabled(false);
        m_dialog->set_auto_support_all_enabled(false);
    }

    // One Points job per object, run one after another on the worker thread
    const Domain::BedRef bed_ref = instance->get_last_bed();
    WorkerJobData job_data;
    job_data.cloned_object = std::unique_ptr<Domain::ModelObject>(Domain::ModelObject::new_clone(*model_object));
    job_data.instance_matrix = instance->get_matrix();
    job_data.config = *config_opt;
    job_data.object_id = obj_id;
    job_data.instance_id = instance->id().id;
    job_data.slicing_id = Domain::SlicingId{m_project_interactor.selected_project_id(), bed_ref.instance_id};
    job_data.job_type = WorkerJobType::Points;
    job_data.for_auto_support_all = true;
    job_data.job_counter = ++m_job_counter;

    m_points_job_running = true;
    start_worker_job(std::move(job_data));
}

void SlaSupportPointsGizmo::on_auto_support_completed(Domain::ObjectID obj_id, std::optional<Domain::SLA::SupportPoints> support_points)
{
    m_points_job_running = false;

    if (support_points.has_value() && !support_points->empty()) {
        Domain::Project& project = m_project_interactor.selected_project();
        Domain::ModelObject* model_object = project.find_object_by_id(obj_id.id);
        if (model_object) {
            m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::SlaSupportPointsApply);

            // A generated point takes the tip shape, tip length, knot, stem cross-section and stem
            // taper of the settings of its own model, like a point placed by hand (M2.16c, M2.24).
            const SlaSupportGeometry geometry = support_geometry_defaults(model_object);
            for (Domain::SLA::SupportPoint& point : *support_points) {
                apply_support_geometry(point, geometry);
            }

            // Find a printable instance on a bed for this object to get instance_id
            Domain::SelectionId instance_id = 0;
            for (const Domain::ModelInstance* inst : model_object->instances) {
                if (!inst || !inst->is_printable()) {
                    continue;
                }
                const Domain::BedRef bed_ref = inst->get_last_bed();
                if (project.find_bed_instance_by_id(bed_ref.instance_id) != nullptr) {
                    instance_id = inst->id().id;
                    break;
                }
            }

            if (m_selected_object_id == obj_id) {
                DialogSyncGuard guard(*this);
                m_dialog->set_point_count(support_points->size());
            }

            if (instance_id != 0) {
                const Domain::ElementRef object_ref{obj_id.id, instance_id};
                m_project_interactor.scene_interactor().modify_sla_support_points(object_ref, [&](Domain::ModelObject& mo) {
                    mo.sla_support_points = std::move(*support_points);
                    mo.sla_points_status = PointsStatus::AutoGenerated;
                });
            } else {
                // Fallback: no valid instance found, just update without notification
                model_object->sla_support_points = std::move(*support_points);
                model_object->sla_points_status = PointsStatus::AutoGenerated;
            }
        }
    } else {
        SPDLOG_WARN("Auto support all: No support points could be generated for object {}", obj_id.id);
    }

    // Continue with next object in queue
    process_auto_support_queue();
}

void SlaSupportPointsGizmo::begin_editing()
{
    DialogSyncGuard guard(*this);

    Domain::Project& project = m_project_interactor.selected_project();
    Domain::ModelObject* model_object = project.find_object_by_id(m_selected_object_id.id);
    if (!model_object) {
        return;
    }

    // Remember the model's points before editing for discard/restore
    m_points_before_edit = model_object->sla_support_points;
    m_status_before_edit = model_object->sla_points_status;

    m_edit_state = SupportPointEditState{};
    m_edit_state->editing.points = model_object->sla_support_points;

    double pillar_diameter = 0.8;
    auto pillar_result = model_object->object_settings_sla.find("support_pillar_diameter");
    if (pillar_result.item) {
        pillar_diameter = pillar_result.item->get<double>();
    }
    m_edit_state->editing.pillar_diameter_mm = pillar_diameter;
    m_edit_state->editing.pillar_diameter_use_global = true;
    m_dialog->set_pillar_diameter(pillar_diameter);
    m_dialog->set_pillar_diameter_use_global(true);

    double base_diameter = 2.0;
    auto base_dia_result = model_object->object_settings_sla.find("support_base_diameter");
    if (base_dia_result.item) {
        base_diameter = base_dia_result.item->get<double>();
    }
    m_edit_state->editing.base_diameter_mm = base_diameter;
    m_edit_state->editing.base_diameter_use_global = true;
    m_dialog->set_base_diameter(base_diameter);
    m_dialog->set_base_diameter_use_global(true);

    double base_height = 1.0;
    auto base_ht_result = model_object->object_settings_sla.find("support_base_height");
    if (base_ht_result.item) {
        base_height = base_ht_result.item->get<double>();
    }
    m_edit_state->editing.base_height_mm = base_height;
    m_edit_state->editing.base_height_use_global = true;
    m_dialog->set_base_height(base_height);
    m_dialog->set_base_height_use_global(true);

    // The tip diameter, tip shape, tip length, knot, stem cross-section and stem taper a new point
    // takes (M2.16c, M2.24). The tip diameter is the head diameter control above, which takes the
    // configured value; the fields then show the selection, empty while the tool opens.
    m_edit_state->editing.support_geometry = support_geometry_defaults(model_object);
    m_dialog->set_head_diameter(m_edit_state->editing.support_geometry.tip_diameter_mm);
    m_dialog->set_head_diameter_use_global(true);
    update_selected_support_geometry();

    m_dialog->set_lock_island_supports(false);

    m_dialog->set_apply_enabled(true);
    m_dialog->set_generate_enabled(true);
    m_dialog->set_auto_support_all_enabled(true);

    update_point_visuals();
}

void SlaSupportPointsGizmo::end_editing()
{
    clear_point_visuals();
    m_hovered_point_idx.reset();
    m_edit_state.reset();
}

void SlaSupportPointsGizmo::commit_edited_points_live()
{
    if (!m_edit_state.has_value() || !m_selected_object_id.valid()) {
        return;
    }

    Domain::Project& project = m_project_interactor.selected_project();
    Domain::ModelObject* model_object = project.find_object_by_id(m_selected_object_id.id);
    if (!model_object) {
        return;
    }

    const Domain::ElementRef object_ref{m_selected_object_id.id, m_selected_instance_id};
    m_project_interactor.scene_interactor().modify_sla_support_points(object_ref, [&](Domain::ModelObject& mo) {
        mo.sla_support_points = m_edit_state->editing.points;
        mo.sla_points_status = PointsStatus::UserModified;
    });
}

void SlaSupportPointsGizmo::apply_edited_points()
{
    DialogSyncGuard guard(*this);

    if (!m_edit_state.has_value() || !m_selected_object_id.valid()) {
        return;
    }

    Domain::Project& project = m_project_interactor.selected_project();
    Domain::ModelObject* model_object = project.find_object_by_id(m_selected_object_id.id);
    if (!model_object) {
        return;
    }

    m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::SlaSupportPointsApply);

    const Domain::ElementRef object_ref{m_selected_object_id.id, m_selected_instance_id};
    m_project_interactor.scene_interactor().modify_sla_support_points(object_ref, [&](Domain::ModelObject& mo) {
        mo.sla_support_points = std::move(m_edit_state->editing.points);
        mo.sla_points_status = PointsStatus::UserModified;
    });

    m_dialog->set_point_count(model_object->sla_support_points.size());
    end_editing();
}

void SlaSupportPointsGizmo::discard_edited_points()
{
    DialogSyncGuard guard(*this);

    end_editing();
    m_dialog->set_apply_enabled(false);
    m_dialog->set_generate_enabled(true);
    m_dialog->set_auto_support_all_enabled(true);

    Domain::Project& project = m_project_interactor.selected_project();
    Domain::ModelObject* model_object = project.find_object_by_id(m_selected_object_id.id);
    if (model_object) {
        const Domain::ElementRef object_ref{m_selected_object_id.id, m_selected_instance_id};
        m_project_interactor.scene_interactor().modify_sla_support_points(object_ref, [&](Domain::ModelObject& mo) {
            mo.sla_support_points = m_points_before_edit;
            mo.sla_points_status = m_status_before_edit;
        });
        m_dialog->set_point_count(model_object->sla_support_points.size());
    }
}

void SlaSupportPointsGizmo::take_undo_snapshot()
{
    m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::SlaSupportPointsEdit);
}

// Hits are in the hit volume's local frame; sla_support_points live in the object's mesh frame.
Domain::Vec3d SlaSupportPointsGizmo::hit_to_object_pos(const VolumeHitPoint& hit) const
{
    return m_paintable_volumes[hit.volume_idx].model_volume.get_matrix() * hit.volume_hit_position;
}

std::optional<size_t> SlaSupportPointsGizmo::find_nearest_point(const Domain::Vec3d& mesh_pos, double max_distance_mm) const
{
    if (!m_edit_state.has_value()) {
        return std::nullopt;
    }

    return m_edit_state->editing.find_nearest_point(mesh_pos, max_distance_mm);
}

void SlaSupportPointsGizmo::add_point_at_mesh_pos(const Domain::Vec3d& mesh_pos)
{
    DialogSyncGuard guard(*this);

    if (!m_edit_state.has_value()) {
        return;
    }

    m_edit_state->editing.add_point(mesh_pos);
    m_dialog->set_point_count(m_edit_state->editing.points.size());
    take_undo_snapshot();
    update_point_visuals();
    commit_edited_points_live();
}

void SlaSupportPointsGizmo::remove_point_at_index(size_t idx)
{
    DialogSyncGuard guard(*this);

    if (!m_edit_state.has_value() || idx >= m_edit_state->editing.points.size()) {
        return;
    }

    m_edit_state->editing.remove_point(idx);
    m_dialog->set_point_count(m_edit_state->editing.points.size());
    take_undo_snapshot();
    update_point_visuals();
    commit_edited_points_live();
}

void SlaSupportPointsGizmo::move_point_to_mesh_pos(size_t idx, const Domain::Vec3d& mesh_pos)
{
    DialogSyncGuard guard(*this);

    if (!m_edit_state.has_value() || idx >= m_edit_state->editing.points.size()) {
        return;
    }

    m_edit_state->editing.move_point(idx, mesh_pos);
    m_dialog->set_point_count(m_edit_state->editing.points.size());
    update_point_visuals();
}

std::optional<SlaSupportPointsGizmo::VolumeHitPoint> SlaSupportPointsGizmo::raycast_mouse(const Domain::Vec2d& mouse_position) const
{
    if (m_paintable_volumes.empty()) {
        return std::nullopt;
    }

    const Scene::Camera& camera = m_scene_presenter.scene().camera();
    const Scene::Ray ray = camera.ray_at(mouse_position.x(), mouse_position.y());

    // Get the clipping plane for raycasting
    std::optional<Biz::ClippingPlane> clipping_plane_opt;
    if (m_clipping_plane_presenter.clipper().get_position() != 0.) {
        clipping_plane_opt = m_clipping_plane_presenter.clipper().get_clipping_plane();
    }

    Domain::Vec3d closest_hit_position = Domain::Vec3d::Zero();
    double closest_hit_squared_distance = std::numeric_limits<double>::max();
    size_t closest_facet_idx = 0;
    int closest_volume_idx = -1;

    for (const auto& paintable_volume : m_paintable_volumes) {
        const int volume_idx = &paintable_volume - &m_paintable_volumes.front();

        const std::optional<MeshRaycaster::UnprojectResult> unproject_result =
            MeshRaycaster::unproject_on_mesh(
                paintable_volume.aabb_mesh,
                ray,
                paintable_volume.world_trafo,
                clipping_plane_opt,
                true
            );

        if (!unproject_result.has_value()) {
            continue;
        }

        double hit_squared_distance =
            (ray.origin - paintable_volume.world_trafo * unproject_result->position).squaredNorm();
        if (hit_squared_distance < closest_hit_squared_distance) {
            closest_hit_squared_distance = hit_squared_distance;
            closest_facet_idx = unproject_result->facet_idx;
            closest_volume_idx = volume_idx;
            closest_hit_position = unproject_result->position;
        }
    }

    if (closest_volume_idx == -1) {
        return std::nullopt;
    }

    VolumeHitPoint hit;
    hit.volume_hit_position = closest_hit_position;
    hit.volume_idx = closest_volume_idx;
    hit.facet_idx = closest_facet_idx;
    return hit;
}

Scene::GizmoActivationState SlaSupportPointsGizmo::on_mouse(Scene::GizmoEventContext& ctx, bool only_active)
{
    using namespace Slic3r::App::Platform;

    const MouseEvent& mouse_event = ctx.mouse_event();
    const Domain::Vec2d mouse_position = Domain::Vec2f(ctx.screen_mouse_x(), ctx.screen_mouse_y()).cast<double>();

    const bool is_left_button_event =
        (mouse_event.button() & MouseButton::Left) == MouseButton::Left;
    const bool is_right_button_event =
        (mouse_event.button() & MouseButton::Right) == MouseButton::Right;

    const bool ctrl_down  = (mouse_event.key_modifiers() & KeyModifiers(KeyModifier::Ctrl)) != 0;
    const bool shift_down = (mouse_event.key_modifiers() & KeyModifiers(KeyModifier::Shift)) != 0;

    if (m_paintable_volumes.empty()) {
        return Scene::GizmoActivationState::Inactive;
    }

    if (!m_edit_state.has_value()) {
        begin_editing();
    }

    const std::optional<VolumeHitPoint> hit_opt = raycast_mouse(mouse_position);
    const bool has_hit = hit_opt.has_value();

    // Track hovered point (when not dragging or rectangle selecting)
    if (!m_edit_state->dragged_point_idx.has_value() && !m_edit_state->rect_select_active && has_hit) {
        const Domain::Vec3d mesh_pos = hit_to_object_pos(*hit_opt);
        const double hover_radius = m_edit_state->editing.support_geometry.tip_diameter_mm * 2.0;
        m_hovered_point_idx = find_nearest_point(mesh_pos, hover_radius);
    } else if (!has_hit || m_edit_state->dragged_point_idx.has_value() || m_edit_state->rect_select_active) {
        m_hovered_point_idx.reset();
    }

    // Handle mouse wheel for clipping plane (Ctrl + wheel)
    if (mouse_event.type() == MouseEvent::Type::Wheel) {
        if (ctrl_down) {
            const float wheel_rotation =
                mouse_event.wheel_delta_y() / std::abs(mouse_event.wheel_delta_y());
            double pos = m_clipping_plane_presenter.clipper().get_position();
            pos = (wheel_rotation > 0.f) ? std::min(1., pos + 0.01) : std::max(0., pos - 0.01);
            m_clipping_plane_presenter.set_position_by_ratio(pos, true);
            update_clipping_plane();

            DialogSyncGuard guard(*this);
            m_dialog->set_clipping_plane_position(pos);

            return Scene::GizmoActivationState::Done;
        }
    }

    // Left button down
    if (is_left_button_event && mouse_event.type() == MouseEvent::Type::ButtonDown) {
        // Ctrl+click: remove point
        if (ctrl_down) {
            if (has_hit) {
                const Domain::Vec3d mesh_pos = hit_to_object_pos(*hit_opt);
                const double removal_radius = m_edit_state->editing.support_geometry.tip_diameter_mm * 2.0;
                if (auto idx = find_nearest_point(mesh_pos, removal_radius); idx.has_value()) {
                    if (!m_edit_state->editing.lock_island_supports || !m_edit_state->editing.points[*idx].is_island()) {
                        remove_point_at_index(*idx);
                    }
                    return Scene::GizmoActivationState::Active;
                }
            }
            return Scene::GizmoActivationState::Inactive;
        }

        // Shift+click on empty space: start rectangle selection
        if (shift_down && !has_hit) {
            start_rectangle_selection(mouse_position, true);
            return Scene::GizmoActivationState::Probing;
        }

        // Shift+click on point: toggle selection
        if (shift_down && has_hit) {
            const Domain::Vec3d mesh_pos = hit_to_object_pos(*hit_opt);
            const double selection_radius = m_edit_state->editing.support_geometry.tip_diameter_mm * 2.0;
            if (auto idx = find_nearest_point(mesh_pos, selection_radius); idx.has_value()) {
                m_edit_state->editing.toggle_point(*idx);
                update_point_visuals();
                return Scene::GizmoActivationState::Active;
            }
        }

        // Regular click on point: select and start drag
        if (has_hit) {
            const Domain::Vec3d mesh_pos = hit_to_object_pos(*hit_opt);
            const double selection_radius = m_edit_state->editing.support_geometry.tip_diameter_mm * 2.0;
            if (auto idx = find_nearest_point(mesh_pos, selection_radius); idx.has_value()) {
                if (!m_edit_state->editing.lock_island_supports || !m_edit_state->editing.points[*idx].is_island()) {
                    clear_selection();
                    select_point(*idx);
                    m_edit_state->dragged_point_idx = idx;
                    m_edit_state->drag_start_world_pos = m_paintable_volumes[hit_opt->volume_idx].world_trafo * hit_opt->volume_hit_position;
                    m_edit_state->drag_start_mesh_pos = mesh_pos;
                }
                return Scene::GizmoActivationState::Active;
            } else {
                // Click on empty model surface: add point
                clear_selection();
                add_point_at_mesh_pos(mesh_pos);
                return Scene::GizmoActivationState::Active;
            }
        }

        // Click on empty space: clear selection
        clear_selection();
        update_point_visuals();
        return Scene::GizmoActivationState::Inactive;
    }

    // Right button down: remove point (or deselect if locked)
    if (is_right_button_event && mouse_event.type() == MouseEvent::Type::ButtonDown) {
        if (has_hit) {
            const Domain::Vec3d mesh_pos = hit_to_object_pos(*hit_opt);
            const double removal_radius = m_edit_state->editing.support_geometry.tip_diameter_mm * 2.0;
            if (auto idx = find_nearest_point(mesh_pos, removal_radius); idx.has_value()) {
                if (!m_edit_state->editing.lock_island_supports || !m_edit_state->editing.points[*idx].is_island()) {
                    remove_point_at_index(*idx);
                }
                return Scene::GizmoActivationState::Active;
            }
        }
        return Scene::GizmoActivationState::Inactive;
    }

    // Mouse move during drag
    if (mouse_event.type() == MouseEvent::Type::Move && m_edit_state->dragged_point_idx.has_value()) {
        if (has_hit) {
            const Domain::Vec3d mesh_pos = hit_to_object_pos(*hit_opt);
            move_point_to_mesh_pos(*m_edit_state->dragged_point_idx, mesh_pos);
            return Scene::GizmoActivationState::Active;
        }
        return Scene::GizmoActivationState::Inactive;
    }

    // Mouse move during rectangle selection
    if (mouse_event.type() == MouseEvent::Type::Move && m_edit_state->rect_select_active) {
        update_rectangle_selection(mouse_position);
        return Scene::GizmoActivationState::Active;
    }

    // Button up
    if ((is_left_button_event || is_right_button_event) && mouse_event.type() == MouseEvent::Type::ButtonUp) {
        if (m_edit_state->dragged_point_idx.has_value()) {
            take_undo_snapshot();
            m_edit_state->dragged_point_idx.reset();
            update_point_visuals();
            commit_edited_points_live();
            return Scene::GizmoActivationState::Active;
        }
        if (m_edit_state->rect_select_active) {
            finish_rectangle_selection();
            return Scene::GizmoActivationState::Active;
        }
        return Scene::GizmoActivationState::Inactive;
    }

    return Scene::GizmoActivationState::Inactive;
}

std::unique_ptr<GizmoWindow> SlaSupportPointsGizmo::release_ui_window()
{
    return m_dialog.release();
}

// Visuals

void SlaSupportPointsGizmo::update_point_visuals()
{
    if (!m_edit_state.has_value() || m_points_node == nullptr) {
        return;
    }

    const auto& points = m_edit_state->editing.points;
    if (points.empty()) {
        clear_point_visuals();
        return;
    }

    Scene::Scene& scene = m_scene_presenter.scene();

    // Get the instance transform with support elevation applied
    const Domain::Project& project = m_project_interactor.selected_project();
    const Domain::ModelInstance* instance = project.find_instance_by_id(m_selected_object_id.id, m_selected_instance_id);
    if (!instance) {
        return;
    }
    const double elevation = support_elevation();
    const Domain::Transform3d lift = Domain::translation_transform(Domain::Vec3d(0., 0., elevation));
    const Domain::Transform3d instance_trafo = lift * instance->get_matrix();

    // Sphere geometry (shared for all points)
    static constexpr double SPHERE_RESOLUTION_ANGLE = Slic3r::deg2rad(360.0 / 32.0);
    const std::string sphere_id = "support_point_sphere";
    auto sphere_trimesh = m_triangle_mesh_manager.get_or_create(sphere_id, [this]() {
        Domain::TriangleMesh mesh = Biz::Algorithms::TriangleMesh::make_sphere(1.0, SPHERE_RESOLUTION_ANGLE);
        return std::make_unique<Scene::TriangleMesh>(std::move(mesh.its));
    });
    const auto* sphere_geom = m_geometry_manager.get_or_create(sphere_id, [&]() {
        return Render::geometry_from_triangle_mesh(m_device, sphere_trimesh->triangles());
    });

    // Cone geometry for selected points (surface normal visualization)
    create_cone_geometry_if_needed();
    const auto* cone_geom = m_geometry_manager.get(m_cone_geometry_id);

    // Clear existing point nodes
    scene.remove_children([this](const Scene::Node* node) {
        return node->parent() == m_points_node;
    }, m_points_node);

    // Determine highlighted index (dragged or hovered)
    std::optional<size_t> highlighted_idx = m_edit_state->dragged_point_idx;
    if (!highlighted_idx.has_value()) {
        highlighted_idx = m_hovered_point_idx;
    }

    // Cone parameters (matching legacy)
    static constexpr double CONE_RADIUS = 0.25;
    static constexpr double CONE_HEIGHT = 0.75;

    for (size_t i = 0; i < points.size(); ++i) {
        const auto& point = points[i];
        const bool highlighted = highlighted_idx.has_value() && *highlighted_idx == i;
        const bool is_selected = m_edit_state->editing.selected_point_indices.count(i) > 0;
        const bool is_locked_island = m_edit_state->editing.lock_island_supports && point.is_island();

        // Point position in world space: instance_trafo * point.pos (point.pos is in mesh coords)
        Domain::Vec3d world_pos = instance_trafo * point.pos.cast<double>();

        // Radius = head_front_radius (minimum 0.2 mm)
        double radius = std::max(static_cast<double>(point.head_front_radius), 0.2);

        // Color based on point type and state
        const PointGlyphState glyph_state = highlighted   ? PointGlyphState::Hovered
                                     : is_selected      ? PointGlyphState::Selected
                                                        : PointGlyphState::Resting;
        ColorRGBA color = get_point_color(point, glyph_state);

        Render::Material material = Render::Material{}
            .set_shader(m_device.context().shader_manager().shader("gouraud_light"))
            .set_uniform("uniform_color", color);

        Domain::Transform3d xform = Domain::Transform3d::Identity();
        xform.translate(world_pos);
        xform.scale(radius);

        Scene::NodeBuilder builder{scene};
        builder.set_debug_name(fmt::format("support_point_{}", i))
            .set_mesh(sphere_geom, material, Scene::RenderLayerId(PlaterSceneLayer::GizmoHandles))
            .set_aabb(sphere_trimesh->aabb_mesh())
            .set_transform(xform);

        scene.add_child(builder.build().release(), m_points_node);

// Draw cone for selected points (editing mode visual) - pointing along surface normal
        if (is_selected && cone_geom) {
            // Find surface normal by raycasting downward from the point
            Domain::Vec3d normal_world = Domain::Vec3d::UnitZ(); // Default upward
            
            // Raycast from slightly above the point down to the mesh
            for (const auto& paintable_volume : m_paintable_volumes) {
                const Domain::Transform3d volume_trafo = paintable_volume.world_trafo;
                const Domain::Vec3d point_world = world_pos;
                const Domain::Vec3d point_volume = volume_trafo.inverse() * point_world;
                
                // Cast ray downward in world space
                App::Scene::Ray ray;
                ray.origin = point_world + Domain::Vec3d::UnitZ() * 10.0;
                ray.direction = -Domain::Vec3d::UnitZ();
                
                const std::optional<MeshRaycaster::UnprojectResult> result =
                    MeshRaycaster::unproject_on_mesh(
                        paintable_volume.aabb_mesh,
                        ray,
                        volume_trafo,
                        std::nullopt,
                        true
                    );
                
                if (result.has_value()) {
                    // The normal from unproject_on_mesh is in world space
                    normal_world = result->normal;
                    normal_world.normalize();
                    break;
                }
            }

            // Create cone transform: point along normal, base at sphere surface
            Eigen::Quaterniond q;
            q.setFromTwoVectors(Domain::Vec3d::UnitZ(), normal_world);
            Domain::Transform3d cone_xform = Domain::Transform3d::Identity();
            cone_xform.translate(world_pos + normal_world * (radius + CONE_HEIGHT * 0.5 * radius));
            cone_xform.rotate(q);
            cone_xform.scale(Domain::Vec3d(radius * CONE_RADIUS, radius * CONE_RADIUS, radius * CONE_HEIGHT));

            Render::Material cone_material = Render::Material{}
                .set_shader(m_device.context().shader_manager().shader("gouraud_light"))
                .set_uniform("uniform_color", color);

            Scene::NodeBuilder cone_builder{scene};
            cone_builder.set_debug_name(fmt::format("support_point_cone_{}", i))
                .set_mesh(cone_geom, cone_material, Scene::RenderLayerId(PlaterSceneLayer::GizmoHandles))
                .set_transform(cone_xform);

            scene.add_child(cone_builder.build().release(), m_points_node);
        }
    }
}

void SlaSupportPointsGizmo::clear_point_visuals()
{
    if (m_points_node != nullptr) {
        Scene::Scene& scene = m_scene_presenter.scene();
        scene.remove_children([this](const Scene::Node* node) {
            return node->parent() == m_points_node;
        }, m_points_node);
    }
}

Domain::ColorRGBA SlaSupportPointsGizmo::get_point_color(const Domain::SLA::SupportPoint& point, PointGlyphState state) const
{
    const auto& theme = AppServices::instance().theme();

    // A hovered or selected glyph takes the state token instead of its type token, so the five
    // states stay apart in a grayscale render (M2.9c). An island point under the pointer or in the
    // selection therefore reads as hovered / selected, not as an island; its cone marker, the
    // island lock and the amber counts in the dialog carry that.
    if (state == PointGlyphState::Hovered) {
        return theme.color(Platform::Color::SlaSupportPointHovered, Platform::ColorGroup::Default);
    }
    if (state == PointGlyphState::Selected) {
        return theme.color(Platform::Color::SlaSupportPointSelected, Platform::ColorGroup::Default);
    }

    switch (point.type) {
    case SupportPointType::manual_add:
        return theme.color(Platform::Color::SlaSupportPointManual, Platform::ColorGroup::Default);
    case SupportPointType::island:
        return theme.color(Platform::Color::SlaIslandWarning, Platform::ColorGroup::Default);
    case SupportPointType::slope:
    default:
        return theme.color(Platform::Color::SlaSupportPointAuto, Platform::ColorGroup::Default);
    }
}

// Clipping plane

void SlaSupportPointsGizmo::update_clipping_plane()
{
    // Update the clipper presenter which updates the scene nodes
    const auto& clipper = m_clipping_plane_presenter.clipper();
    m_clipping_plane_presenter.update_clipper(
        clipper.get_clipping_plane().get_normal(),
        clipper.get_clipping_plane().get_offset(),
        clipper.get_position(),
        false
    );
}

void SlaSupportPointsGizmo::reset_clipping_plane()
{
    DialogSyncGuard guard(*this);

    m_clipping_plane_presenter.set_position_by_ratio(-1., false);
    update_clipping_plane();
    m_dialog->set_clipping_plane_position(m_clipping_plane_presenter.clipper().get_position());
}

// Selection helpers

void SlaSupportPointsGizmo::select_point(size_t idx, bool add_to_selection)
{
    if (!m_edit_state.has_value()) {
        return;
    }
    m_edit_state->editing.select_point(idx, add_to_selection);
    update_selected_support_geometry();
}

void SlaSupportPointsGizmo::deselect_point(size_t idx)
{
    if (!m_edit_state.has_value()) {
        return;
    }
    m_edit_state->editing.deselect_point(idx);
    update_selected_support_geometry();
}

void SlaSupportPointsGizmo::select_all_points()
{
    if (!m_edit_state.has_value()) {
        return;
    }
    m_edit_state->editing.select_all_points();
    update_point_visuals();
    update_selected_support_geometry();
}

void SlaSupportPointsGizmo::clear_selection()
{
    if (!m_edit_state.has_value()) {
        return;
    }
    m_edit_state->editing.clear_selection();
    update_point_visuals();
    update_selected_support_geometry();
}

void SlaSupportPointsGizmo::delete_selected_points()
{
    if (!m_edit_state.has_value()) {
        return;
    }

    const size_t old_count = m_edit_state->editing.points.size();
    m_edit_state->editing.delete_selected_points();

    if (m_edit_state->editing.points.size() != old_count) {
        m_dialog->set_point_count(m_edit_state->editing.points.size());
        take_undo_snapshot();
        update_point_visuals();
        update_selected_support_geometry();
        commit_edited_points_live();
    }
}

void SlaSupportPointsGizmo::apply_pillar_diameter_to_selected()
{
    if (!m_edit_state.has_value()) {
        return;
    }
    take_undo_snapshot();
    m_edit_state->editing.apply_pillar_diameter_to_selected();
    update_point_visuals();
    commit_edited_points_live();
}

void SlaSupportPointsGizmo::apply_base_diameter_to_selected()
{
    if (!m_edit_state.has_value()) {
        return;
    }
    take_undo_snapshot();
    m_edit_state->editing.apply_base_diameter_to_selected();
    update_point_visuals();
    commit_edited_points_live();
}

void SlaSupportPointsGizmo::apply_base_height_to_selected()
{
    if (!m_edit_state.has_value()) {
        return;
    }
    take_undo_snapshot();
    m_edit_state->editing.apply_base_height_to_selected();
    update_point_visuals();
    commit_edited_points_live();
}

void SlaSupportPointsGizmo::apply_preset_mini()
{
    const auto [head_diameter, pillar_diameter, base_diameter, base_height] = get_support_preset_values("mini");
    apply_support_preset(static_cast<float>(head_diameter), static_cast<float>(pillar_diameter),
                         static_cast<float>(base_diameter), static_cast<float>(base_height), 0);
}

void SlaSupportPointsGizmo::apply_support_geometry_to_selected(SupportGeometryField field)
{
    if (!m_edit_state.has_value()) {
        return;
    }
    take_undo_snapshot();
    m_edit_state->editing.apply_support_geometry_to_selected(field);
    // The points now carry the new value, so the fields keep showing it.
    update_selected_support_geometry();
    update_point_visuals();
    commit_edited_points_live();
}

void SlaSupportPointsGizmo::apply_support_on_model_to_selected(SupportOnModel on_model)
{
    if (!m_edit_state.has_value()) {
        return;
    }
    take_undo_snapshot();
    m_edit_state->editing.apply_support_on_model_to_selected(on_model);
    // The points now carry the new state, so the control keeps showing it.
    update_selected_support_geometry();
    update_point_visuals();
    // The tree the preview shows is built from the points, so a change of where a pillar ends
    // has to reach the model like any other edit of a point (M2.26).
    commit_edited_points_live();
}

void SlaSupportPointsGizmo::update_selected_support_geometry()
{
    DialogSyncGuard guard(*this);
    if (!m_edit_state.has_value()) {
        m_dialog->set_support_geometry(std::nullopt, false);
        m_dialog->set_support_on_model(std::nullopt);
        return;
    }
    // The dialog needs to tell "nothing is selected" from "the points disagree": the tip diameter is
    // the head diameter control as well, which keeps showing what a new point takes when there is
    // no selection to show.
    m_dialog->set_support_geometry(m_edit_state->editing.selected_support_geometry(),
                                   !m_edit_state->editing.selected_point_indices.empty());
    // The "may rest on the model" switch has a value of its own, so it is shown for a selection
    // that agrees on it even when the selection shows no geometry at all (M2.26).
    m_dialog->set_support_on_model(m_edit_state->editing.selected_support_on_model());
}

SlaSupportGeometry SlaSupportPointsGizmo::support_geometry_defaults(const Domain::ModelObject* model_object) const
{
    SlaSupportGeometry geometry;
    if (!model_object) {
        return geometry;
    }

    const auto& settings = model_object->object_settings_sla;

    // The tip diameter is the "head diameter" control of the tool (M2.24), so a point placed by hand
    // takes the configured one and a preset button replaces it.
    if (auto result = settings.find("support_head_front_diameter"); result.item != nullptr) {
        geometry.tip_diameter_mm = result.item->get<double>();
    }
    if (auto result = settings.find("support_tip_shape"); result.item != nullptr) {
        geometry.tip_shape = support_tip_shape_of(result.item->get<Domain::sla::SupportTipShape>());
    }
    if (auto result = settings.find("support_tip_length"); result.item != nullptr) {
        geometry.tip_length_mm = result.item->get<double>();
    }
    if (auto result = settings.find("support_knot_diameter"); result.item != nullptr) {
        geometry.knot_diameter_mm = result.item->get<double>();
    }
    if (auto result = settings.find("support_stem_sides"); result.item != nullptr) {
        geometry.stem_sides = result.item->get<int>();
    }
    if (auto result = settings.find("support_stem_taper"); result.item != nullptr) {
        geometry.stem_taper = result.item->get<double>();
    }
    // The foot of a new point is the shape support_base_shape asks for (M2.23b), so a point placed
    // by hand gets the foot the user configured rather than the cone of before.
    if (auto result = settings.find("support_base_shape"); result.item != nullptr) {
        geometry.base_shape = support_base_shape_of(result.item->get<Domain::sla::SupportBaseShape>());
    }

    return geometry;
}

void SlaSupportPointsGizmo::apply_preset_light()
{
    const auto [head_diameter, pillar_diameter, base_diameter, base_height] = get_support_preset_values("light");
    apply_support_preset(static_cast<float>(head_diameter), static_cast<float>(pillar_diameter),
                         static_cast<float>(base_diameter), static_cast<float>(base_height), 1);
}

void SlaSupportPointsGizmo::apply_preset_medium()
{
    const auto [head_diameter, pillar_diameter, base_diameter, base_height] = get_support_preset_values("medium");
    apply_support_preset(static_cast<float>(head_diameter), static_cast<float>(pillar_diameter),
                         static_cast<float>(base_diameter), static_cast<float>(base_height), 2);
}

void SlaSupportPointsGizmo::apply_preset_heavy()
{
    const auto [head_diameter, pillar_diameter, base_diameter, base_height] = get_support_preset_values("heavy");
    apply_support_preset(static_cast<float>(head_diameter), static_cast<float>(pillar_diameter),
                         static_cast<float>(base_diameter), static_cast<float>(base_height), 3);
}

std::tuple<double, double, double, double> SlaSupportPointsGizmo::get_support_preset_values(const std::string& preset_name) const
{
    const auto& config_box = m_project_interactor.preset_interactor().selected_printer_preset().print.config_box();
    const std::string prefix = "support_preset_" + preset_name + "_";

    auto get_value = [&](const std::string& suffix, double fallback) -> double {
        const auto* item = config_box.items.find(prefix + suffix);
        return item ? item->get<double>() : fallback;
    };

    // The same defaults as the config definitions, for a preset the config box does not have.
    const double head_diameter = get_value("head_diameter",
        preset_name == "mini" ? 0.2 : (preset_name == "light" ? 0.30 : (preset_name == "medium" ? 0.45 : 0.60)));
    const double pillar_diameter = get_value("pillar_diameter",
        preset_name == "mini" ? 0.5 : (preset_name == "light" ? 0.8 : (preset_name == "medium" ? 1.2 : 1.8)));
    const double base_diameter = get_value("base_diameter",
        preset_name == "mini" ? 1.4 : (preset_name == "light" ? 2.0 : (preset_name == "medium" ? 3.0 : 4.0)));
    const double base_height = get_value("base_height",
        preset_name == "mini" ? 0.4 : (preset_name == "light" ? 0.5 : (preset_name == "medium" ? 0.7 : 1.0)));

    return {head_diameter, pillar_diameter, base_diameter, base_height};
}

void SlaSupportPointsGizmo::apply_support_preset(float head_diameter, float pillar_diameter, float base_diameter, float base_height, int preset_index)
{
    DialogSyncGuard guard(*this);

    if (!m_edit_state.has_value()) {
        return;
    }

    m_edit_state->editing.support_geometry.tip_diameter_mm = head_diameter;
    m_edit_state->editing.pillar_diameter_mm = pillar_diameter;
    m_edit_state->editing.base_diameter_mm = base_diameter;
    m_edit_state->editing.base_height_mm = base_height;
    m_edit_state->editing.head_diameter_use_global = false;
    m_edit_state->editing.pillar_diameter_use_global = false;
    m_edit_state->editing.base_diameter_use_global = false;
    m_edit_state->editing.base_height_use_global = false;

    m_dialog->set_head_diameter(head_diameter);
    m_dialog->set_pillar_diameter(pillar_diameter);
    m_dialog->set_base_diameter(base_diameter);
    m_dialog->set_base_height(base_height);
    m_dialog->set_head_diameter_use_global(false);
    m_dialog->set_pillar_diameter_use_global(false);
    m_dialog->set_base_diameter_use_global(false);
    m_dialog->set_base_height_use_global(false);
    m_dialog->set_active_preset(preset_index);

    if (!m_edit_state->editing.selected_point_indices.empty()) {
        take_undo_snapshot();
        m_edit_state->editing.apply_support_geometry_to_selected(SupportGeometryField::TipDiameter);
        m_edit_state->editing.apply_pillar_diameter_to_selected();
        m_edit_state->editing.apply_base_diameter_to_selected();
        m_edit_state->editing.apply_base_height_to_selected();
        update_point_visuals();
        commit_edited_points_live();
    }
}

// Rectangle selection

void SlaSupportPointsGizmo::start_rectangle_selection(const Domain::Vec2d& mouse_pos, bool is_add)
{
    if (!m_edit_state.has_value()) {
        return;
    }
    m_edit_state->rect_select_active = true;
    m_edit_state->rect_select_start_pos = mouse_pos;
    m_edit_state->rect_select_current_pos = mouse_pos;
    m_edit_state->rect_select_is_add = is_add;
}

void SlaSupportPointsGizmo::update_rectangle_selection(const Domain::Vec2d& mouse_pos)
{
    if (!m_edit_state.has_value() || !m_edit_state->rect_select_active) {
        return;
    }
    m_edit_state->rect_select_current_pos = mouse_pos;
    // Visual feedback would go here if we had a rectangle drawing API
    // For now, just update the state
}

void SlaSupportPointsGizmo::finish_rectangle_selection()
{
    if (!m_edit_state.has_value() || !m_edit_state->rect_select_active) {
        return;
    }

    std::vector<Domain::Vec2d> screen_positions;
    project_points_to_screen(screen_positions);

    const Domain::Vec2d rect_min(
        std::min(m_edit_state->rect_select_start_pos.x(), m_edit_state->rect_select_current_pos.x()),
        std::min(m_edit_state->rect_select_start_pos.y(), m_edit_state->rect_select_current_pos.y())
    );
    const Domain::Vec2d rect_max(
        std::max(m_edit_state->rect_select_start_pos.x(), m_edit_state->rect_select_current_pos.x()),
        std::max(m_edit_state->rect_select_start_pos.y(), m_edit_state->rect_select_current_pos.y())
    );

    std::vector<size_t> indices = SlaSupportPointsEditing::points_in_rectangle(screen_positions, rect_min, rect_max);

    if (m_edit_state->rect_select_is_add) {
        for (size_t idx : indices) {
            if (!m_edit_state->editing.lock_island_supports || !m_edit_state->editing.points[idx].is_island()) {
                m_edit_state->editing.select_point(idx, true);
            }
        }
    } else {
        for (size_t idx : indices) {
            m_edit_state->editing.deselect_point(idx);
        }
    }

    m_edit_state->rect_select_active = false;
    update_point_visuals();
    update_selected_support_geometry();
}

void SlaSupportPointsGizmo::project_points_to_screen(std::vector<Domain::Vec2d>& out_screen_positions) const
{
    if (!m_edit_state.has_value() || m_paintable_volumes.empty()) {
        return;
    }

    const Scene::Camera& camera = m_scene_presenter.scene().camera();
    const Domain::Project& project = m_project_interactor.selected_project();
    const Domain::ModelInstance* instance = project.find_instance_by_id(m_selected_object_id.id, m_selected_instance_id);
    if (!instance) {
        return;
    }
    const double elevation = support_elevation();
    const Domain::Transform3d lift = Domain::translation_transform(Domain::Vec3d(0., 0., elevation));
    const Domain::Transform3d instance_trafo = lift * instance->get_matrix();

    out_screen_positions.resize(m_edit_state->editing.points.size());

    for (size_t i = 0; i < m_edit_state->editing.points.size(); ++i) {
        const Domain::Vec3d world_pos = instance_trafo * m_edit_state->editing.points[i].pos.cast<double>();
        Domain::Vec2d screen_pos = camera.project_to_screen_space(world_pos);
        out_screen_positions[i] = screen_pos;
    }
}

// Cone visual

void SlaSupportPointsGizmo::create_cone_geometry_if_needed()
{
    if (m_cone_geometry_created) {
        return;
    }

    static constexpr double CONE_RESOLUTION_ANGLE = Slic3r::deg2rad(360.0 / 32.0);
    Domain::TriangleMesh mesh = Biz::Algorithms::TriangleMesh::make_cone(1.0, 1.0, CONE_RESOLUTION_ANGLE);
    auto cone_trimesh = std::make_unique<Scene::TriangleMesh>(std::move(mesh.its));
    const auto* cone_geom = m_geometry_manager.get_or_create(m_cone_geometry_id, [&]() {
        return Render::geometry_from_triangle_mesh(m_device, cone_trimesh->triangles());
    });
    (void)cone_geom;
    m_cone_geometry_created = true;
}

void SlaSupportPointsGizmo::on_keyboard(Scene::GizmoKeyEventContext& ctx)
{
    const Platform::KeyboardEvent& evt = ctx.keyboard_event();
    if (evt.is_repeat() || !m_edit_state.has_value()) {
        return;
    }

    // A shortcut acts on the key press, a release asks for nothing.
    if (evt.type() != Platform::KeyboardEvent::Type::KeyDown) {
        return;
    }

    SupportToolKeyEvent key;
    key.code        = evt.code();
    key.modifiers   = evt.key_modifiers();
    key.tool_active = m_gizmo_active;
    // A text field owns the keys while it has the focus, so a number typed into one of the tool's
    // own inputs does not pick a preset. The canvas already drops those keys before they get here
    // (AbstractRenderCanvas::emit_enqueued_events), this is the tool asking about it anyway.
    key.text_field_focus = ImGui::GetIO().WantTextInput;
    key.has_selection    = !m_edit_state->editing.selected_point_indices.empty();

    switch (support_tool_action_for(key)) {
    case SupportToolAction::None:
        return;
    case SupportToolAction::PresetMini:
        apply_preset_mini();
        break;
    case SupportToolAction::PresetLight:
        apply_preset_light();
        break;
    case SupportToolAction::PresetMedium:
        apply_preset_medium();
        break;
    case SupportToolAction::PresetHeavy:
        apply_preset_heavy();
        break;
    case SupportToolAction::AutoSupportSelection:
        auto_support({m_selected_object_id});
        break;
    case SupportToolAction::AutoSupportAll:
        auto_support();
        break;
    case SupportToolAction::ToggleSupportOnModel:
        apply_support_on_model_to_selected(
            support_tool_toggled_on_model(m_edit_state->editing.selected_support_on_model())
        );
        break;
    case SupportToolAction::ClearSelection:
        clear_selection();
        // The selection was the tool's own answer to Escape, so the tool keeps open and the next
        // Escape, with nothing selected, closes it.
        ctx.consume();
        break;
    case SupportToolAction::SelectAllPoints:
        select_all_points();
        break;
    case SupportToolAction::DeleteSelectedPoints:
        delete_selected_points();
        break;
    }
}

void SlaSupportPointsGizmo::render_scene(Render::CommandBuffer& cmd_buffer)
{
    // Update point visuals each frame to reflect hover/drag state
    update_point_visuals();
}

double SlaSupportPointsGizmo::support_elevation() const
{
    if (!m_selected_object_id.valid()) {
        return 0.;
    }

    const Domain::Project& project = m_project_interactor.selected_project();
    const Domain::ModelObject* model_object = project.find_object_by_id(m_selected_object_id.id);
    if (!model_object) {
        return 0.;
    }

    const Domain::ModelInstance* instance = project.find_instance_by_id(m_selected_object_id.id, m_selected_instance_id);
    if (!instance) {
        return 0.;
    }

    const std::optional<ObjectSlaConfig> config_opt =
        build_object_sla_config(model_object, instance);
    if (!config_opt.has_value()) {
        return 0.;
    }

    return sla::support_tool_elevation(config_opt->full, config_opt->object);
}

std::optional<SlaSupportPointsGizmo::ObjectSlaConfig> SlaSupportPointsGizmo::build_object_sla_config(
    const Domain::ModelObject* model_object,
    const Domain::ModelInstance* instance) const
{
    // The preview service resolves the configuration, so the tool and the plate always agree.
    const std::optional<SlaSupportPreviewService::ObjectSlaConfig> config =
        m_support_preview_service.build_object_sla_config(
            m_project_interactor.selected_project(), model_object, instance);
    if (!config.has_value()) {
        return std::nullopt;
    }
    return ObjectSlaConfig{config->full, config->object};
}

void SlaSupportPointsGizmo::start_worker_job(WorkerJobData&& job_data)
{
    m_active_job = std::move(job_data);
    m_worker = Biz::JThread::JThread([this](Biz::JThread::StopToken stop_token, WorkerJobData job_data) mutable {
        WorkerJobResult result;
        result.object_id = job_data.object_id;
        result.instance_id = job_data.instance_id;
        result.slicing_id = job_data.slicing_id;
        result.job_type = job_data.job_type;
        result.for_auto_support_all = job_data.for_auto_support_all;
        result.job_counter = job_data.job_counter;

        const SupportToolStop stop = [&stop_token]() {
            return stop_token.stop_requested();
        };

        if (job_data.job_type == WorkerJobType::Points) {
            result.points = sla::generate_support_points_for_tool(
                *job_data.cloned_object,
                job_data.instance_matrix,
                job_data.config.full,
                job_data.config.object,
                stop);
        }

        if (stop()) {
            result.cancelled = true;
        }

        Biz::Platform::PlatformServices::instance().main_thread_dispatcher().dispatch_on_main_thread(
            [this, result = std::move(result)]() mutable {
                on_worker_job_completed(std::move(result));
            });
    }, std::move(m_active_job.value()));
}

void SlaSupportPointsGizmo::cancel_worker_job()
{
    if (m_worker.joinable()) {
        m_worker.request_stop();
        m_worker.join();
    }
    ++m_job_counter;
    m_points_job_running = false;
    m_active_job.reset();
}

void SlaSupportPointsGizmo::on_worker_job_completed(WorkerJobResult&& result)
{
    if (!m_gizmo_active || result.job_counter != m_job_counter) {
        return;
    }

    m_active_job.reset();

    if (result.job_type == WorkerJobType::Points) {
        on_points_job_completed(std::move(result.points), result.object_id, result.for_auto_support_all, result.job_counter);
    }
}

void SlaSupportPointsGizmo::on_points_job_completed(std::optional<Domain::SLA::SupportPoints> points, Domain::ObjectID object_id, bool for_auto_support_all, size_t job_counter)
{
    (void)job_counter;

    if (for_auto_support_all) {
        on_auto_support_completed(object_id, std::move(points));
        return;
    }

    on_generation_completed(std::move(points));
}

} // namespace Slic3r::App::Plater
