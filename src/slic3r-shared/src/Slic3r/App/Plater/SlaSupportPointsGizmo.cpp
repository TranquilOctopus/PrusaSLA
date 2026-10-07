#include "Slic3r/App/Plater/SlaSupportPointsGizmo.hpp"
#include "Slic3r/App/Plater/SlaSupportAutoPresets.hpp"
#include "Slic3r/App/Plater/SlaSupportPointPick.hpp"
#include "Slic3r/App/Plater/SlaSupportPointEdits.hpp"
#include "Slic3r/App/Plater/SlaSupportPointsClear.hpp"
#include "Slic3r/App/Plater/SlaSupportPointsEditing.hpp"
#include "Slic3r/App/Plater/SlaSupportPointsLeaving.hpp"
#include "Slic3r/App/Plater/SlaSupportPointsLift.hpp"
#include "Slic3r/App/Plater/SlaSupportPointsSettings.hpp"
#include "Slic3r/App/Plater/SlaSupportToolShortcuts.hpp"

#include "Slic3r/App/Plater/SlaSupportPointsDialog.hpp"
#include "Slic3r/App/Plater/PlaterScenePresenter.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/IDialogManager.hpp"
#include "Slic3r/App/IsSlaActive.hpp"
#include "Slic3r/App/DisplayStrings.hpp"
#include "Slic3r/App/Scene/Scene.hpp"
#include "Slic3r/App/Scene/NodeBuilder.hpp"
#include "Slic3r/App/Scene/Camera.hpp"
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
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "jthread/JThread.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/Domain/SlaLayerHeight.hpp"
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
#include <cstddef>

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

namespace {

// How large the glyph of a support point is drawn, in mm. The head radius of the point, and never
// less than a fifth of a millimeter, so a point the generator gave a tiny head is still visible and
// still clickable (the same value the glyphs are drawn with, and the size the picking measures on
// the screen).
double point_glyph_radius_mm(const SupportPoint& point)
{
    return std::max(static_cast<double>(point.head_front_radius), 0.2);
}

} // namespace

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
    m_dialog->callbacks().remove_all_points = [this]() { this->remove_all_points(); };
    // The "Delete" button of the "Selected supports" group: the same action the Delete key, Ctrl and
    // the right button take, so removing one support is one gesture in the panel as well (M2.38).
    m_dialog->callbacks().delete_selected_points = [this]() { this->delete_selected_points(); };
    m_dialog->callbacks().value_editing_started = [this]() { this->on_value_editing_started(); };
    m_dialog->callbacks().value_editing_ended = [this]() { this->on_value_editing_ended(); };
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
                take_undo_snapshot_for_value_edit();
                auto result = model_object->object_settings_sla.find("support_points_density_relative");
                if (result.item) {
                    result.item->set<int>(density);
                }
            }
        }
    };
    // The two groups of the support settings (M2.33). Before this the same field set both jobs, which
    // is what made it unclear what a changed value touched: the group says whether the value is what
    // the next clicked point takes or what the selected points carry.
    m_dialog->callbacks().support_setting_changed =
        [this](SlaSupportSettingsGroup group, SlaSupportPointField field, double value)
    {
        if (m_syncing_dialog) {
            return;
        }
        if (!m_edit_state.has_value()) {
            return;
        }
        if (group == SlaSupportSettingsGroup::NewSupports) {
            this->apply_new_support_setting(field, value);
        } else {
            this->apply_selected_support_setting(field, value);
        }
    };
    m_dialog->callbacks().support_preset_selected = [this](SlaSupportSettingsGroup group, int index)
    {
        if (m_syncing_dialog) {
            return;
        }
        if (group == SlaSupportSettingsGroup::NewSupports) {
            this->apply_new_support_preset(index);
        } else {
            this->apply_selected_support_preset(index);
        }
    };
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
    m_project_interactor.scene_interactor().add_listener<Biz::Scene::ISceneChangedListener>(this);
    m_project_interactor.sla_object_cache().add_listener<Biz::ISLAObjectCacheChangedListener>(this);

    // The tool is where the support settings are changed, so it opens with them open (M2.17d4).
    m_dialog->set_settings_expanded(true);

    const Biz::Scene::ObjectSelection& selection =
        m_project_interactor.scene_interactor().object_selection();
    this->on_scene_selection_changed(m_project_interactor.selected_project_id(), selection);

    // A double click on a drawn support opened the tool on that model with that support selected, so
    // its "Selected supports" group shows what it carries right away (M2.35).
    this->open_on_picked_point();
}

void SlaSupportPointsGizmo::on_project_activated(size_t new_project_id)
{
    on_activated();
}

void SlaSupportPointsGizmo::on_project_deactivated(size_t old_project_id)
{
    on_deactivated();
}

void SlaSupportPointsGizmo::on_deactivated()
{
    m_gizmo_active = false;
    // A generation that is still running is cancelled, and its result is dropped with the job
    // (on_worker_job_completed asks for the counter, which cancel_worker_job has moved on).
    cancel_worker_job();
    m_project_interactor.scene_interactor().remove_listener<Biz::Scene::ISceneSelectionChangedListener>(this);
    m_project_interactor.scene_interactor().remove_listener<Biz::Scene::ISceneChangedListener>(this);
    m_project_interactor.sla_object_cache().remove_listener<Biz::ISLAObjectCacheChangedListener>(this);

    // Leaving the tool applies the points a generation produced, so the model keeps them (M2.31).
    // Without them the model falls back onto the plate, the preview service has no tree to draw and
    // the slice finds no points.
    this->apply_pending_points_on_leaving();

    // The lift the tool took for its object is given back on the way out, so a model with no support
    // points falls onto the plate again (M2.33). A model with points has the lift of the M2.21
    // support preview by now, which stays.
    this->release_tool_lift();

    // Cancel auto-support all queue
    if (!m_auto_support_queue.empty()) {
        m_auto_support_queue.clear();
        m_auto_support_keep_existing.reset();
    }

    if (m_edit_state.has_value()) {
        // end_editing, not discard_edited_points: the edits of the session are on the model already
        // (commit_edited_points_live writes them after every edit), so they stay. Putting the points
        // the session started from back would throw away work the user did, with no way to get it
        // back but the undo.
        end_editing();
    }

    m_paintable_volumes.clear();
    // The tool is on no object any more, so there is nothing to rebuild the volumes of (M2.33).
    m_selected_element = Domain::ElementRef{};
    m_applied_lift     = 0.;

    // Deactivate clipping plane presenter
    m_clipping_plane_presenter.deactivate();

    // Clear point visuals
    clear_point_visuals();
    m_hovered_point_idx.reset();
    // A support a double click found while the tool was closed, which the tool was never opened on,
    // is not a support to select now (M2.35).
    m_pending_open_pick.reset();

    DialogSyncGuard guard(*this);
    m_dialog->set_generate_enabled(false);
    m_dialog->set_apply_enabled(false);
    m_dialog->set_auto_support_all_enabled(false);
    m_dialog->set_point_count(0);
    // The edit session is gone, so there is no selection and no "Selected supports" group (M2.33).
    m_dialog->set_selected_support_values(SlaSupportSelectionView{});
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

    // The lift the scene draws the model with, which is the one the raycast has to use: on a model
    // with no support points the scene draws it on the plate and every click that tested a copy of it
    // 5 mm higher missed (M2.33).
    const double elevation = applied_lift();

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

        m_paintable_volumes.push_back({
            *model_object,
            *model_instance,
            *model_volume,
            *scene_mesh,
            scene_mesh->aabb_mesh(),
            sla_support_points_drawing_trafo(model_instance->get_matrix(), elevation) * model_volume->get_matrix(),
            model_instance->get_matrix_no_offset() * model_volume->get_matrix_no_offset()
        });
    }

    // The volumes above are the ones every raycast of the tool uses, so what they carry is the lift
    // the scene drew the model with (M2.33).
    m_applied_lift = elevation;
}

