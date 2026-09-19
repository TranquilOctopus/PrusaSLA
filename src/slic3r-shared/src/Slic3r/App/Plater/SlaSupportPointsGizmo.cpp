#include "Slic3r/App/Plater/SlaSupportPointsGizmo.hpp"

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
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Slicing/SlicingInteractor.hpp"
#include "Slic3r/Biz/StatusCache.hpp"
#include "Slic3r/Biz/IUndoProvider.hpp"
#include "Slic3r/Biz/Utils/MeshRaycaster.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/Domain/ConfigContainer.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Math.hpp"
#include "Slic3r/App/Platform/KeyboardEvent.hpp"
#include "Slic3r/App/Platform/KeyModifers.hpp"
#include "Slic3r/App/Platform/KeyCode.hpp"

#include <Eigen/Geometry>
#include <fmt/format.h>
#include <magic_enum/magic_enum_flags.hpp>

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

namespace Slic3r::Biz {

class SlaSupportPointsRequest :
    public ISLAObjectCacheChangedListener,
    public IStatusCacheChangedListener
{
public:
    struct Callbacks
    {
        std::function<void(std::optional<Domain::SLA::SupportPoints>)> completed =
            [](std::optional<Domain::SLA::SupportPoints>) {};
    };

    SlaSupportPointsRequest() = delete;

    SlaSupportPointsRequest(
        SlicingInteractor& slicing_interactor,
        StatusCache& status_cache,
        SLAObjectCache& sla_object_cache,
        ProjectInteractor& project_interactor
    ) :
        m_slicing_interactor(slicing_interactor),
        m_status_cache(status_cache),
        m_sla_object_cache(sla_object_cache),
        m_project_interactor(project_interactor)
    {}

    ~SlaSupportPointsRequest() override
    {
        this->cancel();
    }

    Callbacks& callbacks()
    {
        return m_callbacks;
    }

    void start(SlicingId slicing_id, ObjectID model_object_id)
    {
        if (this->running()) {
            return;
        }

        m_state = State::WaitingForSlicing;
        m_slicing_id = slicing_id;
        m_model_object_id = model_object_id;
        m_has_fresh_points = false;
        m_sla_object_cache.add_listener<ISLAObjectCacheChangedListener>(this);
        m_status_cache.add_listener<IStatusCacheChangedListener>(this);

        const StatusCode status = m_slicing_interactor.get_status(slicing_id);
        if (status == StatusCode::Finished) {
            this->try_complete_from_cache();
        } else if (status == StatusCode::Modified) {
            this->request_slicing_until_support_spots();
        } else if (status == StatusCode::Empty || status == StatusCode::InvalidData) {
            this->complete(std::nullopt);
        }
    }

    void cancel()
    {
        if (!this->running()) {
            return;
        }

        m_sla_object_cache.remove_listener<ISLAObjectCacheChangedListener>(this);
        m_status_cache.remove_listener<IStatusCacheChangedListener>(this);

        m_state = State::Idle;
    }

    [[nodiscard]] bool running() const
    {
        return m_state != State::Idle;
    }

    void on_sla_object_cache_changed(const SlicingId& id, ObjectID object_id) override
    {
        if (!this->running() || id != m_slicing_id || object_id != m_model_object_id) {
            return;
        }

        const std::optional<Slicing::Status> current_status = m_status_cache.get_status(id);
        if (current_status.has_value()
            && current_status->code == StatusCode::Running
            && this->cached_support_points().has_value())
        {
            m_has_fresh_points = true;
        }
    }

    void on_status_cache_status_code_changed(const SlicingId id) override
    {
        if (!this->running() || id != m_slicing_id) {
            return;
        }

        const std::optional<Slicing::Status> status = m_status_cache.get_status(id);
        if (!status.has_value()) {
            this->complete(std::nullopt);
            return;
        }

        switch (status->code) {
        case StatusCode::Running:
            m_state = State::SlicingActive;
            m_has_fresh_points = false;
            break;
        case StatusCode::Stopping:
            m_state = State::SlicingActive;
            m_has_fresh_points = false;
            break;
        case StatusCode::Updating:
            m_has_fresh_points = false;
            break;
        case StatusCode::Modified:
            if (m_has_fresh_points) {
                this->try_complete_from_cache();
            } else if (m_state == State::WaitingForSlicing) {
                this->request_slicing_until_support_spots();
            } else {
                this->complete(std::nullopt);
            }
            break;
        case StatusCode::Finished:
            this->try_complete_from_cache();
            break;
        case StatusCode::Empty:
        case StatusCode::InvalidData:
            this->complete(std::nullopt);
            break;
        default:
            break;
        }
    }

private:
    enum class State
    {
        Idle,
        WaitingForSlicing,
        SlicingRequested,
        SlicingActive
    };

    [[nodiscard]] std::optional<Domain::SLA::SupportPoints> cached_support_points() const
    {
        const SLAObjectCache::Key key{m_slicing_id, m_model_object_id};
        const SLAObjectOptRef opt_ref = m_sla_object_cache.get_instance(key);
        if (!opt_ref.has_value()) {
            return std::nullopt;
        }

        const Slicing::Sla::Object& sla_object = opt_ref->get();
        if (!sla_object.support_points) {
            return std::nullopt;
        }

        Domain::SLA::SupportPoints world_points = *sla_object.support_points;
        Domain::SLA::SupportPoints mesh_points;
        mesh_points.reserve(world_points.size());
        const Domain::Transform3f inv = sla_object.object_trafo.inverse().cast<float>();
        for (const auto& sp : world_points) {
            Domain::SLA::SupportPoint mesh_sp = sp;
            mesh_sp.pos = inv * sp.pos;
            mesh_points.push_back(mesh_sp);
        }
        return mesh_points;
    }

    void try_complete_from_cache()
    {
        const std::optional<Domain::SLA::SupportPoints> points = this->cached_support_points();
        if (points.has_value()) {
            this->complete(points);
        }
    }

    void complete(std::optional<Domain::SLA::SupportPoints> support_points)
    {
        this->cancel();
        m_callbacks.completed(support_points);
    }

    void request_slicing_until_support_spots()
    {
        m_state = State::SlicingRequested;
        m_slicing_interactor.slice_bed(
            m_slicing_id,
            SliceUntilStep{Slic3r::slaposSupportPoints, m_model_object_id}
        );
    }

    SlicingInteractor& m_slicing_interactor;
    StatusCache& m_status_cache;
    SLAObjectCache& m_sla_object_cache;
    ProjectInteractor& m_project_interactor;

    Callbacks m_callbacks;

    State m_state = State::Idle;
    bool m_has_fresh_points = false;
    SlicingId m_slicing_id;
    ObjectID m_model_object_id;
};

} // namespace Slic3r::Biz

