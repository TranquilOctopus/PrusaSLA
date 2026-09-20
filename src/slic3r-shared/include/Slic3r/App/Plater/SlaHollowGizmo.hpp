#pragma once

#include "Slic3r/App/Scene/IGizmo.hpp"
#include "Slic3r/App/Plater/GizmoWindow.hpp"
#include "Slic3r/App/Plater/SlaDrainHolesEditing.hpp"
#include "Slic3r/Biz/SLAObjectCache.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Domain/ObjectID.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/App/Scene/TriangleMeshManager.hpp"
#include "Slic3r/App/Render/GeometryManager.hpp"
#include "Slic3r/Biz/Algorithms/AABBMesh.hpp"
#include "Slic3r/Biz/Slicing/SlicingInteractor.hpp"

#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

namespace Slic3r::App::Plater {
class SlaHollowDialog;
class PlaterScenePresenter;
}

namespace Slic3r::App::Render {
class Device;
}

namespace Slic3r::Biz {
class ProjectInteractor;
class SlaHollowRequest;
} // namespace Slic3r::Biz

namespace Slic3r::App::Plater {


struct DrainHoleEditState
{
    SlaDrainHolesEditing editing;
    std::optional<size_t> dragged_hole_idx;
    Domain::Vec3d drag_start_world_pos;
    Domain::Vec3d drag_start_mesh_pos;
};

struct DrainHolePaintableVolume
{
    const Domain::ModelObject& model_object;
    const Domain::ModelInstance& model_instance;
    Domain::ModelVolume& model_volume;
    const Scene::TriangleMesh& scene_mesh;
    const Slic3r::AABBMesh& aabb_mesh;
    Domain::Transform3d world_trafo;
    Domain::Transform3d world_trafo_no_translate;
};

using DrainHolePaintableVolumes = std::vector<DrainHolePaintableVolume>;

class SlaHollowGizmo :
    public Scene::IToolGizmo,
    public Biz::Scene::ISceneSelectionChangedListener,
    public Biz::ISLAObjectCacheChangedListener
{
public:
    SlaHollowGizmo(
        PlaterScenePresenter& scene_presenter,
        Biz::ProjectInteractor& project_interactor,
        Render::Device& device
    );

    ~SlaHollowGizmo() override;

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

    void render_scene(Render::CommandBuffer& cmd_buffer) override;

private:
    void start_preview();
    void on_preview_completed(std::optional<Biz::Slicing::Sla::Object> sla_object);

    void clear_preview_visuals();
    void show_preview_mesh(const Biz::Slicing::Sla::Object& sla_object, const Domain::ModelInstance* instance);

    // Helper to read/write per-object config with undo snapshot
    bool read_hollowing_config(const Domain::ModelObject* model_object);
    void write_hollowing_config(Domain::ModelObject* model_object, bool enable, double min_thickness, double quality, double closing_distance);
    void take_undo_snapshot();

    // Drain hole editing helpers
    void begin_editing();
    void end_editing();
    void apply_edited_holes();
    void discard_edited_holes();
    std::optional<size_t> find_nearest_hole(const Domain::Vec3d& mesh_pos, double max_distance_mm) const;
    void add_hole_at_mesh_pos(const Domain::Vec3d& mesh_pos, const Domain::Vec3d& mesh_normal);
    void remove_hole_at_index(size_t idx);
    void move_hole_to_mesh_pos(size_t idx, const Domain::Vec3d& mesh_pos, const Domain::Vec3d& mesh_normal);
    void take_hole_undo_snapshot();

    // Visuals
    void update_hole_visuals();
    void clear_hole_visuals();
    Domain::ColorRGBA get_hole_color(const Domain::SLA::DrainHole& hole, bool highlighted) const;

    // Raycasting helpers (adapted from PaintOnGizmoBase)
    struct VolumeHitPoint
    {
        Domain::Vec3d volume_hit_position = Domain::Vec3d::Zero();
        Domain::Vec3d volume_hit_normal = Domain::Vec3d::UnitZ();
        int volume_idx = -1;
        size_t facet_idx = 0;
    };
    std::pair<Domain::Vec3d, Domain::Vec3d> hit_to_object_pos_normal(const VolumeHitPoint& hit) const;

    std::optional<VolumeHitPoint> raycast_mouse(const Domain::Vec2d& mouse_position) const;
    void collect_paintable_volumes(const Domain::SelectionId project_id, const Domain::ElementRef& element);

    // Selection helpers
    void select_hole(size_t idx, bool add_to_selection = false);
    void deselect_hole(size_t idx);
    void select_all_holes();
    void clear_hole_selection();
    void delete_selected_holes();
    void apply_radius_to_selected();
    void apply_height_to_selected();

    // Cone/cylinder visual
    void create_cylinder_geometry_if_needed();

    PlaterScenePresenter& m_scene_presenter;
    Biz::ProjectInteractor& m_project_interactor;
    Render::Device& m_device;
    std::unique_ptr<SlaHollowDialog> m_dialog;
    std::unique_ptr<Biz::SlaHollowRequest> m_hollow_request;
    std::optional<Domain::SlicingId> m_preview_slicing_id;
    Domain::ObjectID m_selected_object_id;
    Domain::SelectionId m_selected_instance_id{Domain::INVALID_ID};
    bool m_has_preview = false;
    Scene::IGizmoController* m_gizmo_controller = nullptr;

    // Scene nodes for preview visuals
    Scene::Node* m_main_node = nullptr;
    Scene::Node* m_preview_node = nullptr;
    Scene::Node* m_holes_node = nullptr;

    // Geometry and triangle mesh managers for preview mesh
    using GeometryManager = Render::GeometryManager<std::string>;
    using TriangleMeshManager = Scene::TriangleMeshManager<std::string>;
    GeometryManager m_geometry_manager{"sla_hollow_geometry"};
    TriangleMeshManager m_triangle_mesh_manager{"sla_hollow_mesh"};

    // Current config values
    bool m_current_enable = false;
    double m_current_min_thickness = 3.0;
    double m_current_quality = 0.5;
    double m_current_closing_distance = 2.0;
    double m_current_hole_radius = 5.0;
    double m_current_hole_height = 10.0;

    // Editing state
    std::optional<DrainHoleEditState> m_edit_state;

    // Paintable volumes for raycasting
    DrainHolePaintableVolumes m_paintable_volumes;

    // Scene nodes for hole visuals
    std::string m_cylinder_geometry_id = "drain_hole_cylinder";
    bool m_cylinder_geometry_created = false;

    // Hovered hole index (for highlight)
    std::optional<size_t> m_hovered_hole_idx;
};

} // namespace Slic3r::App::Plater