#pragma once

#include "Slic3r/App/Plater/SlaSupportPreviewSchedule.hpp"
#include "Slic3r/App/Render/GeometryManager.hpp"
#include "Slic3r/App/Scene/AuxiliaryElementId.hpp"
#include "Slic3r/App/Scene/TriangleMeshManager.hpp"
#include "Slic3r/Biz/ISelectedBedInstanceChangedListener.hpp"
#include "Slic3r/Biz/ISelectedConfigContainerChangedListener.hpp"
#include "Slic3r/Biz/ISelectedProjectChangedListener.hpp"
#include "Slic3r/Biz/ISlicingInputChangedListener.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/ObjectID.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
#include "jthread/JThread.hpp"
#include "libslic3r/SLASupportTool.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Slic3r::Domain {
class ModelInstance;
class Project;
} // namespace Slic3r::Domain

namespace Slic3r::App::Plater {
class PlaterScenePresenter;
} // namespace Slic3r::App::Plater

namespace Slic3r::App::Scene {
class Node;
} // namespace Slic3r::App::Scene

namespace Slic3r::App::Render {
class Device;
} // namespace Slic3r::App::Render

namespace Slic3r::App::Plater {

// Cheap fingerprint of everything the support tree of one object is built from: its support points,
// where the object sits and the resolved SLA configuration. An unchanged key always produces the
// same tree, so the drawn tree of such an object is still valid.
struct SlaSupportPreviewKey
{
    std::size_t            point_count{0};
    std::uint64_t          points_hash{0};
    std::array<double, 16> instance_matrix{};
    std::size_t            full_config_hash{0};
    std::size_t            object_config_hash{0};
    bool                   supports_enabled{false};

    bool operator==(const SlaSupportPreviewKey& rhs) const = default;
};

/// @brief Hash of the support points and every per-point dimension the support tree is built from
/// (the head, the pillar and the base sizes, the tip and the stem geometry), in the order the
/// points are stored.
std::uint64_t hash_support_points(const Domain::SLA::SupportPoints& points);

SlaSupportPreviewKey make_sla_support_preview_key(
    const Domain::SLA::SupportPoints& points,
    const Domain::Transform3d&        instance_matrix,
    std::size_t                       full_config_hash,
    std::size_t                       object_config_hash,
    bool                             supports_enabled
);

struct SlaSupportPreviewCandidate
{
    Domain::ObjectID         object_id;
    // False when the object must not show a preview at all (no points, supports off, off the plate).
    bool                    wants_preview{false};
    SlaSupportPreviewKey    key;
};

struct SlaSupportPreviewDiff
{
    // Objects whose tree has to be (re)built: the new ones and the ones whose key changed.
    std::vector<Domain::ObjectID> to_recompute;
    // Objects that lost their preview and whose nodes have to go away.
    std::vector<Domain::ObjectID> to_remove;
};

/// @brief Compares the objects that should have a preview now against the keys of the ones that
/// have (or are about to get) one. Objects that are gone or no longer want a preview are erased
/// from @p current_keys and reported in the result.
SlaSupportPreviewDiff diff_sla_support_previews(
    const std::vector<SlaSupportPreviewCandidate>&          candidates,
    std::unordered_map<std::size_t, SlaSupportPreviewKey>& current_keys
);

/**
 * @brief Keeps the SLA support tree and the raft of every model on the build plate visible in the
 * Prepare view, the way Chitubox and Lychee do, without slicing anything.
 *
 * The service watches the slicing input (support point edits, model moves, preset changes), the
 * project, the bed selection and the object selection. For every printable model object on the
 * selected build plate that has support points it builds the tree and the raft on a worker thread
 * and draws them standing on the plate, while PlaterScenePresenter lifts the model itself by the
 * same elevation. Objects without points, with supports turned off, or that left the plate lose
 * their preview. Nothing is sliced, the meshes come from the support tool engine call only.
 *
 * A burst of edits (dragging a support point, a slider changing a per point value) is collected by
 * SlaSupportPreviewSchedule and costs one build of the final state, and a key that changes while
 * a build runs stops it instead of letting it finish into the bin. When a build does start, the
 * main thread only snapshots the model's meshes (sla::support_tool_model_mesh), which shares them
 * instead of copying, so the cost does not scale with the size of the figure; merging, the AABB
 * and the tree itself are built on the worker. The main thread never waits for a build and never
 * holds more than the newest tree and raft of an object.
 */
class SlaSupportPreviewService :
    public Biz::ISlicingInputChangedListener,
    public Biz::Scene::ISceneSelectionChangedListener,
    public Biz::ISelectedBedInstancesChangedListener,
    public Biz::ISelectedProjectChangedListener,
    public Biz::ISelectedConfigContainerChangedListener
{
public:
    SlaSupportPreviewService(
        Biz::ProjectInteractor& project_interactor,
        PlaterScenePresenter&   scene_presenter,
        Render::Device&         device
    );
    ~SlaSupportPreviewService();

    SlaSupportPreviewService(const SlaSupportPreviewService&) = delete;
    SlaSupportPreviewService& operator=(const SlaSupportPreviewService&) = delete;

    struct ObjectSlaConfig
    {
        Domain::FullConfigSLAPtr       full;
        Domain::PartialObjectConfigSLAPtr object;
    };

    /// @brief Resolves the SLA configuration of one model object, shared with the support tool.
    std::optional<ObjectSlaConfig> build_object_sla_config(
        const Domain::Project&       project,
        const Domain::ModelObject*   model_object,
        const Domain::ModelInstance* instance
    ) const;

    /// @brief Whether the support tree (or the raft) of this object is drawn on the plate right now.
    /// The tree is built on a worker, so an object whose points were written a moment ago may have
    /// none yet; a caller that wants to know whether a support of it can be picked asks here (M2.35).
    [[nodiscard]] bool has_preview(Domain::ObjectID object_id) const;

    void on_slicing_input_changed(const Domain::BedRef& bed_instance) override;
    void on_slicing_input_removed(const Domain::BedRef& bed_instance) override;
    void on_scene_selection_changed(
        Domain::SelectionId                project_id,
        const Biz::Scene::ObjectSelection& selection
    ) override;
    void on_selected_bed_instances_changed(
        Domain::SelectionId             project_id,
        const Biz::Scene::BedSelection& bed_selection
    ) override;
    void on_selected_project_changed(size_t index) override;
    void on_selected_project_changed_final(size_t index) override;
    void on_selected_config_container_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId container_id
    ) override;

private:
    /// @brief Everything a build needs but the geometry, taken when the key changed and used when
    /// the debounce window is over. Cheap to copy, so it is taken on every edit of the burst; the
    /// geometry is not, it is snapshotted once the burst is.
    struct Pending
    {
        Domain::ObjectID                  object_id;
        Domain::Transform3d               instance_matrix{Domain::Transform3d::Identity()};
        Domain::SLA::SupportPoints        points;
        Domain::FullConfigSLAPtr          full_config;
        Domain::PartialObjectConfigSLAPtr object_config;
        Domain::Transform3d               bed_trafo{Domain::Transform3d::Identity()};
        double                            elevation{0.};
    };