namespace Slic3r::App::Plater {

SlaSupportPointsGizmo::SlaSupportPointsGizmo(
    PlaterScenePresenter& scene_presenter,
    Biz::ProjectInteractor& project_interactor,
    Render::Device& device
) :
    m_scene_presenter(scene_presenter),
    m_project_interactor(project_interactor),
    m_device(device),
    m_dialog(std::make_unique<SlaSupportPointsDialog>())
{
    m_dialog->set_title(_u8L("SLA Support Points"));
    m_dialog->set_shortcut("P");

    m_support_points_request = std::make_unique<Biz::SlaSupportPointsRequest>(
        m_project_interactor.slicing_interactor(),
        m_project_interactor.status_cache(),
        m_project_interactor.sla_object_cache(),
        m_project_interactor
    );

    m_dialog->callbacks().generate = [this]() { this->start_generation(); };
    m_dialog->callbacks().apply = [this]() { this->apply_generated_points(); };
    m_dialog->callbacks().discard = [this]() { this->discard_generated_points(); };
    m_dialog->callbacks().density_changed = [this](double value)
    {
        const int density = static_cast<int>(value);
        if (m_selected_object_id.valid()) {
            Domain::Project& project = m_project_interactor.selected_project();
            Domain::ModelObject* model_object = project.find_object_by_id(m_selected_object_id.id);
            if (model_object) {
                m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::SetPartSettingsValue);
                auto result = model_object->object_settings_sla.find("support_points_density_relative");
                if (result.item) {
                    result.item->set<int>(density);
                }
                this->start_generation();
            }
        }
    };
    m_dialog->callbacks().head_diameter_changed = [this](double value)
    {
        if (m_edit_state.has_value()) {
            m_edit_state->head_diameter_mm = value;
            this->apply_head_diameter_to_selected();
        }
    };
    m_dialog->callbacks().clipping_plane_changed = [this](double value)
    {
        m_clipping_plane_clipper.set_position_by_ratio(value, true);
        update_clipping_plane();
    };
    m_dialog->callbacks().lock_island_supports_changed = [this](bool value)
    {
        if (m_edit_state.has_value()) {
            m_edit_state->lock_island_supports = value;
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

SlaSupportPointsGizmo::~SlaSupportPointsGizmo() = default;

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
    m_project_interactor.scene_interactor().add_listener<Biz::Scene::ISceneSelectionChangedListener>(this);
    m_project_interactor.sla_object_cache().add_listener<Biz::ISLAObjectCacheChangedListener>(this);

    const Biz::Scene::ObjectSelection& selection =
        m_project_interactor.scene_interactor().object_selection();
    this->on_scene_selection_changed(m_project_interactor.selected_project_id(), selection);
}

void SlaSupportPointsGizmo::on_deactivated()
{
    m_project_interactor.scene_interactor().remove_listener<Biz::Scene::ISceneSelectionChangedListener>(this);
    m_project_interactor.sla_object_cache().remove_listener<Biz::ISLAObjectCacheChangedListener>(this);

    if (m_generation_slicing_id.has_value()) {
        m_support_points_request->cancel();
        m_generation_slicing_id.reset();
    }
    m_has_generated_points = false;
    m_generated_support_points.reset();

    if (m_edit_state.has_value()) {
        discard_edited_points();
    }

    m_paintable_volumes.clear();

    // Deactivate clipping plane presenter
    m_clipping_plane_presenter.deactivate();

    // Clear point visuals
    clear_point_visuals();
    m_hovered_point_idx.reset();

    m_dialog->set_generate_enabled(false);
    m_dialog->set_apply_enabled(false);
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
            model_instance->get_matrix() * model_volume->get_matrix(),
            model_instance->get_matrix_no_offset() * model_volume->get_matrix_no_offset()
        });
    }
}

