#pragma once

#include "Slic3r/App/Scene/IGizmo.hpp"
#include "Slic3r/App/Plater/GizmoWindow.hpp"
#include "Slic3r/App/Plater/SlaSupportAutoPresets.hpp"
#include "Slic3r/App/Plater/SlaSupportPointPick.hpp"
#include "Slic3r/App/Plater/SlaSupportPointsEditing.hpp"
#include "Slic3r/App/Plater/SlaSupportPointsSettings.hpp"
#include "Slic3r/App/Plater/SlaUndoAction.hpp"
#include "Slic3r/Biz/SLAObjectCache.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Domain/ElementRef.hpp"
#include "Slic3r/Domain/ObjectID.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/App/Scene/TriangleMeshManager.hpp"
#include "Slic3r/App/Render/GeometryManager.hpp"
#include "Slic3r/Biz/Algorithms/AABBMesh.hpp"
#include "Slic3r/App/Scene/Clipper.hpp"
#include "Slic3r/App/Scene/ClipperPresenter.hpp"
#include "Slic3r/App/Plater/SlaSupportPreviewService.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "jthread/JThread.hpp"
#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"

#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <deque>

namespace Slic3r::App::Plater {
class SlaSupportPointsDialog;
class PlaterScenePresenter;
class SlaSupportPreviewService;
}

namespace Slic3r::App::Render {
class Device;
}