void SlaSupportPointsGizmo::on_scene_selection_changed(
    Domain::SelectionId project_id,
    const Biz::Scene::ObjectSelection& selection
)
{
    DialogSyncGuard guard(*this);

    // Read before the pending points are applied: they belong to the project the tool was on, and
    // this call is what brings the tool onto the project of this selection (M2.31).
    m_project_id = project_id;

    cancel_worker_job();

    // Selecting another object is the other way the tool is left with something pending, so the
    // points a generation produced are applied on the object they were made for, before the tool
    // moves on to the new one (M2.31).
    this->apply_pending_points_on_leaving();

    // Cancel auto-support all queue on selection change
    if (!m_auto_support_queue.empty()) {
        m_auto_support_queue.clear();
        m_auto_support_keep_existing.reset();
    }

    if (m_edit_state.has_value()) {
        // The edits are on the model already, so the new object starts from them (M2.31).
        end_editing();
    }
    // The selection of the session is gone, so the "Selected supports" group has nothing to show
    // on the object the tool moves to (M2.33).
    this->update_selected_support_values();
    m_hovered_point_idx.reset();

    // Whatever lift the tool took belongs to the object it was on, and it gives it back before the
    // tool moves on: a model with no support points falls onto the plate again, and one the M2.21
    // support preview lifts keeps the service's own lift (M2.33).
    this->release_tool_lift();

    if (!enabled() || selection.elements.empty()) {
        m_dialog->set_generate_enabled(false);
        m_dialog->set_apply_enabled(false);
        m_dialog->set_auto_support_all_enabled(false);
        m_dialog->set_remove_all_points_enabled(false);
        m_dialog->set_point_count(0);
        return;
    }

    const Domain::ElementRef& element = selection.elements.front();
    if (element.volume_id != 0) {
        m_dialog->set_generate_enabled(false);
        m_dialog->set_apply_enabled(false);
        m_dialog->set_auto_support_all_enabled(false);
        m_dialog->set_remove_all_points_enabled(false);
        m_dialog->set_point_count(0);
        return;
    }

    const Domain::Project& project = m_project_interactor.project(project_id);
    const Domain::ModelObject* model_object = project.find_object_by_id(element.object_id);
    if (!model_object) {
        m_dialog->set_generate_enabled(false);
        m_dialog->set_apply_enabled(false);
        m_dialog->set_auto_support_all_enabled(false);
        m_dialog->set_remove_all_points_enabled(false);
        m_dialog->set_point_count(0);
        return;
    }

    m_selected_object_id = model_object->id();
    m_selected_instance_id = element.instance_id;
    m_selected_element = element;

    const Domain::ModelInstance* instance = project.find_instance_by_id(element.object_id, element.instance_id);
    if (!instance) {
        m_dialog->set_generate_enabled(false);
        m_dialog->set_apply_enabled(false);
        m_dialog->set_auto_support_all_enabled(false);
        m_dialog->set_remove_all_points_enabled(false);
        m_dialog->set_point_count(0);
        return;
    }

    const Domain::BedRef bed_ref = instance->get_last_bed();
    if (project.find_bed_instance_by_id(bed_ref.instance_id) == nullptr) {
        m_dialog->set_generate_enabled(false);
        m_dialog->set_apply_enabled(false);
        m_dialog->set_auto_support_all_enabled(false);
        m_dialog->set_remove_all_points_enabled(false);
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
    // "Remove all points" is on while there are points to remove (M2.32).
    m_dialog->set_remove_all_points_enabled(existing_count > 0);

    // The values the "New supports" group shows are the ones of this object, and they are filled into
    // the group when the edit session opens (begin_editing), which is also where the object settings
    // behind the old head diameter row are read (M2.33).

    // Auto support all is available when we have a valid selection
    m_dialog->set_auto_support_all_enabled(true);

    // The model is raised by its support elevation while the tool is open, so a click lands on the
    // surface as it is drawn and the point glyphs sit on it (M2.33). From here on the tool works with
    // the lift the scene actually applies.
    this->refresh_tool_lift();

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
        applied_lift(),
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

void SlaSupportPointsGizmo::on_model_reloaded(Domain::SelectionId project_id)
{
    // An undo replaces the whole model, and with it every volume the tool raycasts on, so the list
    // of them is rebuilt from the model that is there now. (M0.15)
    if (project_id != m_project_id) {
        return;
    }
    this->collect_paintable_volumes(m_project_id, m_selected_element);
    // The undo also brought back the support points the model had, which the edit session takes,
    // or the next edit would write the undone points back (M2.39d).
    this->reload_edit_points_from_model();
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
        // the geometry and the preset size of its own model (M2.16c, M2.24, M2.37).
        Domain::Project& project = m_project_interactor.selected_project();
        this->fill_generated_point_geometry(
            *support_points,
            project.find_object_by_id(m_selected_object_id.id),
            project.find_instance_by_id(m_selected_object_id.id, m_selected_instance_id)
        );
        // The generated points go on the model right away, in one undo step, and the edit session
        // takes them from there (M2.39d). Kept to the tool until Apply, they had no markers, could not
        // be clicked, selected or edited, and the next edit of the session wrote its older list back
        // over them. The session is begun first, so Discard still returns to the points the model
        // had before the generation.
        if (!m_edit_state.has_value()) {
            this->begin_editing();
        }
        m_generated_support_points = *support_points;
        m_has_generated_points = true;
        this->apply_generated_points();

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

    // The points are the tool's own no longer: they are written on the model below, so a second
    // Apply, a second Generate and leaving the tool all find nothing to write twice.
    m_has_generated_points = false;
    m_generated_support_points.reset();

    m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::SlaSupportPointsApply);

    const Domain::ElementRef object_ref{m_selected_object_id.id, m_selected_instance_id};
    m_project_interactor.scene_interactor().modify_sla_support_points(object_ref, [&](Domain::ModelObject& mo) {
        apply_generated_support_points(mo, std::move(domain_points));
    });

    // The model has points now, so the M2.21 support preview draws its tree and lifts the model, and
    // the lift it uses is the one the tool works with from here on (M2.33).
    this->refresh_tool_lift();

    DialogSyncGuard guard(*this);
    m_dialog->set_apply_enabled(false);
    m_dialog->set_point_count(model_object->sla_support_points.size());
    m_dialog->set_remove_all_points_enabled(!model_object->sla_support_points.empty());

    // The supports just written are the ones the tool shows, picks and edits (M2.39d).
    this->reload_edit_points_from_model();
}

// The points a generation produced are the tool's own until they are written on the model, so every
// path that leaves the tool goes through here (M2.31): Apply is the explicit one, and this is what
// the others end up calling. The model they go on is the one they were made for, which the tool
// still knows while it is being left. Discard has cleared them by now, so it writes nothing.
void SlaSupportPointsGizmo::apply_pending_points_on_leaving()
{
    // The write needs the project the tool was working on to be the selected one, which is where
    // SceneInteractor::modify_sla_support_points puts it. Once the selection has moved on to another
    // project the tool's object is not there any more, so the points are dropped instead: there is
    // no model to put them on.
    const bool on_the_selected_project = m_project_id != Domain::INVALID_ID
        && m_project_id == m_project_interactor.selected_project_id();

    if (on_the_selected_project
        && apply_generated_points_on_leaving(m_has_generated_points, /* discard_requested */ false)) {
        this->apply_generated_points();
    }

    m_has_generated_points = false;
    m_generated_support_points.reset();
}

void SlaSupportPointsGizmo::discard_generated_points()
{
    DialogSyncGuard guard(*this);

    // Discard is the one way to throw the generated points away, so it clears them before it closes
    // the tool: on_deactivated then finds nothing pending and writes nothing (M2.31).
    m_has_generated_points = false;
    m_generated_support_points.reset();
    // A generation writes its points on the model at once since M2.39d, so throwing them away means
    // putting back the points the model had when the session began, which is what the session
    // remembers for this.
    if (m_edit_state.has_value()) {
        this->discard_edited_points();
    }
    m_dialog->set_apply_enabled(false);
    m_dialog->set_generate_enabled(true);
    m_dialog->set_auto_support_all_enabled(true);

    if (m_gizmo_controller) {
        m_gizmo_controller->deactivate_current_tool();
    }
}

// The "Remove all points" of the tool (M2.32). It asks first, the way the same action of the Preview
// sidebar and of the object context menu does, and only an answer of yes removes anything.
void SlaSupportPointsGizmo::remove_all_points()
{
    if (!m_selected_object_id.valid()) {
        return;
    }

    Domain::Project& project = m_project_interactor.selected_project();
    Domain::ModelObject* model_object = project.find_object_by_id(m_selected_object_id.id);
    if (!model_object) {
        return;
    }

    const SlaSupportPointsClearPlan plan = sla_support_points_clear_plan({ model_object });
    if (plan.empty()) {
        return;
    }

    // The answer comes back later, and the user may have selected another model in the meantime. The
    // plan names the model it was made for, so the answer only removes that one.
    const ObjectID asked_for_object_id = m_selected_object_id;
    AppServices::instance().dialog_manager().show_yesno_dialog(
        _u8L("Clear support points"),
        sla_support_points_clear_question(plan),
        [this, asked_for_object_id](bool answer)
        {
            if (!answer || m_selected_object_id != asked_for_object_id) {
                return;
            }
            this->remove_all_points_now();
        }
    );
}

void SlaSupportPointsGizmo::remove_all_points_now()
{
    if (!m_selected_object_id.valid()) {
        return;
    }

    Domain::Project& project = m_project_interactor.selected_project();
    Domain::ModelObject* model_object = project.find_object_by_id(m_selected_object_id.id);
    if (!model_object || model_object->sla_support_points.empty()) {
        return;
    }

    const SlaSupportPointsClearPlan plan = sla_support_points_clear_plan({ model_object });
    clear_sla_support_points(m_project_interactor, plan);

    // The model has no points now, so the M2.21 support preview dropped its tree and its lift, and
    // the tool raises the model again for as long as it is open (M2.33).
    this->refresh_tool_lift();

    DialogSyncGuard guard(*this);

    // Points that are only waiting to be applied belong to the points that are gone now, so they go
    // with them instead of landing on the cleared model.
    m_has_generated_points = false;
    m_generated_support_points.reset();
    end_editing();

    // What Discard would bring back is the state the model is in now, which carries no points.
    m_points_before_edit.clear();
    m_status_before_edit = PointsStatus::NoPoints;

    m_dialog->set_apply_enabled(false);
    m_dialog->set_point_count(0);
    m_dialog->set_remove_all_points_enabled(false);
    // The session is gone with the points, so the "Selected supports" group goes too (M2.33).
    this->update_selected_support_values();
    update_point_visuals();
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

    // One snapshot for the whole run: the button (or the "Auto support selected" of the Preview
    // panel) is one user action, and the points of every model of the queue are written from here
    // on. Taking it here, before the first of them is touched, is also what keeps the "replace the
    // existing supports" branch undoable: the clearing it does happens after this snapshot and so is
    // part of what undo brings back (M2.6b).
    m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::SlaSupportPointsApply);

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
            // The snapshot of the run was already taken in auto_support(), before the first model of
            // the queue was touched, so a run over several models is one undo step.

            // Find a printable instance on a bed for this object to get instance_id
            Domain::SelectionId instance_id     = 0;
            const Domain::ModelInstance* placed = nullptr;
            for (const Domain::ModelInstance* inst : model_object->instances) {
                if (!inst || !inst->is_printable()) {
                    continue;
                }
                const Domain::BedRef bed_ref = inst->get_last_bed();
                if (project.find_bed_instance_by_id(bed_ref.instance_id) != nullptr) {
                    instance_id = inst->id().id;
                    placed      = inst;
                    break;
                }
            }

            // A generated point takes the geometry and the preset size of its own model, like a
            // point placed by hand (M2.16c, M2.24, M2.37).
            this->fill_generated_point_geometry(*support_points, model_object, placed);

            if (m_selected_object_id == obj_id) {
                DialogSyncGuard guard(*this);
                m_dialog->set_point_count(support_points->size());
            }

            if (instance_id != 0) {
                const Domain::ElementRef object_ref{obj_id.id, instance_id};
                m_project_interactor.scene_interactor().modify_sla_support_points(object_ref, [&](Domain::ModelObject& mo) {
                    apply_generated_support_points(mo, std::move(*support_points));
                });
            } else {
                // Fallback: no valid instance found, just update without notification
                apply_generated_support_points(*model_object, std::move(*support_points));
            }

            // The model of the tool has its points now, so the M2.21 support preview lifts it and
            // the tool follows that lift (M2.33).
            this->refresh_tool_lift();

            // The model the tool is open on got new supports from outside its edit session, which
            // takes them, so they can be clicked and edited (M2.39d).
            if (m_selected_object_id == obj_id) {
                this->reload_edit_points_from_model();
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

    // The values of the "New supports" group, i.e. what a clicked point takes (M2.33): the stem and
    // the base sizes of the object settings, each of them followed from the global settings, and the
    // per-point tip diameter, tip shape, tip length, knot, stem cross-section, stem taper and foot
    // shape (M2.16c, M2.24, M2.23b).
    double pillar_diameter = 0.8;
    auto pillar_result = model_object->object_settings_sla.find("support_pillar_diameter");
    if (pillar_result.item) {
        pillar_diameter = pillar_result.item->get<double>();
    }
    m_edit_state->editing.pillar_diameter_mm = pillar_diameter;
    m_edit_state->editing.pillar_diameter_use_global = true;

    double base_diameter = 2.0;
    auto base_dia_result = model_object->object_settings_sla.find("support_base_diameter");
    if (base_dia_result.item) {
        base_diameter = base_dia_result.item->get<double>();
    }
    m_edit_state->editing.base_diameter_mm = base_diameter;
    m_edit_state->editing.base_diameter_use_global = true;

    double base_height = 1.0;
    auto base_ht_result = model_object->object_settings_sla.find("support_base_height");
    if (base_ht_result.item) {
        base_height = base_ht_result.item->get<double>();
    }
    m_edit_state->editing.base_height_mm = base_height;
    m_edit_state->editing.base_height_use_global = true;
    m_edit_state->editing.head_diameter_use_global = true;

    m_edit_state->editing.support_geometry = support_geometry_defaults(model_object);
    m_dialog->set_new_support_values(sla_new_support_values(m_edit_state->editing));
    // The "Selected supports" group has nothing to show while the session opens: no point is
    // selected yet, so it stays hidden until one is.
    m_dialog->set_selected_support_values(selection_support_view(m_edit_state->editing));

    m_dialog->set_lock_island_supports(false);

    m_dialog->set_apply_enabled(true);
    m_dialog->set_generate_enabled(true);
    m_dialog->set_auto_support_all_enabled(true);
    m_dialog->set_remove_all_points_enabled(!model_object->sla_support_points.empty());

    update_point_visuals();
}

void SlaSupportPointsGizmo::end_editing()
{
    clear_point_visuals();
    m_hovered_point_idx.reset();
    // A value edit that was running when the tool was closed is over: the next value change is an
    // action of its own again and owes its own undo snapshot (M2.38). Without this a slider drag
    // that was interrupted would leave the next change of the reopened tool, a dropdown in
    // particular, without one.
    m_value_edit_action.end();
    m_edit_state.reset();
}

void SlaSupportPointsGizmo::commit_edited_points_live()
{
    if (!m_edit_state.has_value() || !m_selected_object_id.valid()) {
        return;
    }

    // The one write of the points the session holds, so a support added, removed, moved or changed
    // is on the ModelObject the moment the user changes it and the preview service rebuilds the
    // drawn tree for it (M2.38).
    const Domain::ElementRef object_ref{m_selected_object_id.id, m_selected_instance_id};
    commit_sla_support_point_edits(m_project_interactor, object_ref, m_edit_state->editing.points);

    // The points are the model's now, so the M2.21 support preview has built its tree for them (or
    // dropped it for a model without points), and the lift the scene draws the model with may have
    // changed with it. The raycast and the glyphs follow that lift, not the one they had (M2.33).
    this->refresh_tool_lift();
}

// The session holds its own copy of the points of the model, and every edit writes that copy back
// (commit_edited_points_live). A write from anywhere else - a generation, Auto support all, Apply, an
// undo - therefore has to reach the copy as well: without it the tool showed no markers for the new
// supports, a click on their tree found no point to select, and the next edit wrote the older list
// back over them (measured in the app: markers=0 with a 23k triangle tree drawn, M2.39d).
void SlaSupportPointsGizmo::reload_edit_points_from_model()
{
    if (!m_edit_state.has_value() || !m_selected_object_id.valid()) {
        return;
    }

    const Domain::Project& project = m_project_interactor.selected_project();
    const Domain::ModelObject* model_object = project.find_object_by_id(m_selected_object_id.id);
    if (!model_object || m_edit_state->editing.points == model_object->sla_support_points) {
        return;
    }

    // The indices of a selection, a drag and a hover name points of the list that is replaced.
    m_edit_state->editing.points = model_object->sla_support_points;
    m_edit_state->editing.clear_selection();
    m_edit_state->dragged_point_idx.reset();
    m_hovered_point_idx.reset();

    update_point_visuals();
    this->update_selected_support_values();

    DialogSyncGuard guard(*this);
    m_dialog->set_point_count(model_object->sla_support_points.size());
    m_dialog->set_remove_all_points_enabled(!model_object->sla_support_points.empty());
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

    // The lift of the model may have changed with its points, so the volumes the raycast uses
    // follow it (M2.33).
    this->refresh_tool_lift();

    m_dialog->set_point_count(model_object->sla_support_points.size());
    m_dialog->set_remove_all_points_enabled(!model_object->sla_support_points.empty());
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
        // Putting the points of the session back can change the lift of the model as well
        // (M2.33).
        this->refresh_tool_lift();
        m_dialog->set_point_count(model_object->sla_support_points.size());
    }
}