void SlaSupportPointsGizmo::on_scene_selection_changed(
    Domain::SelectionId project_id,
    const Biz::Scene::ObjectSelection& selection
)
{
    if (m_generation_slicing_id.has_value()) {
        m_support_points_request->cancel();
        m_generation_slicing_id.reset();
    }
    m_has_generated_points = false;
    m_generated_support_points.reset();

    if (m_edit_state.has_value()) {
        discard_edited_points();
    }
    m_hovered_point_idx.reset();

    if (!enabled() || selection.elements.empty()) {
        m_dialog->set_generate_enabled(false);
        m_dialog->set_apply_enabled(false);
        m_dialog->set_point_count(0);
        return;
    }

    const Domain::ElementRef& element = selection.elements.front();
    if (element.volume_id != 0) {
        m_dialog->set_generate_enabled(false);
        m_dialog->set_apply_enabled(false);
        m_dialog->set_point_count(0);
        return;
    }

    const Domain::Project& project = m_project_interactor.project(project_id);
    const Domain::ModelObject* model_object = project.find_object_by_id(element.object_id);
    if (!model_object) {
        m_dialog->set_generate_enabled(false);
        m_dialog->set_apply_enabled(false);
        m_dialog->set_point_count(0);
        return;
    }

    m_selected_object_id = model_object->id();
    m_selected_instance_id = element.instance_id;

    const Domain::ModelInstance* instance = project.find_instance_by_id(element.object_id, element.instance_id);
    if (!instance) {
        m_dialog->set_generate_enabled(false);
        m_dialog->set_apply_enabled(false);
        m_dialog->set_point_count(0);
        return;
    }

    const Domain::BedRef bed_ref = instance->get_last_bed();
    if (project.find_bed_instance_by_id(bed_ref.instance_id) == nullptr) {
        m_dialog->set_generate_enabled(false);
        m_dialog->set_apply_enabled(false);
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
    m_clipping_plane_presenter.activate(
        model_object,
        instance,
        m_main_node,
        0.,
        Scene::BuildMeshesNodes::No
    );
    m_clipping_plane_presenter.set_behavior(true, true, 0.);
    m_clipping_plane_presenter.set_position_by_ratio(m_clipping_plane_clipper.get_position(), true);
    m_dialog->set_clipping_plane_position(m_clipping_plane_clipper.get_position());

    m_dialog->set_generate_enabled(true);
    m_dialog->set_apply_enabled(false);
}

void SlaSupportPointsGizmo::on_sla_object_cache_changed(const Domain::SlicingId& id, Domain::ObjectID object_id)
{
    if (!m_generation_slicing_id.has_value() || id != *m_generation_slicing_id || object_id != m_selected_object_id) {
        return;
    }
}

void SlaSupportPointsGizmo::start_generation()
{
    if (m_generation_slicing_id.has_value() || !m_selected_object_id.valid()) {
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
            _u8L("Automatic generation requires the object to be placed on a bed."),
            _u8L("Warning")
        );
        return;
    }

    const SlicingId slicing_id{m_project_interactor.selected_project_id(), bed_ref.instance_id};
    const StatusCode status = m_project_interactor.slicing_interactor().get_status(slicing_id);
    if (status == StatusCode::InvalidData) {
        std::string error_message;
        const std::optional<Slicing::Status> status_opt = m_project_interactor.status_cache().get_status(slicing_id);
        if (status_opt.has_value()) {
            for (const Slicing::Error& error : status_opt->errors) {
                error_message += "\n" + App::to_display_string(error, project);
            }
        }
        AppServices::instance().dialog_manager().show_warning_dialog(
            _u8L("Automatic generation requires valid print setup.") + error_message,
            _u8L("Warning")
        );
        return;
    } else if (status == StatusCode::Empty) {
        AppServices::instance().dialog_manager().show_warning_dialog(
            _u8L("Automatic generation requires printable object."),
            _u8L("Warning")
        );
        return;
    }

    m_generation_slicing_id = slicing_id;
    m_dialog->set_generate_enabled(false);
    m_dialog->set_apply_enabled(false);

    m_support_points_request->callbacks().completed =
        [this](const std::optional<Domain::SLA::SupportPoints> support_points)
    { this->on_generation_completed(support_points); };

    m_support_points_request->start(slicing_id, m_selected_object_id);
}

