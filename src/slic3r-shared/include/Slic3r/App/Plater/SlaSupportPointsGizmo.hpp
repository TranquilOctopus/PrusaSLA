#pragma once

#include "Slic3r/App/Scene/IGizmo.hpp"
#include "Slic3r/App/Plater/GizmoWindow.hpp"
#include "Slic3r/Biz/SLAObjectCache.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Domain/ObjectID.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/App/Scene/TriangleMeshManager.hpp"
#include "Slic3r/Biz/Algorithms/AABBMesh.hpp"

#include <memory>
#include <optional>
#include <vector>

namespace Slic3r::App::Plater {
class SlaSupportPointsDialog;
class PlaterScenePresenter;
}

namespace Slic3r::Biz {
class ProjectInteractor;
class SlaSupportPointsRequest;
} // namespace Slic3r::Biz

namespace Slic3r::App::Plater {

struct SupportPointEditState
{
    Domain::SLA::SupportPoints working_points;
    std::optional<size_t> dragged_point_idx;
    Domain::Vec3d drag_start_world_pos;
    Domain::Vec3d drag_start_mesh_pos;
    double head_diameter_mm = 0.4;
};

struct SupportPointPaintableVolume
{
    const Domain::ModelObject& model_object;
    const Domain::ModelInstance& model_instance;
    Domain::ModelVolume& model_volume;
    const Scene::TriangleMesh& scene_mesh;
    const Slic3r::Biz::Algorithms::AABBMesh& aabb_mesh;
    Domain::Transform3d world_trafo;
    Domain::Transform3d world_trafo_no_translate;
};

using SupportPointPaintableVolumes = std::vector<SupportPointPaintableVolume>;

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

    Scene::GizmoActivationState on_mouse(Scene::GizmoEventContext& ctx, bool only_active) override;

    std::unique_ptr<GizmoWindow> release_ui_window() override;

private:
    void start_generation();
    void on_generation_completed(std::optional<Slic3r::Domain::SLA::SupportPoints> support_points);
    void apply_generated_points();
    void discard_generated_points();

    // Editing helpers
    void begin_editing();
    void end_editing();
    void apply_edited_points();
    void discard_edited_points();
    std::optional<size_t> find_nearest_point(const Domain::Vec3d& mesh_pos, double max_distance_mm) const;
    void add_point_at_mesh_pos(const Domain::Vec3d& mesh_pos);
    void remove_point_at_index(size_t idx);
    void move_point_to_mesh_pos(size_t idx, const Domain::Vec3d& mesh_pos);
    void take_undo_snapshot();

    // Raycasting helpers (adapted from PaintOnGizmoBase)
    struct VolumeHitPoint
    {
        Domain::Vec3d volume_hit_position = Domain::Vec3d::Zero();
        int volume_idx = -1;
        size_t facet_idx = 0;
    };
    Domain::Vec3d hit_to_object_pos(const VolumeHitPoint& hit) const;

    std::optional<VolumeHitPoint> raycast_mouse(const Domain::Vec2d& mouse_position) const;
    void collect_paintable_volumes(const Domain::SelectionId project_id, const Domain::ElementRef& element);

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

    // Editing state
    std::optional<SupportPointEditState> m_edit_state;

    // Paintable volumes for raycasting (like PaintOnGizmoBase)
    SupportPointPaintableVolumes m_paintable_volumes;
};

} // namespace Slic3r::App::Plater