    struct Job
    {
        SlaSupportPreviewSchedule::Request request;
        Pending                           pending;
        // The worker's view of the model: the meshes of its MODEL PART volumes, shared with the
        // model instead of copied (M2.21c), so the main thread stays O(volumes) however big the
        // figure is, and the worker reads data the main thread cannot change under it.
        Slic3r::sla::SupportToolModelMesh model_mesh;
    };

    struct ObjectPreview
    {
        SlaSupportPreviewKey key;
        Scene::Node*         node{nullptr};
        double               elevation{0.};
    };

    void refresh();
    void clear();
    void cancel_worker();
    void drop_object(Domain::ObjectID object_id);
    void drop_main_node();
    void start_due_build(const SlaSupportPreviewSchedule::Request& request);
    void start_next_job();
    void build_nodes(
        Domain::ObjectID                  object_id,
        const Domain::Transform3d&        bed_trafo,
        double                            elevation,
        const Slic3r::sla::SupportToolTree& tree
    );

    Biz::ProjectInteractor& m_project_interactor;
    PlaterScenePresenter&   m_scene_presenter;
    Render::Device&         m_device;

    Biz::ListenerScope<Biz::ISlicingInputChangedListener, Biz::Scene::SceneInteractor, SlaSupportPreviewService>
        m_scene_slicing_input_scope;
    Biz::ListenerScope<Biz::ISlicingInputChangedListener, Biz::Preset::PresetInteractor, SlaSupportPreviewService>
        m_preset_slicing_input_scope;
    Biz::ListenerScope<Biz::Scene::ISceneSelectionChangedListener, Biz::Scene::SceneInteractor, SlaSupportPreviewService>
        m_selection_scope;
    Biz::ListenerScope<Biz::ISelectedBedInstancesChangedListener, Biz::Scene::SceneInteractor, SlaSupportPreviewService>
        m_bed_selection_scope;
    Biz::ListenerScope<Biz::ISelectedProjectChangedListener, Biz::ProjectInteractor, SlaSupportPreviewService>
        m_project_scope;
    Biz::ListenerScope<Biz::ISelectedConfigContainerChangedListener, Biz::ProjectInteractor, SlaSupportPreviewService>
        m_config_container_scope;

    using SupportGeometryManager     = Render::GeometryManager<Scene::AuxiliaryElementId>;
    using SupportTriangleMeshManager = Scene::TriangleMeshManager<Scene::AuxiliaryElementId>;

    // Parent of every preview node, built in the scene of the project it belongs to.
    Scene::Node*        m_main_node{nullptr};
    Domain::SelectionId m_main_node_project_id{Domain::INVALID_ID};

    SupportGeometryManager     m_support_geometry_manager{"sla_support_preview_geometry"};
    SupportTriangleMeshManager m_support_mesh_manager{"sla_support_preview_mesh"};

    // One entry per object that has, or is about to get, a preview. The key is the one the tree is
    // (or will be) built from, so an object with an unchanged key is not rebuilt.
    std::unordered_map<std::size_t, ObjectPreview> m_previews;

    // What is waiting for its debounce window, at most one entry per object: the newest key
    // replaces the older one, so a burst of edits keeps a single build.
    std::unordered_map<std::size_t, Pending> m_pending;
    // The signature handed to the schedule per object. Bumped on every key change, so the result
    // of a build that an edit made stale is recognised and dropped.
    std::unordered_map<std::size_t, std::uint64_t> m_signatures;
    // The debounce and the cancellation of the running builds.
    std::unique_ptr<ISlaSupportPreviewTimer> m_debounce_timer;
    SlaSupportPreviewSchedule                m_schedule;
    std::deque<Job>                          m_queue;
    Biz::JThread::JThread                    m_worker;
    // True from the moment the worker is spawned until its result came back to the main thread,
    // so that only one tree is built at a time.
    std::atomic<bool> m_worker_running{false};
    // Cleared in the destructor, checked by everything dispatched to the main thread.
    std::shared_ptr<std::atomic<bool>> m_alive{std::make_shared<std::atomic<bool>>(true)};
};

} // namespace Slic3r::App::Plater