void SlaSupportPointsGizmo::on_generation_completed(std::optional<Domain::SLA::SupportPoints> support_points)
{
    m_generation_slicing_id.reset();

    if (support_points.has_value()) {
        m_generated_support_points = *support_points;
        m_has_generated_points = true;

        size_t count = m_generated_support_points->size();

        m_dialog->set_point_count(count);
        m_dialog->set_apply_enabled(true);
        m_dialog->set_generate_enabled(true);
    } else {
        m_has_generated_points = false;
        m_generated_support_points.reset();
        m_dialog->set_point_count(0);
        m_dialog->set_apply_enabled(false);
        m_dialog->set_generate_enabled(true);

        AppServices::instance().dialog_manager().show_warning_dialog(
            _u8L("Failed to generate support points."),
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

    m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::SetPartSettingsValue);

    model_object->sla_support_points = std::move(domain_points);
    model_object->sla_points_status = PointsStatus::AutoGenerated;

    m_has_generated_points = false;
    m_dialog->set_apply_enabled(false);
    m_dialog->set_point_count(model_object->sla_support_points.size());

    if (m_gizmo_controller) {
        m_gizmo_controller->deactivate_current_tool();
    }
}

void SlaSupportPointsGizmo::discard_generated_points()
{
    m_has_generated_points = false;
    m_generated_support_points.reset();
    m_generation_slicing_id.reset();
    m_dialog->set_apply_enabled(false);
    m_dialog->set_generate_enabled(true);

    if (m_gizmo_controller) {
        m_gizmo_controller->deactivate_current_tool();
    }
}

void SlaSupportPointsGizmo::begin_editing()
{
    Domain::Project& project = m_project_interactor.selected_project();
    Domain::ModelObject* model_object = project.find_object_by_id(m_selected_object_id.id);
    if (!model_object) {
        return;
    }

    m_edit_state = SupportPointEditState{};
    m_edit_state->working_points = model_object->sla_support_points;

    double head_diameter = 0.4;
    auto head_result = model_object->object_settings_sla.find("support_head_front_diameter");
    if (head_result.item) {
        head_diameter = head_result.item->get<double>();
    }
    m_edit_state->head_diameter_mm = head_diameter;
    m_dialog->set_head_diameter(head_diameter);
    m_dialog->set_lock_island_supports(false);

    m_dialog->set_apply_enabled(true);
    m_dialog->set_generate_enabled(true);

    update_point_visuals();
}

void SlaSupportPointsGizmo::end_editing()
{
    clear_point_visuals();
    m_hovered_point_idx.reset();
    m_edit_state.reset();
}

void SlaSupportPointsGizmo::apply_edited_points()
{
    if (!m_edit_state.has_value() || !m_selected_object_id.valid()) {
        return;
    }

    Domain::Project& project = m_project_interactor.selected_project();
    Domain::ModelObject* model_object = project.find_object_by_id(m_selected_object_id.id);
    if (!model_object) {
        return;
    }

    m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::SetPartSettingsValue);

    model_object->sla_support_points = std::move(m_edit_state->working_points);
    model_object->sla_points_status = PointsStatus::UserModified;

    m_dialog->set_point_count(model_object->sla_support_points.size());
    end_editing();

    if (m_gizmo_controller) {
        m_gizmo_controller->deactivate_current_tool();
    }
}

