#pragma once

#include "Slic3r/App/Scene/IGizmo.hpp"
#include "Slic3r/App/Plater/GizmoWindow.hpp"
#include "Slic3r/Biz/SLAObjectCache.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Domain/ObjectID.hpp"
#include "Slic3r/Domain/SelectionId.hpp"

#include <memory>
#include <optional>

namespace Slic3r::App::Plater {
class SlaSupportPointsDialog;
class PlaterScenePresenter;
}

namespace Slic3r::Biz {
class ProjectInteractor;
class SlaSupportPointsRequest;
} // namespace Slic3r::Biz

namespace Slic3r::App::Plater {

class SlaSupportPointsGizmo :
    public Scene::IToolGizmo,
    public Biz::Scene::ISceneSelectionChangedListener,
    public Biz::ISLAObjectCacheChangedListener
{
public:
    SlaSupportPointsGizmo(
        PlaterScenePresenter& scene_presenter,
        Biz::ProjectInteractor& project_interactor
    );

    ~SlaSupportPointsGizmo() override;

    Scene::ToolType type() const override;
    bool supports_printer(Domain::PrinterTechnology pt) const override;
    bool enabled() const override;

    void on_activated() override;
    void on_deactivated() override;

    void on_scene_selection_changed(
        Domain::SelectionId project_id,
        const Biz::Scene::ObjectSelection& selection
    ) override;

    void on_sla_object_cache_changed(const Domain::SlicingId& id, Domain::ObjectID object_id) override;

    void provide_gizmo_controller(Scene::IGizmoController& controller) override;

    Scene::GizmoActivationState on_mouse(Scene::GizmoEventContext& ctx, bool only_active) override
    {
        return Scene::GizmoActivationState::Inactive;
    }

    std::unique_ptr<GizmoWindow> release_ui_window() override;

private:
    void start_generation();
    void on_generation_completed(std::optional<Slic3r::Domain::SLA::SupportPoints> support_points);
    void apply_generated_points();
    void discard_generated_points();

    PlaterScenePresenter& m_scene_presenter;
    Biz::ProjectInteractor& m_project_interactor;
    std::unique_ptr<SlaSupportPointsDialog> m_dialog;
    std::unique_ptr<Biz::SlaSupportPointsRequest> m_support_points_request;
    std::optional<Domain::SlicingId> m_generation_slicing_id;
    Domain::ObjectID m_selected_object_id;
    Domain::SelectionId m_selected_instance_id{Domain::INVALID_ID};
    std::optional<Slic3r::Domain::SLA::SupportPoints> m_generated_support_points;
    bool m_has_generated_points = false;
    Scene::IGizmoController* m_gizmo_controller = nullptr;
};

} // namespace Slic3r::App::Plater