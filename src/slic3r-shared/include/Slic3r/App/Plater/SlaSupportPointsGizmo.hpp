#pragma once

#include "Slic3r/App/Scene/IGizmo.hpp"
#include "Slic3r/App/Plater/GizmoWindow.hpp"
#include "Slic3r/App/Plater/SlaSupportPointsEditing.hpp"
#include "Slic3r/Biz/SLAObjectCache.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Domain/ObjectID.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/App/Scene/TriangleMeshManager.hpp"
#include "Slic3r/App/Render/GeometryManager.hpp"
#include "Slic3r/Biz/Algorithms/AABBMesh.hpp"
#include "Slic3r/App/Scene/Clipper.hpp"
#include "Slic3r/App/Scene/ClipperPresenter.hpp"
#include "Slic3r/App/Yoga/Item.hpp"

#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

namespace Slic3r::App::Plater {
class SlaSupportPointsDialog;
class PlaterScenePresenter;
}

namespace Slic3r::App::Render {
class Device;
}

namespace Slic3r::Biz {
class ProjectInteractor;
class SlaSupportPointsRequest;
} // namespace Slic3r::Biz

namespace Slic3r::App::Plater {

struct SupportPointEditState
{
    SlaSupportPointsEditing editing;
    std::optional<size_t> dragged_point_idx;
    Domain::Vec3d drag_start_world_pos;
    Domain::Vec3d drag_start_mesh_pos;

    // Rectangle selection state
    bool rect_select_active = false;
    Domain::Vec2d rect_select_start_pos;
    Domain::Vec2d rect_select_current_pos;
    bool rect_select_is_add = true;
};

struct SupportPointPaintableVolume
{
    const Domain::ModelObject& model_object;
    const Domain::ModelInstance& model_instance;
    Domain::ModelVolume& model_volume;
    const Scene::TriangleMesh& scene_mesh;
    const Slic3r::AABBMesh& aabb_mesh;
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
        Biz::ProjectInteractor& project_interactor,
        Render::Device& device
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

    void provide_clipper(Scene::Clipper& clipper);

    void render_scene(Render::CommandBuffer& cmd_buffer) override;
    void on_keyboard(Scene::GizmoKeyEventContext& ctx) override;

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

    // Visuals
    void update_point_visuals();
    void clear_point_visuals();
    Domain::ColorRGBA get_point_color(const Domain::SLA::SupportPoint& point, bool highlighted) const;

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

    // Clipping plane
    void update_clipping_plane();
    void reset_clipping_plane();

    // Selection helpers
    void select_point(size_t idx, bool add_to_selection = false);
    void deselect_point(size_t idx);
    void select_all_points();
    void clear_selection();
    void delete_selected_points();
    void apply_head_diameter_to_selected();
    void apply_pillar_diameter_to_selected();
    void apply_base_diameter_to_selected();
    void apply_base_height_to_selected();
    void apply_preset_light();
    void apply_preset_medium();
    void apply_preset_heavy();
    void apply_support_preset(float head_diameter, float pillar_diameter, float base_diameter, float base_height, int preset_index);

    // Rectangle selection
    void start_rectangle_selection(const Domain::Vec2d& mouse_pos, bool is_add);
    void update_rectangle_selection(const Domain::Vec2d& mouse_pos);
    void finish_rectangle_selection();
    void project_points_to_screen(std::vector<Domain::Vec2d>& out_screen_positions) const;

    // Cone visual
    void create_cone_geometry_if_needed();

    PlaterScenePresenter& m_scene_presenter;
    Biz::ProjectInteractor& m_project_interactor;
    Render::Device& m_device;
    Yoga::Passthrough<SlaSupportPointsDialog> m_dialog;
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

    // Clipping plane (like PaintOnGizmoBase)
    Scene::ClipperPresenter m_clipping_plane_presenter;

    // Scene nodes for point visuals
    Scene::Node* m_main_node = nullptr;
    Scene::Node* m_points_node = nullptr;

    // Geometry and triangle mesh managers for point visuals (like MeasureGizmo)
    using GeometryManager = Render::GeometryManager<std::string>;
    using TriangleMeshManager = Scene::TriangleMeshManager<std::string>;
    GeometryManager m_geometry_manager{"sla_support_points_geometry"};
    TriangleMeshManager m_triangle_mesh_manager{"sla_support_points_mesh"};

    // Cone geometry for surface normal visualization
    std::string m_cone_geometry_id = "support_point_cone";
    bool m_cone_geometry_created = false;

    // Hovered point index (for highlight)
    std::optional<size_t> m_hovered_point_idx;

    // Guard to prevent dialog setters from triggering value-change callbacks
    bool m_syncing_dialog{false};

    class DialogSyncGuard
    {
    public:
        explicit DialogSyncGuard(SlaSupportPointsGizmo& gizmo)
            : m_gizmo(gizmo), m_previous(gizmo.m_syncing_dialog)
        { m_gizmo.m_syncing_dialog = true; }
        ~DialogSyncGuard() { m_gizmo.m_syncing_dialog = m_previous; }
        DialogSyncGuard(const DialogSyncGuard&) = delete;
        DialogSyncGuard& operator=(const DialogSyncGuard&) = delete;
    private:
        SlaSupportPointsGizmo& m_gizmo;
        bool m_previous;
    };
};

} // namespace Slic3r::App::Plater