void SlaSupportPointsGizmo::discard_edited_points()
{
    end_editing();
    m_dialog->set_apply_enabled(false);

    Domain::Project& project = m_project_interactor.selected_project();
    Domain::ModelObject* model_object = project.find_object_by_id(m_selected_object_id.id);
    if (model_object) {
        m_dialog->set_point_count(model_object->sla_support_points.size());
    }
}

void SlaSupportPointsGizmo::take_undo_snapshot()
{
    m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::SetPartSettingsValue);
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

    const auto& points = m_edit_state->working_points;
    std::optional<size_t> nearest_idx;
    double nearest_dist_sq = max_distance_mm * max_distance_mm;

    for (size_t i = 0; i < points.size(); ++i) {
        const Domain::Vec3d point_pos = points[i].pos.cast<double>();
        double dist_sq = (point_pos - mesh_pos).squaredNorm();
        if (dist_sq < nearest_dist_sq) {
            nearest_dist_sq = dist_sq;
            nearest_idx = i;
        }
    }

    return nearest_idx;
}

void SlaSupportPointsGizmo::add_point_at_mesh_pos(const Domain::Vec3d& mesh_pos)
{
    if (!m_edit_state.has_value()) {
        return;
    }

    SupportPoint new_point;
    new_point.pos = mesh_pos.cast<float>();
    new_point.head_front_radius = static_cast<float>(m_edit_state->head_diameter_mm / 2.0);
    new_point.type = SupportPointType::manual_add;

    m_edit_state->working_points.push_back(new_point);
    m_dialog->set_point_count(m_edit_state->working_points.size());
    take_undo_snapshot();
    update_point_visuals();
}

void SlaSupportPointsGizmo::remove_point_at_index(size_t idx)
{
    if (!m_edit_state.has_value() || idx >= m_edit_state->working_points.size()) {
        return;
    }

    m_edit_state->working_points.erase(m_edit_state->working_points.begin() + idx);
    m_dialog->set_point_count(m_edit_state->working_points.size());
    take_undo_snapshot();
    update_point_visuals();
}

