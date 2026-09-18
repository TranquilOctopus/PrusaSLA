#include "Slic3r/App/Plater/SlaSupportPointsGizmo.hpp"

#include "Slic3r/App/Plater/SlaSupportPointsDialog.hpp"
#include "Slic3r/App/Plater/PlaterScenePresenter.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/IDialogManager.hpp"
#include "Slic3r/App/DisplayStrings.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Slicing/SlicingInteractor.hpp"
#include "Slic3r/Biz/StatusCache.hpp"
#include "Slic3r/Biz/IUndoProvider.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/Domain/ConfigContainer.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"

#include <Eigen/Geometry>
#include <fmt/format.h>

using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;
using namespace Slic3r::Biz::Slicing;

using Slic3r::Domain::SlicingId;
using Slic3r::Domain::ObjectID;
using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec3f;

namespace Slic3r::Biz {

/**
 * @brief Requests SLA support points for a model object by listening to SLAObjectCache.
 * The points arrive in world coordinates (first instance + printer corrections) and are
 * converted to mesh coordinates before being returned.
 */
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

        // Convert from world coordinates (first instance + printer corrections) to mesh coordinates
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
    Biz::ProjectInteractor& project_interactor
) :
    m_scene_presenter(scene_presenter),
    m_project_interactor(project_interactor),
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

    m_dialog->set_generate_enabled(false);
    m_dialog->set_apply_enabled(false);
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
    // Register selection listener only on the scene interactor (not on the scene directly),
    // as the scene does not carry ISceneSelectionChangedListener.
    m_project_interactor.scene_interactor().add_listener<Biz::Scene::ISceneSelectionChangedListener>(this);

    // Register for SLA object cache changes
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
    m_dialog->set_generate_enabled(false);
    m_dialog->set_apply_enabled(false);
    m_dialog->set_point_count(0);
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

    if (!enabled() || selection.elements.empty()) {
        m_dialog->set_generate_enabled(false);
        m_dialog->set_apply_enabled(false);
        m_dialog->set_point_count(0);
        return;
    }

    const Domain::ElementRef& element = selection.elements.front();
    if (element.volume_id != 0) {
        // Only support whole object selection
        m_dialog->set_generate_enabled(false);
        m_dialog->set_apply_enabled(false);
        m_dialog->set_point_count(0);
        return;
    }

    const Domain::Project& project = m_project_interactor.project(project_id);
    const Domain::ModelObject* model_object = project.find_object_by_id(element.object_id.id);
    if (!model_object) {
        m_dialog->set_generate_enabled(false);
        m_dialog->set_apply_enabled(false);
        m_dialog->set_point_count(0);
        return;
    }

    m_selected_object_id = model_object->id();
    m_selected_instance_id = element.instance_id;

    // Check if object is on a bed
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

    // Get the slicing ID for this bed
    const SlicingId slicing_id{project_id, bed_ref.instance_id};

    // Update dialog with current density setting
    int density = 100;
    auto result = model_object->object_settings_sla.find("support_points_density_relative");
    if (result.item) {
        density = result.item->get<int>();
    }
    m_dialog->set_density(density);

    // Show existing manual support points count
    size_t existing_count = model_object->sla_support_points.size();
    m_dialog->set_point_count(existing_count);

    m_dialog->set_generate_enabled(true);
    m_dialog->set_apply_enabled(false);
}

void SlaSupportPointsGizmo::on_sla_object_cache_changed(const Domain::SlicingId& id, Domain::ObjectID object_id)
{
    // Only care about the object we're currently generating for
    if (!m_generation_slicing_id.has_value() || id != *m_generation_slicing_id || object_id != m_selected_object_id) {
        return;
    }

    // The request object will handle the cache change and call our callback when ready
    // No direct action needed here - the request's listener will trigger completion
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

    // Check if object is printable
    const Domain::ModelInstance* instance = project.find_instance_by_id(m_selected_object_id.id, m_selected_instance_id);
    if (!instance || !instance->is_printable()) {
        AppServices::instance().dialog_manager().show_warning_dialog(
            _u8L("Automatic generation requires printable object."),
            _u8L("Warning")
        );
        return;
    }

    // Check if object is on a bed
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

    // The points are already in mesh coordinates (converted by the request)
    Domain::SLA::SupportPoints domain_points = std::move(*m_generated_support_points);

    // Take undo snapshot
    m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::SetPartSettingsValue);

    // Apply to model object
    model_object->sla_support_points = std::move(domain_points);
    model_object->sla_points_status = Domain::SLA::PointsStatus::AutoGenerated;

    m_has_generated_points = false;
    m_dialog->set_apply_enabled(false);
    m_dialog->set_point_count(model_object->sla_support_points.size());

    // Close the tool
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

    // Close the tool
    if (m_gizmo_controller) {
        m_gizmo_controller->deactivate_current_tool();
    }
}

std::unique_ptr<GizmoWindow> SlaSupportPointsGizmo::release_ui_window()
{
    return std::move(m_dialog);
}

} // namespace Slic3r::App::Plater