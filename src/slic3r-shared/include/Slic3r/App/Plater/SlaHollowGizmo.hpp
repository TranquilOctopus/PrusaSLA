#pragma once

#include "Slic3r/App/Scene/IGizmo.hpp"
#include "Slic3r/App/Plater/GizmoWindow.hpp"
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
} // namespace Slic3r::Biz

namespace Slic3r::App::Plater {

struct SlaHollowRequest;

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

    PlaterScenePresenter& m_scene_presenter;
    Biz::ProjectInteractor& m_project_interactor;
    Render::Device& m_device;
    std::unique_ptr<SlaHollowDialog> m_dialog;
    std::unique_ptr<SlaHollowRequest> m_hollow_request;
    std::optional<Domain::SlicingId> m_preview_slicing_id;
    Domain::ObjectID m_selected_object_id;
    Domain::SelectionId m_selected_instance_id{Domain::INVALID_ID};
    bool m_has_preview = false;
    Scene::IGizmoController* m_gizmo_controller = nullptr;

    // Scene nodes for preview visuals
    Scene::Node* m_main_node = nullptr;
    Scene::Node* m_preview_node = nullptr;

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
};

} // namespace Slic3r::App::Plater