void SlaSupportPointsGizmo::move_point_to_mesh_pos(size_t idx, const Domain::Vec3d& mesh_pos)
{
    if (!m_edit_state.has_value() || idx >= m_edit_state->working_points.size()) {
        return;
    }

    m_edit_state->working_points[idx].pos = mesh_pos.cast<float>();
    m_dialog->set_point_count(m_edit_state->working_points.size());
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
    if (m_clipping_plane_clipper.get_position() != 0.) {
        clipping_plane_opt = m_clipping_plane_clipper.get_clipping_plane();
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
        const double hover_radius = m_edit_state->head_diameter_mm * 2.0;
        m_hovered_point_idx = find_nearest_point(mesh_pos, hover_radius);
    } else if (!has_hit || m_edit_state->dragged_point_idx.has_value() || m_edit_state->rect_select_active) {
        m_hovered_point_idx.reset();
    }

    // Handle mouse wheel for clipping plane (Ctrl + wheel)
    if (mouse_event.type() == MouseEvent::Type::Wheel) {
        if (ctrl_down) {
            const float wheel_rotation =
                mouse_event.wheel_delta_y() / std::abs(mouse_event.wheel_delta_y());
            double pos = m_clipping_plane_clipper.get_position();
            pos = (wheel_rotation > 0.f) ? std::min(1., pos + 0.01) : std::max(0., pos - 0.01);
            m_clipping_plane_clipper.set_position_by_ratio(pos, true);
            update_clipping_plane();
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
                const double removal_radius = m_edit_state->head_diameter_mm * 2.0;
                if (auto idx = find_nearest_point(mesh_pos, removal_radius); idx.has_value()) {
                    if (!m_edit_state->lock_island_supports || !m_edit_state->working_points[*idx].is_island()) {
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
            const double selection_radius = m_edit_state->head_diameter_mm * 2.0;
            if (auto idx = find_nearest_point(mesh_pos, selection_radius); idx.has_value()) {
                if (m_edit_state->selected_point_indices.count(*idx)) {
                    deselect_point(*idx);
                } else {
                    select_point(*idx, true);
                }
                update_point_visuals();
                return Scene::GizmoActivationState::Active;
            }
        }

        // Regular click on point: select and start drag
        if (has_hit) {
            const Domain::Vec3d mesh_pos = hit_to_object_pos(*hit_opt);
            const double selection_radius = m_edit_state->head_diameter_mm * 2.0;
            if (auto idx = find_nearest_point(mesh_pos, selection_radius); idx.has_value()) {
                if (!m_edit_state->lock_island_supports || !m_edit_state->working_points[*idx].is_island()) {
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
            const double removal_radius = m_edit_state->head_diameter_mm * 2.0;
            if (auto idx = find_nearest_point(mesh_pos, removal_radius); idx.has_value()) {
                if (!m_edit_state->lock_island_supports || !m_edit_state->working_points[*idx].is_island()) {
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
    return std::move(m_dialog);
}

// Visuals

void SlaSupportPointsGizmo::update_point_visuals()
{
    if (!m_edit_state.has_value() || m_points_node == nullptr) {
        return;
    }

    const auto& points = m_edit_state->working_points;
    if (points.empty()) {
        clear_point_visuals();
        return;
    }

    Scene::Scene& scene = m_scene_presenter.scene();

    // Get the instance transform
    const Domain::Project& project = m_project_interactor.selected_project();
    const Domain::ModelInstance* instance = project.find_instance_by_id(m_selected_object_id.id, m_selected_instance_id);
    if (!instance) {
        return;
    }
    const Domain::Transform3d instance_trafo = instance->get_matrix();

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
        const bool is_selected = m_edit_state->selected_point_indices.count(i) > 0;
        const bool is_locked_island = m_edit_state->lock_island_supports && point.is_island();

        // Point position in world space: instance_trafo * point.pos (point.pos is in mesh coords)
        Domain::Vec3d world_pos = instance_trafo * point.pos.cast<double>();

        // Radius = head_front_radius (minimum 0.2 mm)
        double radius = std::max(static_cast<double>(point.head_front_radius), 0.2);

        // Color based on point type and state
        ColorRGBA color = get_point_color(point, highlighted || is_selected);

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
            cone_xform.scale(radius * CONE_RADIUS, radius * CONE_RADIUS, radius * CONE_HEIGHT);

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

Domain::ColorRGBA SlaSupportPointsGizmo::get_point_color(const Domain::SLA::SupportPoint& point, bool highlighted) const
{
    const auto& theme = AppServices::instance().theme();

    ColorRGBA base_color;
    switch (point.type) {
    case SupportPointType::manual_add:
        base_color = theme.color(Platform::Color::SlaSupportPointManual, Platform::ColorGroup::Default);
        break;
    case SupportPointType::island:
        base_color = theme.color(Platform::Color::SlaIslandWarning, Platform::ColorGroup::Default);
        break;
    case SupportPointType::slope:
    default:
        base_color = theme.color(Platform::Color::SlaSupportPointAuto, Platform::ColorGroup::Default);
        break;
    }

    if (highlighted) {
        // Brighten for highlight
        return ColorRGBA{
            std::min(base_color.r() * 1.5f, 1.0f),
            std::min(base_color.g() * 1.5f, 1.0f),
            std::min(base_color.b() * 1.5f, 1.0f),
            base_color.a()
        };
    }

    return base_color;
}

// Clipping plane

void SlaSupportPointsGizmo::update_clipping_plane()
{
    // Update the clipper presenter which updates the scene nodes
    m_clipping_plane_presenter.update_clipper(
        m_clipping_plane_clipper.get_clipping_plane().get_normal(),
        m_clipping_plane_clipper.get_clipping_plane().get_offset(),
        m_clipping_plane_clipper.get_position(),
        false
    );
}

void SlaSupportPointsGizmo::reset_clipping_plane()
{
    m_clipping_plane_clipper.set_position_by_ratio(-1., false);
    update_clipping_plane();
    m_dialog->set_clipping_plane_position(m_clipping_plane_clipper.get_position());
}

// Selection helpers

void SlaSupportPointsGizmo::select_point(size_t idx, bool add_to_selection)
{
    if (!m_edit_state.has_value() || idx >= m_edit_state->working_points.size()) {
        return;
    }
    if (!add_to_selection) {
        m_edit_state->selected_point_indices.clear();
    }
    m_edit_state->selected_point_indices.insert(idx);
    update_point_visuals();
}

void SlaSupportPointsGizmo::deselect_point(size_t idx)
{
    if (!m_edit_state.has_value()) {
        return;
    }
    m_edit_state->selected_point_indices.erase(idx);
    update_point_visuals();
}

void SlaSupportPointsGizmo::select_all_points()
{
    if (!m_edit_state.has_value()) {
        return;
    }
    m_edit_state->selected_point_indices.clear();
    for (size_t i = 0; i < m_edit_state->working_points.size(); ++i) {
        if (!m_edit_state->lock_island_supports || !m_edit_state->working_points[i].is_island()) {
            m_edit_state->selected_point_indices.insert(i);
        }
    }
    update_point_visuals();
}

void SlaSupportPointsGizmo::clear_selection()
{
    if (!m_edit_state.has_value()) {
        return;
    }
    m_edit_state->selected_point_indices.clear();
    update_point_visuals();
}

void SlaSupportPointsGizmo::delete_selected_points()
{
    if (!m_edit_state.has_value() || m_edit_state->selected_point_indices.empty()) {
        return;
    }

    // Collect indices to delete (sorted descending to erase correctly)
    std::vector<size_t> indices_to_delete(m_edit_state->selected_point_indices.begin(),
                                           m_edit_state->selected_point_indices.end());
    std::sort(indices_to_delete.rbegin(), indices_to_delete.rend());

    bool any_deleted = false;
    for (size_t idx : indices_to_delete) {
        if (idx < m_edit_state->working_points.size()) {
            const bool is_island = m_edit_state->working_points[idx].is_island();
            if (!m_edit_state->lock_island_supports || !is_island) {
                m_edit_state->working_points.erase(m_edit_state->working_points.begin() + idx);
                any_deleted = true;
            }
        }
    }

    if (any_deleted) {
        m_edit_state->selected_point_indices.clear();
        m_dialog->set_point_count(m_edit_state->working_points.size());
        take_undo_snapshot();
        update_point_visuals();
    }
}

void SlaSupportPointsGizmo::apply_head_diameter_to_selected()
{
    if (!m_edit_state.has_value() || m_edit_state->selected_point_indices.empty()) {
        return;
    }

    const float new_radius = static_cast<float>(m_edit_state->head_diameter_mm / 2.0);
    for (size_t idx : m_edit_state->selected_point_indices) {
        if (idx < m_edit_state->working_points.size()) {
            m_edit_state->working_points[idx].head_front_radius = new_radius;
        }
    }
    update_point_visuals();
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

    const Domain::Vec2d rect_min(
        std::min(m_edit_state->rect_select_start_pos.x(), m_edit_state->rect_select_current_pos.x()),
        std::min(m_edit_state->rect_select_start_pos.y(), m_edit_state->rect_select_current_pos.y())
    );
    const Domain::Vec2d rect_max(
        std::max(m_edit_state->rect_select_start_pos.x(), m_edit_state->rect_select_current_pos.x()),
        std::max(m_edit_state->rect_select_start_pos.y(), m_edit_state->rect_select_current_pos.y())
    );

    std::vector<size_t> indices = points_in_rectangle(rect_min, rect_max);

    if (m_edit_state->rect_select_is_add) {
        for (size_t idx : indices) {
            if (!m_edit_state->lock_island_supports || !m_edit_state->working_points[idx].is_island()) {
                m_edit_state->selected_point_indices.insert(idx);
            }
        }
    } else {
        for (size_t idx : indices) {
            m_edit_state->selected_point_indices.erase(idx);
        }
    }

    m_edit_state->rect_select_active = false;
    update_point_visuals();
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
    const Domain::Transform3d instance_trafo = instance->get_matrix();

    out_screen_positions.resize(m_edit_state->working_points.size());

    for (size_t i = 0; i < m_edit_state->working_points.size(); ++i) {
        const Domain::Vec3d world_pos = instance_trafo * m_edit_state->working_points[i].pos.cast<double>();
        Domain::Vec2d screen_pos = camera.project_to_screen_space(world_pos);
        out_screen_positions[i] = screen_pos;
    }
}

std::vector<size_t> SlaSupportPointsGizmo::points_in_rectangle(const Domain::Vec2d& rect_min, const Domain::Vec2d& rect_max) const
{
    std::vector<Domain::Vec2d> screen_positions;
    project_points_to_screen(screen_positions);

    std::vector<size_t> result;
    for (size_t i = 0; i < screen_positions.size(); ++i) {
        const Domain::Vec2d& pos = screen_positions[i];
        if (pos.x() >= rect_min.x() && pos.x() <= rect_max.x() &&
            pos.y() >= rect_min.y() && pos.y() <= rect_max.y()) {
            result.push_back(i);
        }
    }
    return result;
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

    Platform::KeyCode code = evt.code();

    // Ctrl+A: Select all
    if (code == Platform::KeyCode::A && Platform::ctrl_down(evt.key_modifiers())) {
        if (evt.type() == Platform::KeyboardEvent::Type::KeyDown) {
            select_all_points();
        }
        return;
    }

    // Delete or Backspace: Delete selected points
    if ((code == Platform::KeyCode::Delete || code == Platform::KeyCode::Backspace) &&
        evt.type() == Platform::KeyboardEvent::Type::KeyDown) {
        delete_selected_points();
        return;
    }
}

void SlaSupportPointsGizmo::render_scene(Render::CommandBuffer& cmd_buffer)
{
    // Update point visuals each frame to reflect hover/drag state
    update_point_visuals();
}

} // namespace Slic3r::App::Plater