namespace Slic3r::Biz {
class ProjectInteractor;
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
    // Every one of these is a view on something the model owns: the volumes hold references to the
    // model object, the instance and the volume, and the AABBMesh is a view on the mesh of the
    // volume rather than a copy of it. An undo replaces the whole model (SceneInteractor::set_state
    // moves a new one in), which destroys every object named above, and the manager may release the
    // scene mesh of a volume that is gone. The tool therefore rebuilds this list whenever the model
    // is reloaded, so nothing here outlives what it names. (M0.15)
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
    public Biz::Scene::ISceneChangedListener,
    public Biz::ISLAObjectCacheChangedListener
{
public:
    SlaSupportPointsGizmo(
        PlaterScenePresenter& scene_presenter,
        Biz::ProjectInteractor& project_interactor,
        Render::Device& device,
        SlaSupportPreviewService& support_preview_service
    );

    ~SlaSupportPointsGizmo() override;

    Scene::ToolType type() const override;
    bool supports_printer(Domain::PrinterTechnology pt) const override;
    bool enabled() const override;

    void on_activated() override;
    void on_deactivated() override;

    // The project being switched away from is the other way the tool ends: the pending points are
    // applied on the way out, like every other way out (M2.31). Every other gizmo answers these two
    // by calling on_activated / on_deactivated, so the tool follows.
    void on_project_activated(size_t new_project_id) override;
    void on_project_deactivated(size_t old_project_id) override;

    void on_scene_selection_changed(
        Domain::SelectionId project_id,
        const Biz::Scene::ObjectSelection& selection
    ) override;

    void on_sla_object_cache_changed(const Domain::SlicingId& id, Domain::ObjectID object_id) override;

    // An undo rebuilds the model, so the volumes the tool raycasts on are rebuilt from the new one
    // here (M0.15). Without it the tool would keep the volumes of the model the undo replaced.
    void on_model_reloaded(Domain::SelectionId project_id) override;

    void provide_gizmo_controller(Scene::IGizmoController& controller) override;

    Scene::GizmoActivationState on_mouse(Scene::GizmoEventContext& ctx, bool only_active) override;

    // A double click on a drawn support tree opens this tool on the model of that support, with the
    // clicked support selected, so its "Selected supports" group (M2.33) shows the values to change
    // (M2.35). Outside the tool only: while the tool is open the click is a click of the tool
    // (on_mouse), which picks the point of the drawn tree as well.
    bool allows_activation_by_double_click(const Scene::GizmoEventContext& ctx) override;

    std::unique_ptr<GizmoWindow> release_ui_window() override;

    void provide_clipper(Scene::Clipper& clipper);

    /**
     * @brief The Auto support action, on the given models or on every printable model of the
     * project when @p object_ids is empty. Runs the generator for them one after another, asks
     * once whether to keep existing points, and writes the result into the models.
     *
     * The tool has to be active, the generation belongs to it, and the Preview sidebar (M2.17d4)
     * opens it here through Navigator::run_sla_auto_support.
     *
     * @return false when the run was not started, because the tool is not active, one is already
     * going or none of the models can be supported.
     */
    bool auto_support(const std::vector<Domain::ObjectID>& object_ids = {});

    /// Whether a generation started by auto_support() or by Generate is still going.
    bool auto_support_running() const
    {
        return m_points_job_running || !m_auto_support_queue.empty();
    }

    void render_scene(Render::CommandBuffer& cmd_buffer) override;
    void on_keyboard(Scene::GizmoKeyEventContext& ctx) override;

private:
    void start_generation();
    void process_auto_support_queue();
    void on_auto_support_completed(Domain::ObjectID obj_id, std::optional<Domain::SLA::SupportPoints> support_points);
    void on_generation_completed(std::optional<Domain::SLA::SupportPoints> support_points);
    void apply_generated_points();
    // What every path that leaves the tool does with the points a generation produced: it applies
    // them, exactly as the Apply button does, undo snapshot included (M2.31). Discard has cleared
    // them by then, so it writes nothing.
    void apply_pending_points_on_leaving();
    void discard_generated_points();

    // The tool's own "Remove all points" (M2.32): takes every support point of the model the tool
    // works on away, after asking how many go, in one undo snapshot. It drops the points that are
    // only waiting to be applied and the edit session of the tool first, so nothing of them can come
    // back over the cleared model.
    void remove_all_points();
    void remove_all_points_now();

    // Editing helpers
    void begin_editing();
    void end_editing();
    void apply_edited_points();
    void discard_edited_points();
    void commit_edited_points_live();
    // The edit session takes the points of the model again when something other than the session
    // wrote them: a generation, Auto support all, Apply, an undo (M2.39d).
    void reload_edit_points_from_model();
    void add_point_at_mesh_pos(const Domain::Vec3d& mesh_pos);
    void remove_point_at_index(size_t idx);
    void move_point_to_mesh_pos(size_t idx, const Domain::Vec3d& mesh_pos);
    void take_undo_snapshot();
    void take_undo_snapshot_for_value_edit();
    void on_value_editing_started();
    void on_value_editing_ended();

    // Visuals
    void update_point_visuals();
    void clear_point_visuals();
    // Colour state of a point glyph (M2.9c): the point's own type token at rest, the hovered
    // token while the pointer is on it and the selected token while it is part of the selection.
    enum class PointGlyphState { Resting, Hovered, Selected };
    Domain::ColorRGBA get_point_color(const Domain::SLA::SupportPoint& point, PointGlyphState state) const;

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

    // What the pointer is on (M2.35). A click used to be tested against the surface of the model
    // alone, which left a support point under an overhang reachable only by looking at it from below
    // the model, and made a click on a marker that stands in front of that surface miss. The drawn
    // markers are picked on the screen first, the drawn tree of a point next, and only a click that
    // hits neither falls back to the surface of the model.
    void collect_point_markers(std::vector<SlaSupportPointMarker>& out_markers) const;
    // The drawn pieces of the support tree, one per support point. With @p whole_plate they are
    // collected for every model on the plate that has support points (the double click that opens the
    // tool on a support), otherwise for the object the tool works on.
    void collect_tree_parts(std::vector<SlaSupportTreePart>& out_parts, bool whole_plate) const;
    std::optional<SlaSupportPointTarget> point_at(const Domain::Vec2d& cursor) const;
    // Raycast against the real support tree mesh and pick the support point whose head is nearest.
    std::optional<std::pair<size_t, Domain::Vec3d>> raycast_tree_mesh(const Domain::Vec2d& cursor) const;
    // Selects the point a double click on a drawn support opened the tool on (M2.35).
    void open_on_picked_point();

    // Clipping plane
    void update_clipping_plane();
    void reset_clipping_plane();

    // Selection helpers
    void select_point(size_t idx, bool add_to_selection = false);
    void deselect_point(size_t idx);
    void select_all_points();
    void clear_selection();
    void delete_selected_points();

    // The two groups of the support settings (M2.33). "New supports" is what a clicked point takes
    // and changes no point that is already there, "Selected supports" is what the selected points
    // carry and only ever changes those. One field set used to do both jobs at once.
    void apply_new_support_setting(SlaSupportPointField field, double value);
    void apply_new_support_preset(int preset_index);
    void apply_selected_support_setting(SlaSupportPointField field, double value);
    void apply_selected_support_preset(int preset_index);
    void refresh_new_support_values();
    void update_selected_support_values();
    // The tip diameter, tip shape, tip length, knot, stem cross-section and stem taper a new point
    // takes, from the Supports & raft settings of @p model_object.
    SlaSupportGeometry support_geometry_defaults(const Domain::ModelObject* model_object) const;
    SlaSupportPreset get_support_preset_values(const std::string& preset_name) const;

    // What every point of a generation takes: the tip shape, tip length, knot, stem cross-section,
    // stem taper and foot shape of the Supports & raft settings of @p model_object, and then the
    // tip class of the role the point carries, with the two settings of the automatic placement
    // (support_auto_heavy_base, support_auto_detail_preset) deciding what a role is given
    // (M2.37, M7.8.8). Both generation paths (Generate and Auto support) call this and nothing
    // else, so a generated point is the same support whichever of the two made it.
    void fill_generated_point_geometry(
        Domain::SLA::SupportPoints& points,
        const Domain::ModelObject* model_object,
        const Domain::ModelInstance* instance
    );
    // The two settings of "Supports & raft" that say what the automatic placement puts where
    // (support_auto_heavy_base and support_auto_detail_preset), read off the selected print preset,
    // the same place the preset dimensions come from.
    SlaAutoSupportChoice auto_support_preset_choice() const;

    // Rectangle selection
    void start_rectangle_selection(const Domain::Vec2d& mouse_pos, bool is_add);
    void update_rectangle_selection(const Domain::Vec2d& mouse_pos);
    void finish_rectangle_selection();
    void project_points_to_screen(std::vector<Domain::Vec2d>& out_screen_positions) const;

    // Cone visual
    void create_cone_geometry_if_needed();

    // Config and elevation helpers
    using ObjectSlaConfig = SlaSupportPreviewService::ObjectSlaConfig;
    std::optional<ObjectSlaConfig> build_object_sla_config(const Domain::ModelObject* model_object, const Domain::ModelInstance* instance) const;
    double support_elevation() const;

    // The lift of the model the tool works on (M2.33). The scene draws a model lifted only when the
    // M2.21 support preview asks for it, and it does that for an object that has support points and
    // supports on. The tool raises its object while it is open, the way Chitubox raises the model
    // while the supports are edited, so a click lands on the surface as it is drawn instead of on a
    // copy of the mesh 5 mm above it, and there is no jump when the first point is added: by then the
    // preview service lifts the model by the same elevation.
    void refresh_tool_lift();
    void release_tool_lift();
    /// The lift the scene draws the object of the tool with, which is the one the raycast and the
    /// point glyphs use. Never a separately computed one, or the two could disagree.
    double applied_lift() const;
    /// Rebuilds the paintable volumes when the lift the scene applies changed, so every raycast
    /// tests the model as it is drawn.
    void sync_paintable_lift();
    // The transform the tool draws the object of with: the instance transform with the lift the
    // scene applies to it (M2.33). One value for the point glyphs, the raycast and the picking of the
    // points and of their drawn tree (M2.35), so all three of them test what is on the screen.
    Domain::Transform3d object_drawing_trafo() const;

    // Worker helpers
    enum class WorkerJobType { Points };
    struct WorkerJobData {
        std::unique_ptr<Domain::ModelObject> cloned_object;
        Domain::Transform3d instance_matrix;
        ObjectSlaConfig config;
        Domain::ObjectID object_id;
        Domain::SelectionId instance_id;
        Domain::SlicingId slicing_id;
        WorkerJobType job_type;
        bool for_auto_support_all = false;
        size_t job_counter = 0;
    };
    struct WorkerJobResult {
        std::optional<Domain::SLA::SupportPoints> points; // for Points job
        Domain::ObjectID object_id;
        Domain::SelectionId instance_id;
        Domain::SlicingId slicing_id;
        WorkerJobType job_type;
        bool for_auto_support_all = false;
        size_t job_counter = 0;
        bool cancelled = false;
    };
    void start_worker_job(WorkerJobData&& job_data);
    void cancel_worker_job();
    void on_worker_job_completed(WorkerJobResult&& result);
    void on_points_job_completed(std::optional<Domain::SLA::SupportPoints> points, Domain::ObjectID object_id, bool for_auto_support_all, size_t job_counter);

    PlaterScenePresenter& m_scene_presenter;
    Biz::ProjectInteractor& m_project_interactor;
    Render::Device& m_device;
    SlaSupportPreviewService& m_support_preview_service;
    Yoga::Passthrough<SlaSupportPointsDialog> m_dialog;

    // Worker state
    std::optional<WorkerJobData> m_active_job;
    Biz::JThread::JThread m_worker;
    size_t m_job_counter = 0;
    bool m_points_job_running = false;
    bool m_gizmo_active = false;

    // The project the tool works on, which is the project the pending points belong to when the tool
    // is being left (M2.31).
    Domain::SelectionId m_project_id{Domain::INVALID_ID};
    Domain::ObjectID m_selected_object_id;
    Domain::SelectionId m_selected_instance_id{Domain::INVALID_ID};
    // The object the tool works on, which the paintable volumes are rebuilt from whenever the lift
    // the scene applies to it changes (M2.33).
    Domain::ElementRef m_selected_element;
    // The lift the paintable volumes were built with, and whether the tool is the one holding it (so
    // only it gives it back, an object the preview service lifts keeps the service's own lift).
    double m_applied_lift{0.};
    bool m_tool_owns_lift{false};
    std::optional<Domain::SLA::SupportPoints> m_generated_support_points;
    bool m_has_generated_points = false;
    Scene::IGizmoController* m_gizmo_controller = nullptr;

    // Editing state
    std::optional<SupportPointEditState> m_edit_state;
    Domain::SLA::SupportPoints m_points_before_edit;
    Domain::SLA::PointsStatus m_status_before_edit{};

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

    // The piece of a drawn tree a double click found while the tool was closed, and the support the
    // tool is opened on with (M2.35). Set while the tool is still closed, taken in on_activated.
    std::optional<SlaSupportTreePart> m_pending_open_pick;

    // Auto support all queue
    std::deque<Domain::ObjectID> m_auto_support_queue;
    std::optional<bool> m_auto_support_keep_existing;

    // Raycast all support tree meshes on the plate and pick the nearest hit (M2.39b).
    std::optional<SlaSupportTreePart> raycast_all_tree_meshes(
        const Scene::GizmoEventContext& ctx,
        const Domain::Vec2d& cursor,
        std::optional<double>& out_tree_hit_distance_mm) const;

    // Guard to prevent dialog setters from triggering value-change callbacks
    bool m_syncing_dialog{false};

    // The value edit of a slider that is running, so the ticks of a drag become one undo step
    // (M2.6b).
    SlaUndoAction m_value_edit_action;

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