void SlaSupportPointsGizmo::take_undo_snapshot()
{
    m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::SlaSupportPointsEdit);
}

// A slider reports a new value on every frame its thumb is dragged, so a drag of N ticks arrives
// here as N value changes. They are one user action, so only the first of them takes a snapshot,
// and it takes it before it changes the model: undo then brings back the values the drag started
// from, in one step (M2.6b).
void SlaSupportPointsGizmo::take_undo_snapshot_for_value_edit()
{
    if (m_value_edit_action.take()) {
        take_undo_snapshot();
    }
}

void SlaSupportPointsGizmo::on_value_editing_started()
{
    m_value_edit_action.begin();
}

void SlaSupportPointsGizmo::on_value_editing_ended()
{
    m_value_edit_action.end();
}

// Hits are in the hit volume's local frame; sla_support_points live in the object's mesh frame. The
// lift the scene drew the model by is not part of it: it moved the mesh, not the point on it
// (M2.33).
Domain::Vec3d SlaSupportPointsGizmo::hit_to_object_pos(const VolumeHitPoint& hit) const
{
    return sla_support_points_hit_position(m_paintable_volumes[hit.volume_idx].model_volume.get_matrix(),
                                           hit.volume_hit_position);
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
    // The removed point may have been the selected one, so the "Selected supports" group has to be
    // shown with what the selection carries now and not with what it carried before (M2.38).
    this->update_selected_support_values();
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

// The transform every part of the tool works with: the instance transform with the lift the scene
// draws the object by. The glyphs, the raycast and the picking of the points and of their drawn
// tree all go through this one value, or they would test three different places (M2.33, M2.35).
Domain::Transform3d SlaSupportPointsGizmo::object_drawing_trafo() const
{
    const Domain::Project& project = m_project_interactor.selected_project();
    const Domain::ModelInstance* instance =
        project.find_instance_by_id(m_selected_object_id.id, m_selected_instance_id);
    if (!instance) {
        return Transform3d::Identity();
    }
    return sla_support_points_drawing_trafo(instance->get_matrix(), applied_lift());
}

// The markers of the points of the edit session as they are on the screen. A click picks these
// first, so a support under an overhang is reachable without looking at it from below and a click
// on a glyph that stands in front of the model hits that point (M2.35).
void SlaSupportPointsGizmo::collect_point_markers(std::vector<SlaSupportPointMarker>& out_markers) const
{
    out_markers.clear();
    if (!m_edit_state.has_value()) {
        return;
    }

    const Scene::Camera& camera = m_scene_presenter.scene().camera();
    const Vec3d          eye    = camera.position();
    const Transform3d    trafo  = this->object_drawing_trafo();

    out_markers.reserve(m_edit_state->editing.points.size());
    for (const SupportPoint& point : m_edit_state->editing.points) {
        const Vec3d   world_pos    = trafo * point.pos.cast<double>();
        const double drawn_radius =
            sla_support_point_screen_radius(camera, world_pos, point_glyph_radius_mm(point));

        SlaSupportPointMarker marker;
        marker.screen_pos      = camera.project_to_screen_space(world_pos);
        marker.drawn_radius_px = drawn_radius;
        marker.depth_mm        = (world_pos - eye).norm();
        out_markers.push_back(marker);
    }
}

// The drawn pieces of the support tree, one per support point that has one. The M2.21 support
// preview draws a tree as one merged mesh without an AABB, on purpose: the tree never steals a pick
// from the model and one click keeps selecting the object. A click that means to take a support
// still has to find the piece of the tree under the cursor, so every point contributes the segment
// its pillar runs along, from the head of the point down to the plate where the pillar stands
// (M2.35).
void SlaSupportPointsGizmo::collect_tree_parts(std::vector<SlaSupportTreePart>& out_parts, bool whole_plate) const
{
    out_parts.clear();

    const Scene::Camera& camera = m_scene_presenter.scene().camera();
    const Vec3d          eye    = camera.position();

    const auto add_object = [&camera, &eye, &out_parts](const Domain::ModelObject&     object,
                                                       const Domain::ModelInstance&  instance,
                                                       const SupportPoints&          points,
                                                       const Domain::Transform3d&    object_to_world)
    {
        for (size_t i = 0; i < points.size(); ++i) {
            const Vec3d head_pos = object_to_world * points[i].pos.cast<double>();

            SlaSupportTreePart part;
            part.object.object_id   = object.id().id;
            part.object.instance_id = instance.id().id;
            part.point_index        = i;
            part.screen_start       = camera.project_to_screen_space(head_pos);
            // A pillar stands on the plate, so the drawn piece of a point runs from its head down to
            // the ground under it. A support that ends on the model is covered by the same segment.
            part.screen_end = camera.project_to_screen_space(Vec3d{head_pos.x(), head_pos.y(), 0.});
            part.drawn_radius_px =
                sla_support_point_screen_radius(camera, head_pos, point_glyph_radius_mm(points[i]));
            part.depth_mm = (head_pos - eye).norm();
            out_parts.push_back(part);
        }
    };

    if (!whole_plate) {
        if (!m_selected_object_id.valid()) {
            return;
        }
        const Domain::Project& project = m_project_interactor.selected_project();
        const Domain::ModelObject* model_object = project.find_object_by_id(m_selected_object_id.id);
        const Domain::ModelInstance* model_instance =
            project.find_instance_by_id(m_selected_object_id.id, m_selected_instance_id);
        if (!model_object || !model_instance) {
            return;
        }
        // While the tool is open the points it shows are the edited ones, not the ones the model
        // carried when the session began.
        const SupportPoints& points = m_edit_state.has_value() ? m_edit_state->editing.points
                                                              : model_object->sla_support_points;
        add_object(*model_object, *model_instance, points, this->object_drawing_trafo());
        return;
    }

    // The double click out of the tool: any model on the plate may have the tree under the cursor,
    // so all of them that have support points are asked (M2.35).
    const Domain::SelectionId project_id = m_project_interactor.selected_project_id();
    const SlicingId           slicing_id = m_project_interactor.selected_bed_slicing_id();
    if (slicing_id.project_id != project_id) {
        return;
    }
    const Domain::Project&     project = m_project_interactor.project(project_id);
    const Domain::BedInstance* bed     = project.find_bed_instance_by_id(slicing_id.bed_instance_id);
    if (!bed) {
        return;
    }

    std::unordered_set<size_t> seen;
    for (const Domain::ModelInstance* instance : bed->model_instances) {
        if (instance == nullptr || !instance->is_printable()) {
            continue;
        }
        const Domain::ModelObject* model_object = instance->get_object();
        if (model_object == nullptr || model_object->sla_support_points.empty()) {
            continue;
        }
        if (!seen.insert(model_object->id().id).second) {
            continue;
        }
        // Only a support that is drawn on the plate can be double clicked (M2.35), and the tree of an
        // object is drawn while the support preview has one for it.
        if (!m_support_preview_service.has_preview(model_object->id())) {
            continue;
        }
        // The tree is drawn with the lift the M2.21 support preview applies to that model.
        // The instance matrix already carries the plate offset (M2.34), so bed_trafo must NOT be added.
        // Use the same drawing transform the preview uses: instance matrix once + lift.
        const Domain::Transform3d drawing = sla_support_points_drawing_trafo(
            instance->get_matrix(),
            m_scene_presenter.sla_lift(model_object->id()));
        add_object(*model_object, *instance, model_object->sla_support_points, drawing);
    }
}

// What the pointer is on: a marker of a point first, the drawn tree of a point next, and nothing
// when the pointer is on neither, which is what tells on_mouse to fall back to the surface of the
// model (M2.35).
std::optional<SlaSupportPointTarget> SlaSupportPointsGizmo::point_at(const Domain::Vec2d& cursor) const
{
    if (!m_edit_state.has_value()) {
        return std::nullopt;
    }

    std::vector<SlaSupportPointMarker> markers;
    this->collect_point_markers(markers);

    // When the real support tree mesh exists (drawn by the preview service), the tree is picked
    // by raycast_tree_mesh in on_mouse, not by the vertical segments of collect_tree_parts.
    // Use the vertical parts only when there is no drawn tree yet (M2.39a).
    std::vector<SlaSupportTreePart> parts;
    const bool has_real_tree = m_selected_object_id.valid()
        && m_support_preview_service.support_tree_mesh(m_selected_object_id) != nullptr;
    if (!has_real_tree) {
        this->collect_tree_parts(parts, /* whole_plate */ false);
    }

    return sla_support_point_click_target(markers, parts, cursor);
}

// Raycast against the real support tree mesh (from SlaSupportPreviewService) and pick the support
// point whose head is nearest to the hit. Returns the point index and the hit position in world
// coordinates, or nullopt if no tree mesh is available or the ray misses it.
std::optional<std::pair<size_t, Domain::Vec3d>> SlaSupportPointsGizmo::raycast_tree_mesh(const Domain::Vec2d& cursor) const
{
    if (!m_edit_state.has_value() || !m_selected_object_id.valid()) {
        return std::nullopt;
    }

    // Get the tree mesh from the preview service
    const auto* tree_mesh = m_support_preview_service.support_tree_mesh(m_selected_object_id);
    if (!tree_mesh || tree_mesh->triangles().indices.empty()) {
        return std::nullopt;
    }

    // Use the AABBMesh owned by the Scene::TriangleMesh
    const Slic3r::AABBMesh& tree_aabb = tree_mesh->aabb_mesh();

    // Get the camera ray (same as raycast_mouse uses)
    const Scene::Camera& camera = m_scene_presenter.scene().camera();
    const Scene::Ray ray = camera.ray_at(cursor.x(), cursor.y());

    // The tree mesh is in world coordinates of the print pose (object_to_world = instance_matrix,
    // without the node_trafo lift). The preview draws it with only the node_trafo (lift translation).
    // Use the same transform the preview uses: sla_support_tree_placement(instance_matrix, lift).node_trafo
    // (M2.34, M2.39c). This is lift translation only, NOT the instance matrix again.
    const Domain::Project& project = m_project_interactor.selected_project();
    const Domain::ModelInstance* instance =
        project.find_instance_by_id(m_selected_object_id.id, m_selected_instance_id);
    if (!instance) {
        return std::nullopt;
    }
    const Domain::Transform3d tree_trafo =
        sla_support_tree_mesh_transform(instance->get_matrix(), applied_lift());

    // Get clipping plane for raycasting
    std::optional<Biz::ClippingPlane> clipping_plane_opt;
    if (m_clipping_plane_presenter.clipper().get_position() != 0.) {
        clipping_plane_opt = m_clipping_plane_presenter.clipper().get_clipping_plane();
    }

    // Raycast against the tree mesh. A support tree is many overlapping solids whose tips sink into
    // the model, so a ray through it crosses its surface an odd number of times as often as not: the
    // even-hit test of a closed mesh would throw most real hits away (measured in the app, M2.39c).
    const auto hit_result = Biz::Utils::MeshRaycaster::unproject_on_mesh(
        tree_aabb,
        ray,
        tree_trafo,
        clipping_plane_opt,
        false // require_even_number_of_hits
    );

    if (!hit_result.has_value()) {
        return std::nullopt;
    }

    // unproject_on_mesh answers in the frame of the mesh; the heads below are in world coordinates.
    const Domain::Vec3d hit_world = tree_trafo * hit_result->position;

    // Compute the head positions of all points in world coordinates (through the drawing transform:
    // instance_matrix once + lift, no bed_trafo). This is sla_support_points_drawing_trafo.
    std::vector<Domain::Vec3d> point_heads_world;
    point_heads_world.reserve(m_edit_state->editing.points.size());
    const Domain::Transform3d drawing_trafo = sla_support_points_drawing_trafo(instance->get_matrix(), applied_lift());
    for (const auto& point : m_edit_state->editing.points) {
        point_heads_world.push_back(drawing_trafo * point.pos.cast<double>());
    }

    // Pick the support point whose head is nearest to the hit, preferring heads above the hit
    const auto picked_idx = sla_support_point_pick_from_tree_hit(point_heads_world, hit_world);
    if (!picked_idx.has_value()) {
        return std::nullopt;
    }

    return std::make_pair(*picked_idx, hit_world);
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

    // The volumes the raycast uses are the ones of the lift the scene draws the model with, brought
    // in step with it here: a click has to land on the surface that is on the screen (M2.33). The lift
    // itself was taken on activation, on the selection change and after every edit of a point; this
    // is the cheap half of it, which a mouse move can do without asking the configuration again.
    this->sync_paintable_lift();

    if (!m_edit_state.has_value()) {
        begin_editing();
    }

    const std::optional<VolumeHitPoint> hit_opt = raycast_mouse(mouse_position);
    const bool has_hit = hit_opt.has_value();

    // What the pointer is on: a marker of a point, the drawn tree of a point, or nothing (M2.35).
    // This is what the hover, the Shift toggle, the Ctrl and right button removal and the start of a
    // drag all use, so every one of them picks the same point of the same click. Nothing while a
    // point is being dragged or a rectangle is being drawn: those are the pointer's own states.
    const bool picking = !m_edit_state->dragged_point_idx.has_value() && !m_edit_state->rect_select_active;
    const std::optional<SlaSupportPointTarget> point_under_cursor =
        picking ? this->point_at(mouse_position) : std::nullopt;

    // If no marker or drawn tree part was hit, try the real support tree mesh (M2.39a).
    std::optional<std::pair<size_t, Domain::Vec3d>> tree_hit;
    std::optional<double> tree_hit_distance_mm;
    std::optional<double> model_hit_distance_mm;
    if (picking && !point_under_cursor.has_value()) {
        tree_hit = this->raycast_tree_mesh(mouse_position);
        if (tree_hit.has_value()) {
            tree_hit_distance_mm = (tree_hit->second - m_scene_presenter.scene().camera().position()).norm();
        }
        if (has_hit) {
            const Domain::Vec3d model_hit_world =
                m_paintable_volumes[hit_opt->volume_idx].world_trafo * hit_opt->volume_hit_position;
            model_hit_distance_mm = (model_hit_world - m_scene_presenter.scene().camera().position()).norm();
        }
    }

    // Decide whether the tree hit wins over the model hit (M2.39a).
    // Tree wins only when model was not hit, or tree is nearer by more than epsilon (0.01 mm).
    const bool tree_wins = sla_support_click_on_tree(tree_hit_distance_mm, model_hit_distance_mm);

    // Track hovered point (when not dragging or rectangle selecting)
    m_hovered_point_idx.reset();
    if (point_under_cursor.has_value()) {
        m_hovered_point_idx = point_under_cursor->index;
    } else if (tree_hit.has_value() && tree_wins) {
        m_hovered_point_idx = tree_hit->first;
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

    // A click: a button going down. What it asks for is one question with no camera, no scene and
    // no gizmo in it (M2.38): select a support and change it, remove one, or add one on the drawn
    // model. The pick of M2.35 and the raycast of M2.33 are what it is asked about, and the answer
    // is acted on here.
    if ((is_left_button_event || is_right_button_event) && mouse_event.type() == MouseEvent::Type::ButtonDown) {
        // [SupportPick] Log detailed ButtonDown info for support picking diagnostics
        {
            // Collect point markers for logging
            std::vector<SlaSupportPointMarker> markers;
            collect_point_markers(markers);
            size_t marker_count = markers.size();
            double nearest_marker_screen_dist = -1.0;
            double nearest_marker_drawn_radius = -1.0;
            if (!markers.empty()) {
                double min_dist = std::numeric_limits<double>::max();
                for (const auto& m : markers) {
                    const double dist = (m.screen_pos - mouse_position).norm();
                    if (dist < min_dist) {
                        min_dist = dist;
                        nearest_marker_screen_dist = dist;
                        nearest_marker_drawn_radius = m.drawn_radius_px;
                    }
                }
            }

            // Tree mesh info
            bool tree_mesh_is_null = true;
            size_t tree_triangle_count = 0;
            Domain::Vec3d tree_bbox_min{0,0,0}, tree_bbox_max{0,0,0};
            if (m_selected_object_id.valid()) {
                const auto* tree_mesh = m_support_preview_service.support_tree_mesh(m_selected_object_id);
                if (tree_mesh && !tree_mesh->triangles().indices.empty()) {
                    tree_mesh_is_null = false;
                    tree_triangle_count = tree_mesh->triangles().indices.size() / 3;
                    // Compute transformed bbox using the same transform as raycast_tree_mesh
                    const Domain::Project& project = m_project_interactor.selected_project();
                    const Domain::ModelInstance* instance = project.find_instance_by_id(m_selected_object_id.id, m_selected_instance_id);
                    if (instance) {
                        const Domain::Transform3d tree_trafo = sla_support_tree_mesh_transform(instance->get_matrix(), applied_lift());
                        const auto& vertices = tree_mesh->triangles().vertices;
                        bool first = true;
                        for (const auto& vertex : vertices) {
                            const Domain::Vec3d v_world = tree_trafo * vertex.cast<double>();
                            if (first) {
                                tree_bbox_min = tree_bbox_max = v_world;
                                first = false;
                            } else {
                                tree_bbox_min = tree_bbox_min.cwiseMin(v_world);
                                tree_bbox_max = tree_bbox_max.cwiseMax(v_world);
                            }
                        }
                    }
                }
            }

            // Camera ray
            const Scene::Camera& camera = m_scene_presenter.scene().camera();
            const Scene::Ray ray = camera.ray_at(mouse_position.x(), mouse_position.y());

            // First support point head world position and applied_lift
            Domain::Vec3d first_head_world{0,0,0};
            bool has_first_point = false;
            if (m_edit_state.has_value() && !m_edit_state->editing.points.empty()) {
                first_head_world = object_drawing_trafo() * m_edit_state->editing.points[0].pos.cast<double>();
                has_first_point = true;
            }

            SPDLOG_INFO("[SupportPick] SlaSupportPointsGizmo::on_mouse ButtonDown cursor=({}, {}) paintable_volumes={} has_hit={} hit_world=({}, {}, {}) model_hit_dist_mm={} markers={} nearest_marker_dist_px={} nearest_marker_radius_px={} point_under_cursor={} (idx={}, from_marker={}) tree_mesh_null={} tree_triangles={} tree_bbox_min=({}, {}, {}) tree_bbox_max=({}, {}, {}) ray_origin=({}, {}, {}) ray_dir=({}, {}, {}) tree_hit={} tree_hit_idx={} tree_hit_world=({}, {}, {}) tree_hit_dist_mm={} tree_wins={} click_action={} gizmo_state={} first_head_world=({}, {}, {}) applied_lift={}",
                mouse_position.x(), mouse_position.y(),
                m_paintable_volumes.size(),
                has_hit,
                has_hit ? (m_paintable_volumes[hit_opt->volume_idx].world_trafo * hit_opt->volume_hit_position).x() : 0.0,
                has_hit ? (m_paintable_volumes[hit_opt->volume_idx].world_trafo * hit_opt->volume_hit_position).y() : 0.0,
                has_hit ? (m_paintable_volumes[hit_opt->volume_idx].world_trafo * hit_opt->volume_hit_position).z() : 0.0,
                has_hit ? model_hit_distance_mm.value_or(-1.0) : -1.0,
                marker_count,
                nearest_marker_screen_dist,
                nearest_marker_drawn_radius,
                point_under_cursor.has_value(),
                point_under_cursor.has_value() ? static_cast<int>(point_under_cursor->index) : -1,
                point_under_cursor.has_value() ? (point_under_cursor->from_marker ? 1 : 0) : -1,
                tree_mesh_is_null,
                tree_triangle_count,
                tree_bbox_min.x(), tree_bbox_min.y(), tree_bbox_min.z(),
                tree_bbox_max.x(), tree_bbox_max.y(), tree_bbox_max.z(),
                ray.origin.x(), ray.origin.y(), ray.origin.z(),
                ray.direction.x(), ray.direction.y(), ray.direction.z(),
                tree_hit.has_value(),
                tree_hit.has_value() ? static_cast<int>(tree_hit->first) : -1,
                tree_hit.has_value() ? tree_hit->second.x() : 0.0,
                tree_hit.has_value() ? tree_hit->second.y() : 0.0,
                tree_hit.has_value() ? tree_hit->second.z() : 0.0,
                tree_hit_distance_mm.value_or(-1.0),
                tree_wins,
                static_cast<int>(SlaSupportClickAction::None), // placeholder, will log actual after decision
                static_cast<int>(Scene::GizmoActivationState::Inactive), // placeholder
                has_first_point ? first_head_world.x() : 0.0,
                has_first_point ? first_head_world.y() : 0.0,
                has_first_point ? first_head_world.z() : 0.0,
                applied_lift()
            );
        }

        SlaSupportClick click;
        click.button = is_left_button_event ? SlaSupportClickButton::Left : SlaSupportClickButton::Right;
        if (ctrl_down) {
            click.modifier = SlaSupportClickModifier::Ctrl;
        } else if (shift_down) {
            click.modifier = SlaSupportClickModifier::Shift;
        }

        const std::optional<Domain::Vec3d> surface_pos =
            has_hit ? std::optional<Domain::Vec3d>(hit_to_object_pos(*hit_opt)) : std::nullopt;

        // If the click hit the real support tree mesh but no marker/part, synthesize a target for it.
        // A tree hit selects the support (or removes/toggles with modifiers) but NEVER adds a point.
        // Only use the tree hit if it wins over the model (M2.39a).
        std::optional<SlaSupportPointTarget> effective_target = point_under_cursor;
        bool target_from_tree = false;
        if (!effective_target.has_value() && tree_hit.has_value() && tree_wins) {
            effective_target = SlaSupportPointTarget{tree_hit->first, false};
            target_from_tree = true;
        }

        const SlaSupportClickResult result = sla_support_click_action(
            click,
            effective_target,
            m_edit_state->editing.points,
            m_edit_state->editing.lock_island_supports,
            surface_pos
        );

        // If the click action would be AddPoint but the hit was on the tree mesh, convert to SelectPoint.
        // A click on the real tree must never become AddPoint (M2.39a).
        SlaSupportClickAction final_action = result.action;
        std::optional<size_t> final_point_index = result.point_index;
        if (target_from_tree && final_action == SlaSupportClickAction::AddPoint) {
            final_action = SlaSupportClickAction::SelectPoint;
            final_point_index = tree_hit->first;
        }

        // [SupportPick] Log the chosen action and returned state
        Scene::GizmoActivationState return_state = Scene::GizmoActivationState::Inactive;
        switch (final_action) {
        case SlaSupportClickAction::DeletePoint:
            remove_point_at_index(*final_point_index);
            return_state = Scene::GizmoActivationState::Active;
            break;

        case SlaSupportClickAction::TogglePoint:
            m_edit_state->editing.toggle_point(*final_point_index);
            update_point_visuals();
            // The selection the group shows changed, so its title and its fields follow (M2.38).
            this->update_selected_support_values();
            return_state = Scene::GizmoActivationState::Active;
            break;

        case SlaSupportClickAction::SelectPoint: {
            const size_t idx = *final_point_index;
            clear_selection();
            select_point(idx);
            update_point_visuals(); // M2.39a: ensure selection glyph colour updates
            if (result.drag_allowed && !target_from_tree) {
                m_edit_state->dragged_point_idx = idx;
                // The drag starts on the surface under the cursor when the ray hit one, and on the
                // point itself when it did not: a support under an overhang is picked by its
                // marker with the ray never reaching the model at all.
                m_edit_state->drag_start_world_pos =
                    has_hit ? m_paintable_volumes[hit_opt->volume_idx].world_trafo * hit_opt->volume_hit_position
                            : this->object_drawing_trafo() * m_edit_state->editing.points[idx].pos.cast<double>();
                m_edit_state->drag_start_mesh_pos = m_edit_state->editing.points[idx].pos.cast<double>();
            }
            return_state = Scene::GizmoActivationState::Active;
            break;
        }

        case SlaSupportClickAction::AddPoint:
            // A click on no support at all falls back to the model surface, where a new support
            // point goes (M2.35 keeps M2.33 for this, the marker and the tree only come first).
            clear_selection();
            add_point_at_mesh_pos(*result.surface_pos);
            return_state = Scene::GizmoActivationState::Active;
            break;

        case SlaSupportClickAction::RectangleSelect:
            start_rectangle_selection(mouse_position, true);
            return_state = Scene::GizmoActivationState::Probing;
            break;

        case SlaSupportClickAction::ClearSelection:
            clear_selection();
            update_point_visuals();
            // Consume the event (return Active) so the scene does not treat this as a click on empty
            // space and deselect the object. The tool stays open and the user can continue editing.
            return_state = Scene::GizmoActivationState::Active;
            break;

        case SlaSupportClickAction::Ignored:
            return_state = Scene::GizmoActivationState::Active;
            break;

        case SlaSupportClickAction::None:
            // A click that hit a support tree but picked nothing (defensive: should not happen
            // after the transform fix). Consume the event to keep the tool open.
            return_state = Scene::GizmoActivationState::Active;
            break;
        }

        SPDLOG_INFO("[SupportPick] SlaSupportPointsGizmo::on_mouse ButtonDown final_click_action={} return_state={}",
            static_cast<int>(final_action), static_cast<int>(return_state));

        return return_state;
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

// A double click on a drawn support opens the tool on the model of that support, with that support
// selected (M2.35). A single left click (not a drag) on a drawn support does the same (M2.39b).
// Outside the tool only: while the tool is open the click belongs to the tool (on_mouse).
bool SlaSupportPointsGizmo::allows_activation_by_double_click(const Scene::GizmoEventContext& ctx)
{
    // [SupportPick] Log entry to allows_activation_by_double_click
    const Platform::MouseEvent& mouse_event = ctx.mouse_event();
    const bool is_double_click = mouse_event.type() == Platform::MouseEvent::Type::DoubleClick;
    const bool is_single_click = mouse_event.type() == Platform::MouseEvent::Type::ButtonUp &&
                                 mouse_event.button() == Platform::MouseButton::Left;
    const bool is_sla_active = App::is_sla_active(m_project_interactor);
    SPDLOG_INFO("[SupportPick] SlaSupportPointsGizmo::allows_activation_by_double_click called event_type={} is_double_click={} is_single_click={} is_sla_active={} m_gizmo_active={}",
        static_cast<int>(mouse_event.type()), is_double_click, is_single_click, is_sla_active, m_gizmo_active);

    // While the tool is open the click belongs to the tool, which picks the point of the drawn tree
    // on its own (on_mouse), and a re-activation would not call on_activated anyway.
    if (m_gizmo_active || !is_sla_active) {
        SPDLOG_INFO("[SupportPick] SlaSupportPointsGizmo::allows_activation_by_double_click early return: m_gizmo_active={} is_sla_active={}", m_gizmo_active, is_sla_active);
        return false;
    }

    if (!is_double_click && !is_single_click) {
        SPDLOG_INFO("[SupportPick] SlaSupportPointsGizmo::allows_activation_by_double_click early return: not double or single click");
        return false;
    }

    const Domain::Vec2d cursor = Domain::Vec2f(ctx.screen_mouse_x(), ctx.screen_mouse_y()).cast<double>();

    // For double-click, use the vertical segments (collect_tree_parts) as before (M2.35).
    // For single-click, raycast the REAL drawn support tree meshes (M2.39b).
    std::optional<SlaSupportTreePart> picked;
    std::optional<double> tree_hit_distance_mm;

    if (is_double_click) {
        std::vector<SlaSupportTreePart> parts;
        this->collect_tree_parts(parts, /* whole_plate */ true);
        picked = sla_support_tree_part_at(parts, cursor);
        SPDLOG_INFO("[SupportPick] SlaSupportPointsGizmo::allows_activation_by_double_click double-click path: collect_tree_parts returned {} parts, picked={}", parts.size(), picked.has_value());
    } else {
        // Single click: raycast real tree meshes of all objects with preview.
        // Also raycast models to reject tree hits behind the model surface.
        picked = raycast_all_tree_meshes(ctx, cursor, tree_hit_distance_mm);
        SPDLOG_INFO("[SupportPick] SlaSupportPointsGizmo::allows_activation_by_double_click single-click path: raycast_all_tree_meshes returned picked={} tree_hit_distance_mm={}",
            picked.has_value(), tree_hit_distance_mm.value_or(-1.0));
    }

    if (!picked.has_value()) {
        SPDLOG_INFO("[SupportPick] SlaSupportPointsGizmo::allows_activation_by_double_click no pick, returning false");
        return false;
    }

    // The object is selected first, since the tool works on the selected object, and the point is
    // remembered until the tool is open, where it is selected (open_on_picked_point).
    m_project_interactor.scene_interactor().set_object_selection({
        Biz::Scene::SelectionMode::Instance,
        {Domain::ElementRef{picked->object.object_id, picked->object.instance_id}}
    });
    m_pending_open_pick = *picked;

    SPDLOG_INFO("[SupportPick] SlaSupportPointsGizmo::allows_activation_by_double_click returning true, picked object_id={} instance_id={} point_index={}",
        picked->object.object_id, picked->object.instance_id, picked->point_index);

    return true;
}

// Raycast all support tree meshes on the plate (objects with preview) and pick the nearest hit.
// Also raycasts the model surfaces to reject tree hits that are behind the model (M2.39b).
// Returns the picked tree part (with object/instance/point index) and the tree hit distance from camera.
std::optional<SlaSupportTreePart> SlaSupportPointsGizmo::raycast_all_tree_meshes(
    const Scene::GizmoEventContext& ctx,
    const Domain::Vec2d& cursor,
    std::optional<double>& out_tree_hit_distance_mm) const
{
    out_tree_hit_distance_mm.reset();

    const Scene::Camera& camera = m_scene_presenter.scene().camera();
    const Scene::Ray ray = camera.ray_at(cursor.x(), cursor.y());
    const Domain::Vec3d camera_pos = camera.position();

    const Domain::SelectionId project_id = m_project_interactor.selected_project_id();
    const Domain::SlicingId slicing_id = m_project_interactor.selected_bed_slicing_id();
    if (slicing_id.project_id != project_id) {
        SPDLOG_INFO("[SupportPick] SlaSupportPointsGizmo::raycast_all_tree_meshes early return: slicing_id project mismatch");
        return std::nullopt;
    }
    const Domain::Project& project = m_project_interactor.project(project_id);
    const Domain::BedInstance* bed = project.find_bed_instance_by_id(slicing_id.bed_instance_id);
    if (!bed) {
        SPDLOG_INFO("[SupportPick] SlaSupportPointsGizmo::raycast_all_tree_meshes early return: no bed");
        return std::nullopt;
    }

    std::unordered_set<size_t> seen_objects;

    std::optional<SlaSupportTreePart> best_picked;
    double best_tree_depth_mm = std::numeric_limits<double>::max();
    double best_model_depth_mm = std::numeric_limits<double>::max();

    // For each printable instance on the bed that has support points and a preview
    for (const Domain::ModelInstance* instance : bed->model_instances) {
        if (instance == nullptr || !instance->is_printable()) {
            continue;
        }
        const Domain::ModelObject* model_object = instance->get_object();
        if (model_object == nullptr || model_object->sla_support_points.empty()) {
            continue;
        }
        if (!seen_objects.insert(model_object->id().id).second) {
            continue;
        }
        if (!m_support_preview_service.has_preview(model_object->id())) {
            continue;
        }

        // Get the tree mesh and raycast it
        const auto* tree_mesh = m_support_preview_service.support_tree_mesh(model_object->id());
        bool tree_mesh_is_null = !tree_mesh || tree_mesh->triangles().indices.empty();
        size_t tree_triangle_count = 0;
        Domain::Vec3d tree_bbox_min{0,0,0}, tree_bbox_max{0,0,0};
        
        if (!tree_mesh_is_null) {
            tree_triangle_count = tree_mesh->triangles().indices.size() / 3;
            // Compute transformed bbox
            const double lift = m_scene_presenter.sla_lift(model_object->id());
            const Domain::Transform3d tree_trafo = sla_support_tree_mesh_transform(instance->get_matrix(), lift);
            const auto& vertices = tree_mesh->triangles().vertices;
            bool first = true;
            for (const auto& vertex : vertices) {
                const Domain::Vec3d v_world = tree_trafo * vertex.cast<double>();
                if (first) {
                    tree_bbox_min = tree_bbox_max = v_world;
                    first = false;
                } else {
                    tree_bbox_min = tree_bbox_min.cwiseMin(v_world);
                    tree_bbox_max = tree_bbox_max.cwiseMax(v_world);
                }
            }
        }

        SPDLOG_INFO("[SupportPick] SlaSupportPointsGizmo::raycast_all_tree_meshes object_id={} instance_id={} has_preview={} tree_mesh_null={} tree_triangles={} tree_bbox_min=({}, {}, {}) tree_bbox_max=({}, {}, {})",
            model_object->id().id, instance->id().id, true, tree_mesh_is_null, tree_triangle_count,
            tree_bbox_min.x(), tree_bbox_min.y(), tree_bbox_min.z(),
            tree_bbox_max.x(), tree_bbox_max.y(), tree_bbox_max.z());

        if (tree_mesh_is_null) {
            continue;
        }

        // Use the AABBMesh owned by the Scene::TriangleMesh
        const Slic3r::AABBMesh& tree_aabb = tree_mesh->aabb_mesh();

        // The tree mesh is in world coordinates (object_to_world = instance_matrix, no lift).
        // The preview draws it with only the node_trafo (lift translation). Use the same
        // transform the preview uses: sla_support_tree_placement(instance_matrix, lift).node_trafo
        // (M2.34). The instance matrix already carries the plate offset, so bed_trafo must NOT be added.
        const double lift = m_scene_presenter.sla_lift(model_object->id());
        const Domain::Transform3d tree_trafo = sla_support_tree_mesh_transform(instance->get_matrix(), lift);
        const Domain::Transform3d drawing_trafo = sla_support_points_drawing_trafo(instance->get_matrix(), lift);

        // Raycast tree mesh
        const auto tree_hit_result = Biz::Utils::MeshRaycaster::unproject_on_mesh(
            tree_aabb,
            ray,
            tree_trafo,
            std::nullopt, // no clipping plane for activation pick
            false // a support tree is not one closed mesh, see raycast_tree_mesh
        );

        if (!tree_hit_result.has_value()) {
            continue;
        }

        // unproject_on_mesh answers in the frame of the mesh.
        const Domain::Vec3d tree_hit_world = tree_trafo * tree_hit_result->position;
        const double tree_dist = (tree_hit_world - camera_pos).norm();

        // Raycast the model surface for this instance to check if model is in front.
        // Model volumes are drawn with drawing_trafo * model_volume->get_matrix() (no bed_trafo).
        double model_dist = std::numeric_limits<double>::max();
        bool model_hit = false;

        using MeshManager = PlaterScenePresenter::MeshManager;
        const MeshManager& mesh_manager = m_scene_presenter.model_triangle_mesh_manager(project_id);
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
            const Domain::Transform3d volume_trafo = drawing_trafo * model_volume->get_matrix();
            const auto model_hit_result = Biz::Utils::MeshRaycaster::unproject_on_mesh(
                scene_mesh->aabb_mesh(),
                ray,
                volume_trafo,
                std::nullopt,
                true
            );
            if (model_hit_result.has_value()) {
                const double this_model_dist = (volume_trafo * model_hit_result->position - camera_pos).norm();
                if (this_model_dist < model_dist) {
                    model_dist = this_model_dist;
                    model_hit = true;
                }
            }
        }

        // Use the pure function to decide: tree wins only if no model hit or tree is nearer by > epsilon
        const bool tree_wins = sla_support_click_on_tree(tree_dist, model_hit ? std::optional<double>(model_dist) : std::nullopt);
        if (!tree_wins) {
            SPDLOG_INFO("[SupportPick] SlaSupportPointsGizmo::raycast_all_tree_meshes object_id={} tree_dist={} model_hit={} model_dist={} tree_wins=false (model in front)",
                model_object->id().id, tree_dist, model_hit, model_hit ? model_dist : -1.0);
            continue; // Model is in front, skip this object's tree
        }

        // Pick the support point from the tree hit.
        // Point heads are at drawing_trafo * point.pos (instance matrix once + lift, no bed_trafo).
        std::vector<Domain::Vec3d> point_heads_world;
        point_heads_world.reserve(model_object->sla_support_points.size());
        for (const auto& point : model_object->sla_support_points) {
            point_heads_world.push_back(drawing_trafo * point.pos.cast<double>());
        }
        const auto picked_idx = sla_support_point_pick_from_tree_hit(point_heads_world, tree_hit_world);
        if (!picked_idx.has_value()) {
            continue;
        }

        // Build the tree part for this hit
        SlaSupportTreePart part;
        part.object.object_id   = model_object->id().id;
        part.object.instance_id = instance->id().id;
        part.point_index        = *picked_idx;
        // Screen positions for the part (head to base)
        const Domain::Vec3d head_world = point_heads_world[*picked_idx];
        const Domain::Vec3d base_world = drawing_trafo * Domain::Vec3d{head_world.x(), head_world.y(), 0.};
        part.screen_start = camera.project_to_screen_space(head_world);
        part.screen_end   = camera.project_to_screen_space(base_world);
        part.drawn_radius_px = 3.; // approximate
        part.depth_mm = tree_dist;

        // Keep the nearest tree hit across all objects
        if (tree_dist < best_tree_depth_mm) {
            best_tree_depth_mm = tree_dist;
            best_picked = part;
        }
    }

    if (best_picked.has_value()) {
        out_tree_hit_distance_mm = best_tree_depth_mm;
        SPDLOG_INFO("[SupportPick] SlaSupportPointsGizmo::raycast_all_tree_meshes best_picked object_id={} instance_id={} point_index={} tree_dist={}",
            best_picked->object.object_id, best_picked->object.instance_id, best_picked->point_index, best_tree_depth_mm);
    } else {
        SPDLOG_INFO("[SupportPick] SlaSupportPointsGizmo::raycast_all_tree_meshes no best_picked");
    }
    return best_picked;
}

// The "Selected supports" group (M2.33) shows the tip, stem and foot values of the support the user
// double clicked, so the tool opens with that support selected. A click that found a point of a
// model that is no longer the selected one (the tree moved on, or the model lost its points in
// between) opens the tool on it without selecting anything.
void SlaSupportPointsGizmo::open_on_picked_point()
{
    if (!m_pending_open_pick.has_value()) {
        return;
    }
    const SlaSupportTreePart picked = *m_pending_open_pick;
    m_pending_open_pick.reset();

    if (!m_selected_object_id.valid() || picked.object.object_id != m_selected_object_id.id
        || picked.object.instance_id != m_selected_instance_id) {
        return;
    }

    if (!m_edit_state.has_value()) {
        begin_editing();
    }
    if (!m_edit_state.has_value() || picked.point_index >= m_edit_state->editing.points.size()) {
        return;
    }

    clear_selection();
    select_point(picked.point_index);
    update_point_visuals();
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

    // Get the instance transform with the lift the scene draws the model with applied, so a glyph
    // sits on the model as it is on the screen and not on a copy of it somewhere else (M2.33).
    const Domain::Project& project = m_project_interactor.selected_project();
    const Domain::ModelInstance* instance = project.find_instance_by_id(m_selected_object_id.id, m_selected_instance_id);
    if (!instance) {
        return;
    }
    const Domain::Transform3d instance_trafo = this->object_drawing_trafo();

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
        double radius = point_glyph_radius_mm(point);

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
    this->update_selected_support_values();
}

void SlaSupportPointsGizmo::deselect_point(size_t idx)
{
    if (!m_edit_state.has_value()) {
        return;
    }
    m_edit_state->editing.deselect_point(idx);
    this->update_selected_support_values();
}

void SlaSupportPointsGizmo::select_all_points()
{
    if (!m_edit_state.has_value()) {
        return;
    }
    m_edit_state->editing.select_all_points();
    update_point_visuals();
    this->update_selected_support_values();
}

void SlaSupportPointsGizmo::clear_selection()
{
    if (!m_edit_state.has_value()) {
        return;
    }
    m_edit_state->editing.clear_selection();
    update_point_visuals();
    this->update_selected_support_values();
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
        this->update_selected_support_values();
        commit_edited_points_live();
    }
}

// One value of the "New supports" group: what a clicked point takes from now on (M2.33). No point is
// touched here, not even a selected one: that is what the other group is for.
void SlaSupportPointsGizmo::apply_new_support_setting(SlaSupportPointField field, double value)
{
    if (!m_edit_state.has_value()) {
        return;
    }
    sla_new_support_setting_changed(m_edit_state->editing, field, value);
    this->refresh_new_support_values();
}

// One value of the "Selected supports" group: it lands on the points that are selected and on
// nothing else, and what a clicked point takes is left alone (M2.33). One field at a time, so
// setting the tip shape keeps the sizes a point already has, and one undo step per edit.
void SlaSupportPointsGizmo::apply_selected_support_setting(SlaSupportPointField field, double value)
{
    if (!m_edit_state.has_value() || m_edit_state->editing.selected_point_indices.empty()) {
        return;
    }
    take_undo_snapshot_for_value_edit();
    sla_selected_support_setting_changed(m_edit_state->editing, field, value);
    // The points now carry the new value, so the fields keep showing it.
    this->update_selected_support_values();
    update_point_visuals();
    // The tree the preview shows is built from the points, so a change of a value of a point has to
    // reach the model like any other edit of a point (M2.26, M2.16c).
    commit_edited_points_live();
}

// A preset of the "New supports" group: the four values a clicked point takes from now on. The points
// that exist keep what they carry, so a preset picked while three points are selected does not
// change those three behind the user's back (M2.33).
void SlaSupportPointsGizmo::apply_new_support_preset(int preset_index)
{
    if (!m_edit_state.has_value()) {
        return;
    }
    DialogSyncGuard guard(*this);
    sla_new_support_preset_changed(m_edit_state->editing,
                                   this->get_support_preset_values(sla_support_preset_name(preset_index)));
    this->refresh_new_support_values();
    m_dialog->set_active_preset(preset_index, SlaSupportSettingsGroup::NewSupports);
}

// A preset of the "Selected supports" group: the same four values on the points that are selected,
// in one undo step, and not on what a clicked point takes.
void SlaSupportPointsGizmo::apply_selected_support_preset(int preset_index)
{
    if (!m_edit_state.has_value() || m_edit_state->editing.selected_point_indices.empty()) {
        return;
    }
    DialogSyncGuard guard(*this);
    take_undo_snapshot();
    sla_selected_support_preset_changed(m_edit_state->editing,
                                        this->get_support_preset_values(sla_support_preset_name(preset_index)));
    this->update_selected_support_values();
    update_point_visuals();
    commit_edited_points_live();
    m_dialog->set_active_preset(preset_index, SlaSupportSettingsGroup::SelectedSupports);
}

void SlaSupportPointsGizmo::refresh_new_support_values()
{
    DialogSyncGuard guard(*this);
    if (!m_edit_state.has_value()) {
        return;
    }
    m_dialog->set_new_support_values(sla_new_support_values(m_edit_state->editing));
}

// What the "Selected supports (N)" group is shown with. With no point selected the group is hidden
// by the dialog itself, so an empty selection asks for nothing.
void SlaSupportPointsGizmo::update_selected_support_values()
{
    DialogSyncGuard guard(*this);
    if (!m_edit_state.has_value()) {
        m_dialog->set_selected_support_values(SlaSupportSelectionView{});
        return;
    }
    m_dialog->set_selected_support_values(selection_support_view(m_edit_state->editing));
}

// The tip diameter, tip shape, tip length, knot, stem cross-section and stem taper a point takes, read
// off the object settings of the model the tool works on (M2.16c, M2.24, M2.23b). This is what a
// clicked point takes and what a generated point is filled with, so both start from the values the
// user configured for this model.
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

// The values of a preset: the tip class of the support rulebook (M7.8.1, R3) with the geometry
// every class has, from the print preset of the printer where it belongs
// (support_preset_{mini,light,medium,heavy,xheavy}_*, M2.18, M2.22) and from the values the config
// definitions ship for a preset that does not carry the keys.
SlaSupportPreset SlaSupportPointsGizmo::get_support_preset_values(const std::string& preset_name) const
{
    SlaSupportPreset preset = sla_support_preset(preset_name);

    const auto& config_box = m_project_interactor.preset_interactor().selected_printer_preset().print.config_box();
    const std::string prefix = "support_preset_" + preset_name + "_";

    const auto get_value = [&](const std::string& suffix, double fallback) -> double {
        const auto* item = config_box.items.find(prefix + suffix);
        return item ? item->get<double>() : fallback;
    };

    // Only these four sizes are settings, so a print preset of before M7.8.1 keeps the geometry it
    // stored and the rest of the class is the rulebook's.
    preset.geometry.tip_diameter_mm = get_value("head_diameter", preset.geometry.tip_diameter_mm);
    preset.stem_diameter_mm         = get_value("pillar_diameter", preset.stem_diameter_mm);
    preset.base_diameter_mm         = get_value("base_diameter", preset.base_diameter_mm);
    preset.base_height_mm           = get_value("base_height", preset.base_height_mm);
    return preset;
}

// Which preset the automatic placement gives the base of the model and which one it gives the
// detail, read off the selected print preset next to the preset dimensions above
// (support_auto_heavy_base and support_auto_detail_preset, M2.37). A print preset that carries
// neither key keeps the defaults of the config definitions: the heavy base on, Light for the
// detail.
SlaAutoSupportChoice SlaSupportPointsGizmo::auto_support_preset_choice() const
{
    const auto& config_box =
        m_project_interactor.preset_interactor().selected_printer_preset().print.config_box();

    SlaAutoSupportChoice choice;
    if (const auto* item = config_box.items.find("support_auto_heavy_base")) {
        choice.heavy_base = item->get<bool>();
    }
    if (const auto* item = config_box.items.find("support_auto_detail_preset")) {
        choice.detail = item->get<Domain::sla::SupportAutoDetailPreset>();
    }
    return choice;
}

// What the generator leaves open on every point it produced: the tip shape, tip length, knot, stem
// cross-section, stem taper and foot shape of the settings of this model (support_geometry_defaults,
// M2.16c / M2.24 / M2.23b), and then the sizes the automatic placement picks per point: the tip
// class of the role the point was classified with (M7.8.8, the rule of M7.8.2 in
// SlaSupportRoles.{hpp,cpp}), with the two settings of M2.37 deciding what a role is given - the
// heavy class on the island the model is glued on, the detail class everywhere else, and the
// classes of the roles in between. A point the generator did not classify keeps the band rule of
// M2.37 on its own. Both generation paths come through here, so a generated point is the same
// support whichever made it.
void SlaSupportPointsGizmo::fill_generated_point_geometry(
    Domain::SLA::SupportPoints& points,
    const Domain::ModelObject* model_object,
    const Domain::ModelInstance* instance
)
{
    const SlaSupportGeometry geometry = support_geometry_defaults(model_object);
    for (SupportPoint& point : points) {
        apply_support_geometry(point, geometry);
    }

    if (!model_object) {
        return;
    }

    // The generator returns its points in the model's own frame, and the mesh of the model part
    // volumes is what it sampled them on, so the lowest z of that mesh is the bottom of the model
    // in the very frame the points are in. The lift moves the model and its points together, so
    // nothing here depends on how high the scene draws the object.
    const double lowest_z_mm =
        Biz::Algorithms::ModelObject::raw_mesh_bounding_box(*model_object).min.z();

    // The band of the model the heavy supports cover is two layers, so the layer height the print
    // will be sliced at is what says which points are in it.
    double layer_height_mm = 0.05;
    if (const std::optional<ObjectSlaConfig> config =
            build_object_sla_config(model_object, instance);
        config.has_value())
    {
        Domain::ConfigView config_view{config->full, {config->object}};
        config_view.finalize();
        const double height_mm = Domain::sla_effective_layer_height(config_view);
        if (height_mm > 0.) {
            layer_height_mm = height_mm;
        }
    }

    const SlaAutoSupportChoice choice = this->auto_support_preset_choice();

    // All five tip classes of the rulebook as the print preset carries them, since the role of a
    // point picks its class out of them (M7.8.8) and the two classes of M2.37 are two of the five.
    SlaAutoSupportPresets presets;
    for (int index = 0; index < sla_support_preset_count; ++index) {
        presets.classes[std::size_t(index)] =
            this->get_support_preset_values(sla_support_preset_name(index));
    }
    // What the base of the model gets is the "heavy" id, which is the T0.4 mm class since M7.8.1,
    // and the detail takes the class the setting names.
    presets.base = presets.classes[std::size_t(sla_auto_heavy_base_preset_index)];
    presets.detail = presets.class_of(sla_auto_detail_preset_name(choice.detail));
    // The T0.1 class, preset button 1, is what a point in a detailed region takes (M7.8.5).
    presets.minimum = presets.classes[0];

    sla_apply_auto_support_presets(points, lowest_z_mm, layer_height_mm, presets, choice);
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
    this->update_selected_support_values();
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
    const Domain::Transform3d instance_trafo = this->object_drawing_trafo();

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
    case SupportToolAction::PresetT01:
        apply_new_support_preset(0);
        break;
    case SupportToolAction::PresetT02:
        apply_new_support_preset(1);
        break;
    case SupportToolAction::PresetT03:
        apply_new_support_preset(2);
        break;
    case SupportToolAction::PresetT04:
        apply_new_support_preset(3);
        break;
    case SupportToolAction::PresetT06:
        apply_new_support_preset(4);
        break;
    case SupportToolAction::AutoSupportSelection:
        auto_support({m_selected_object_id});
        break;
    case SupportToolAction::AutoSupportAll:
        auto_support();
        break;
    case SupportToolAction::ToggleSupportOnModel: {
        const SupportOnModel on_model =
            support_tool_toggled_on_model(m_edit_state->editing.selected_support_on_model());
        // The shortcut flips the state of the selected points, like the "Selected supports" field of
        // the same switch does (M2.26, M2.33).
        this->apply_selected_support_setting(SlaSupportPointField::SupportOnModel,
                                            sla_support_point_field_value(on_model));
        break;
    }
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
    // The support preview may have taken the lift of the model or given it back since the last frame
    // (a point added, the points of the model cleared), so the volumes the raycast uses are brought
    // in step with it before the glyphs are drawn (M2.33).
    this->sync_paintable_lift();

    // Update point visuals each frame to reflect hover/drag state
    update_point_visuals();
}

// The lift the scene draws the object of the tool with, which is the one the raycast and the point
// glyphs use (M2.33). It is what PlaterScenePresenter applies, never support_elevation() computed
// again here: the two could disagree, and then every click would test a mesh somewhere else than the
// one on the screen.
double SlaSupportPointsGizmo::applied_lift() const
{
    return m_selected_object_id.valid() ? m_scene_presenter.sla_lift(m_selected_object_id) : 0.;
}

// Asks the scene for the lift of the object the tool works on, and keeps the paintable volumes in
// step with the lift that comes back (M2.33).
void SlaSupportPointsGizmo::refresh_tool_lift()
{
    if (!m_selected_object_id.valid()) {
        return;
    }

    const SlaSupportPointsLiftDecision decision = sla_support_points_lift(
        m_gizmo_active,
        m_scene_presenter.sla_lift(m_selected_object_id),
        support_elevation(),
        m_tool_owns_lift
    );

    if (decision.action == SlaSupportPointsLiftAction::Take
        || decision.action == SlaSupportPointsLiftAction::GiveBack) {
        m_scene_presenter.set_sla_lift(m_selected_object_id, decision.lift);
    }
    m_tool_owns_lift = decision.tool_owns_lift;

    this->sync_paintable_lift();
}

// The tool hands back the lift it took (M2.33). A model the M2.21 support preview lifts keeps the
// service's own lift, so only a lift the tool holds itself goes.
void SlaSupportPointsGizmo::release_tool_lift()
{
    if (!m_tool_owns_lift) {
        return;
    }
    m_tool_owns_lift = false;
    if (m_selected_object_id.valid()) {
        m_scene_presenter.set_sla_lift(m_selected_object_id, 0.);
    }
    this->sync_paintable_lift();
}

// The scene may change the lift of the object behind the tool's back: the support preview takes it
// for a model that has points and gives it back for one that has none. The volumes the raycast tests
// are rebuilt when it does, so a click always lands on the drawn surface.
void SlaSupportPointsGizmo::sync_paintable_lift()
{
    const double lift = applied_lift();
    if (lift == m_applied_lift) {
        return;
    }
    m_applied_lift = lift;
    if (m_selected_element.object_id == 0 || m_project_id == Domain::INVALID_ID) {
        return;
    }
    this->collect_paintable_volumes(m_project_id, m_selected_element);
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
