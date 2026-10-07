#include "Slic3r/App/Plater/SlaSupportPreviewService.hpp"

#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/IsSlaActive.hpp"
#include "Slic3r/App/Plater/PlaterSceneLayer.hpp"
#include "Slic3r/App/Plater/PlaterScenePresenter.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Render/GeometryBuilder.hpp"
#include "Slic3r/App/Scene/Node.hpp"
#include "Slic3r/App/Scene/NodeBuilder.hpp"
#include "Slic3r/App/Scene/Scene.hpp"
#include "Slic3r/App/Theme.hpp"
#include "Slic3r/App/ThemeTypes.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/ConfigContainer.hpp"
#include "Slic3r/Domain/ModelInstance.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/Domain/SlicingId.hpp"
#include "Slic3r/Domain/Transformation.hpp"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>
#include <unordered_set>

using Slic3r::Domain::ColorRGBA;
using Slic3r::Domain::ObjectID;
using Slic3r::Domain::SlicingId;
using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Vec3d;

namespace Slic3r::App::Plater {

std::uint64_t hash_support_points(const Domain::SLA::SupportPoints& points)
{
    std::uint64_t seed = points.size();
    auto mix          = [&seed](std::uint64_t value) {
        seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    };
    auto mix_float = [&mix](float value) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        mix(bits);
    };

    for (const Domain::SLA::SupportPoint& point : points) {
        mix_float(point.pos.x());
        mix_float(point.pos.y());
        mix_float(point.pos.z());
        mix_float(point.head_front_radius);
        // Every dimension the support tree reads off the point, so that a change
        // which only moves the pillar or the base (a support preset, see M2.18c)
        // or only reshapes the head or the stem (the tip shape, the knot, the
        // stem sides and the stem taper, see M2.16b) still refreshes the preview
        // geometry.
        mix_float(point.tip_length);
        mix_float(point.contact_depth);
        mix_float(point.pillar_diameter);
        mix_float(point.base_diameter);
        mix_float(point.base_height);
        // The same for the per-point tip and stem geometry (M2.16c): a change of the tip shape, the
        // knot, the stem cross-section or the stem taper is a new tree as well. The foot the
        // pillar gets (M2.23) is part of it: another shape is another base.
        mix(static_cast<std::uint64_t>(point.tip_shape));
        mix(static_cast<std::uint64_t>(point.base_shape));
        mix_float(point.knot_radius);
        mix(static_cast<std::uint64_t>(point.stem_sides));
        mix_float(point.stem_taper);
        // A point that may or may not end on the model (M2.26) gets a pillar to the
        // plate or a model anchor, so it is another tree as well.
        mix(static_cast<std::uint64_t>(point.on_model));
        // And so does one that may or may not be braced (M2.38): the braces of a pillar are
        // part of the tree.
        mix(static_cast<std::uint64_t>(point.brace));
    }
    return seed;
}

SlaSupportPreviewKey make_sla_support_preview_key(
    const Domain::SLA::SupportPoints& points,
    const Domain::Transform3d&        instance_matrix,
    std::size_t                       full_config_hash,
    std::size_t                       object_config_hash,
    bool                             supports_enabled
)
{
    SlaSupportPreviewKey key;
    key.point_count        = points.size();
    key.points_hash        = hash_support_points(points);
    const auto matrix      = instance_matrix.matrix();
    for (std::size_t i = 0; i < 16; ++i) {
        key.instance_matrix[i] = matrix(i / 4, i % 4);
    }
    key.full_config_hash   = full_config_hash;
    key.object_config_hash = object_config_hash;
    key.supports_enabled   = supports_enabled;
    return key;
}

SlaSupportTreePlacement sla_support_tree_placement(const Domain::Transform3d& instance_matrix, double lift)
{
    // One convention, on both sides (M2.34): the engine puts the object where object_to_world says
    // and returns the tree in that same world frame with the object NOT lifted
    // (libslic3r/SLASupportTool.hpp), and the scene draws the model by translation(0, 0, lift) *
    // instance_matrix (PlaterScenePresenter::instance_transform). The object is placed with the
    // instance matrix already, so the node the tree hangs from has to add the lift and nothing
    // else: the instance matrix is a world matrix and it already carries the offset of the build
    // plate, so a plate transform here would move the tree that far away from its own model.
    SlaSupportTreePlacement placement;
    placement.object_to_world = instance_matrix;
    placement.node_trafo      = lift == 0. ? Domain::Transform3d::Identity()
                                          : Domain::translation_transform(Domain::Vec3d(0., 0., lift));
    return placement;
}

SlaSupportPreviewDiff diff_sla_support_previews(
    const std::vector<SlaSupportPreviewCandidate>&          candidates,
    std::unordered_map<std::size_t, SlaSupportPreviewKey>& current_keys
)
{
    SlaSupportPreviewDiff diff;
    std::unordered_set<std::size_t> wanted;
    wanted.reserve(candidates.size());

    for (const SlaSupportPreviewCandidate& candidate : candidates) {
        const std::size_t id = candidate.object_id.id;
        wanted.insert(id);

        const auto it = current_keys.find(id);
        if (!candidate.wants_preview) {
            if (it != current_keys.end()) {
                current_keys.erase(it);
                diff.to_remove.push_back(candidate.object_id);
            }
            continue;
        }

        if (it == current_keys.end()) {
            current_keys.emplace(id, candidate.key);
            diff.to_recompute.push_back(candidate.object_id);
        } else if (!(it->second == candidate.key)) {
            it->second = candidate.key;
            diff.to_recompute.push_back(candidate.object_id);
        }
    }

    for (auto it = current_keys.begin(); it != current_keys.end();) {
        if (wanted.contains(it->first)) {
            ++it;
            continue;
        }
        diff.to_remove.push_back(ObjectID{it->first});
        it = current_keys.erase(it);
    }

    return diff;
}

SlaSupportPreviewService::SlaSupportPreviewService(
    Biz::ProjectInteractor& project_interactor,
    PlaterScenePresenter&   scene_presenter,
    Render::Device&         device
) :
    m_project_interactor(project_interactor),
    m_scene_presenter(scene_presenter),
    m_device(device),
    m_scene_slicing_input_scope(project_interactor.scene_interactor(), *this),
    m_preset_slicing_input_scope(project_interactor.preset_interactor(), *this),
    m_selection_scope(project_interactor.scene_interactor(), *this),
    m_bed_selection_scope(project_interactor.scene_interactor(), *this),
    m_project_scope(project_interactor, *this),
    m_config_container_scope(project_interactor, *this),
    m_debounce_timer(SlaSupportPreviewSchedule::platform_timer()),
    m_schedule(*m_debounce_timer, [this](const SlaSupportPreviewSchedule::Request& request) {
        start_due_build(request);
    })
{
    refresh();
}

SlaSupportPreviewService::~SlaSupportPreviewService()
{
    m_alive->store(false);
    // The schedule asks the worker to stop before the destructor joins it below.
    m_schedule.clear();
    m_pending.clear();
    cancel_worker();
    drop_main_node();
    m_scene_presenter.clear_sla_lifts();
}

std::optional<SlaSupportPreviewService::ObjectSlaConfig> SlaSupportPreviewService::build_object_sla_config(
    const Domain::Project&       project,
    const Domain::ModelObject*   model_object,
    const Domain::ModelInstance* instance
) const
{
    if (model_object == nullptr || instance == nullptr) {
        return std::nullopt;
    }

    const Domain::BedRef bed_ref              = instance->get_last_bed();
    const Domain::ConfigContainer* cc        = project.find_config_container(bed_ref.config_container_id);
    if (cc == nullptr) {
        return std::nullopt;
    }

    Domain::ConfigPack pack = cc->build_print_config();
    if (!std::holds_alternative<Domain::ConfigPackSLA>(pack)) {
        return std::nullopt;
    }

    const Domain::Preset::HwPrinterConfig& hw_config = cc->selected_preset().hw_config;
    ObjectSlaConfig config;
    config.full   = std::make_shared<const Domain::FullConfigSLA>(std::get<Domain::ConfigPackSLA>(pack), hw_config);
    config.object = std::make_shared<const Domain::PartialObjectConfigSLA>(model_object->object_settings_sla, hw_config);
    return config;
}

bool SlaSupportPreviewService::has_preview(Domain::ObjectID object_id) const
{
    // An entry without a node is an object whose tree is about to be built (or was just dropped), so
    // nothing of it is drawn to be picked (M2.35).
    const auto it = m_previews.find(object_id.id);
    return it != m_previews.end() && it->second.node != nullptr;
}

std::optional<Domain::sla::RaftType> SlaSupportPreviewService::auto_raft_type(
    Domain::ObjectID object_id
) const
{
    const auto it = m_previews.find(object_id.id);
    if (it == m_previews.end())
        return std::nullopt;
    return it->second.auto_raft;
}

const Scene::TriangleMesh* SlaSupportPreviewService::support_tree_mesh(
    Domain::ObjectID object_id
) const
{
    const auto it = m_previews.find(object_id.id);
    if (it == m_previews.end() || it->second.node == nullptr) {
        return nullptr;
    }
    const Scene::AuxiliaryElementId support_id{Scene::AuxiliaryElementId::Type::SlaSupports, object_id.id};
    const auto* trimesh = m_support_mesh_manager.get(support_id);
    if (!trimesh) {
        return nullptr;
    }
    return trimesh;
}

void SlaSupportPreviewService::on_slicing_input_changed(const Domain::BedRef& /*bed_instance*/)
{
    refresh();
}

void SlaSupportPreviewService::on_slicing_input_removed(const Domain::BedRef& /*bed_instance*/)
{
    refresh();
}

void SlaSupportPreviewService::on_scene_selection_changed(
    Domain::SelectionId               project_id,
    const Biz::Scene::ObjectSelection& /*selection*/
)
{
    if (project_id == m_project_interactor.selected_project_id()) {
        refresh();
    }
}

void SlaSupportPreviewService::on_selected_bed_instances_changed(
    Domain::SelectionId             project_id,
    const Biz::Scene::BedSelection& /*bed_selection*/
)
{
    if (project_id == m_project_interactor.selected_project_id()) {
        refresh();
    }
}

void SlaSupportPreviewService::on_selected_project_changed(size_t /*index*/)
{
    clear();
    refresh();
}

void SlaSupportPreviewService::on_selected_project_changed_final(size_t /*index*/)
{
    // The config container and the bed are only complete after the final callback.
    refresh();
}

void SlaSupportPreviewService::on_selected_config_container_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId /*container_id*/
)
{
    if (project_id == m_project_interactor.selected_project_id()) {
        refresh();
    }
}

void SlaSupportPreviewService::refresh()
{
    if (!m_alive->load()) {
        return;
    }

    const Domain::SelectionId project_id = m_project_interactor.selected_project_id();
    if (!App::is_sla_active(m_project_interactor) || !m_project_interactor.project_exists(project_id)) {
        clear();
        return;
    }

    // The preview nodes live in the scene of the project they were built for.
    if (m_main_node != nullptr && m_main_node_project_id != project_id) {
        drop_main_node();
    }

    const Domain::Project& project   = m_project_interactor.project(project_id);
    const SlicingId slicing_id       = m_project_interactor.selected_bed_slicing_id();
    const Domain::BedInstance* bed   = slicing_id.project_id == project_id
        ? project.find_bed_instance_by_id(slicing_id.bed_instance_id)
        : nullptr;

    struct Candidate
    {
        SlaSupportPreviewCandidate    public_part;
        const Domain::ModelObject*    model_object{nullptr};
        const Domain::ModelInstance*  instance{nullptr};
        ObjectSlaConfig                config;
        double                         elevation{0.};
    };

    std::vector<Candidate>        candidates;
    std::unordered_set<std::size_t> seen;

    if (bed != nullptr) {
        for (const Domain::ModelInstance* instance : bed->model_instances) {
            if (instance == nullptr || !instance->is_printable()) {
                continue;
            }
            const Domain::ModelObject* model_object = instance->get_object();
            if (model_object == nullptr || !seen.insert(model_object->id().id).second) {
                continue;
            }

            Candidate candidate;
            candidate.model_object = model_object;
            candidate.instance     = instance;
            candidate.public_part.object_id = model_object->id();

            const std::optional<ObjectSlaConfig> config = build_object_sla_config(project, model_object, instance);
            if (config.has_value() && !model_object->sla_support_points.empty()) {
                // The object settings win over the printer configuration, so ask the merged view.
                Domain::ConfigView config_view{config->full, {config->object}};
                const std::size_t full_config_hash = config_view.hash();
                config_view.finalize();

                const auto& values = config_view.values();
                const auto supports_it = values.find("supports_enable");
                const bool supports_enabled = supports_it != values.end() && supports_it->second.get<bool>();

                candidate.public_part.wants_preview = supports_enabled;
                candidate.public_part.key            = make_sla_support_preview_key(
                    model_object->sla_support_points,
                    instance->get_matrix(),
                    full_config_hash,
                    config->object->hash(),
                    supports_enabled
                );
                candidate.elevation =
                    Slic3r::sla::support_tool_elevation(config->full, config->object);
                candidate.config    = *config;
            }

            candidates.push_back(std::move(candidate));
        }
    }

    std::vector<SlaSupportPreviewCandidate> public_candidates;
    public_candidates.reserve(candidates.size());
    for (const Candidate& candidate : candidates) {
        public_candidates.push_back(candidate.public_part);
    }

    // The keys of the objects that keep their preview, plus the diff against the plate.
    std::unordered_map<std::size_t, SlaSupportPreviewKey> current_keys;
    current_keys.reserve(m_previews.size());
    for (const auto& [id, preview] : m_previews) {
        current_keys.emplace(id, preview.key);
    }

    const SlaSupportPreviewDiff diff = diff_sla_support_previews(public_candidates, current_keys);

    for (const ObjectID& object_id : diff.to_remove) {
        drop_object(object_id);
    }

    for (const Candidate& candidate : candidates) {
        if (!candidate.public_part.wants_preview) {
            continue;
        }
        ObjectPreview& preview = m_previews[candidate.public_part.object_id.id];
        preview.key            = candidate.public_part.key;
        // The model is drawn by this lift, and the tree of it is built and drawn by the very same
        // one (sla_support_tree_placement), so the two can never drift apart (M2.34).
        // Which raft the Auto rule picks decides that lift too (M7.8.4): a part that stands on the
        // plate is drawn there, one held up by its supports is drawn above them. The lift of a
        // build that has not come back yet is the one an unresolved Auto gives, which is the no raft
        // of R6.1, and build_nodes replaces it with the lift the tree was built for.
        m_scene_presenter.set_sla_lift(
            candidate.public_part.object_id,
            preview.elevation_mm.has_value() ? *preview.elevation_mm : candidate.elevation
        );
    }

    for (const ObjectID& object_id : diff.to_recompute) {
        const auto it = std::find_if(candidates.begin(), candidates.end(), [&](const Candidate& candidate) {
            return candidate.public_part.object_id == object_id;
        });
        if (it == candidates.end() || !it->public_part.wants_preview) {
            continue;
        }

        // The geometry is not snapshotted here: a burst of edits of this object keeps one build (the
        // debounce of SlaSupportPreviewSchedule), so the snapshot is taken once the burst is over.
        // Everything below it is cheap enough to take on every edit.
        Pending pending;
        pending.object_id       = object_id;
        pending.placement       = sla_support_tree_placement(it->instance->get_matrix(), it->elevation);
        pending.points          = it->model_object->sla_support_points;
        pending.full_config     = it->config.full;
        pending.object_config   = it->config.object;

        m_pending[object_id.id] = std::move(pending);
        m_schedule.request({object_id, ++m_signatures[object_id.id]});
    }

    start_next_job();
}

void SlaSupportPreviewService::clear()
{
    for (const auto& entry : m_previews) {
        m_scene_presenter.set_sla_lift(ObjectID{entry.first}, 0.);
    }
    m_previews.clear();
    m_pending.clear();
    // Asks the running build to stop, then cancel_worker() below joins it.
    m_schedule.clear();
    cancel_worker();
    drop_main_node();
}

void SlaSupportPreviewService::drop_object(ObjectID object_id)
{
    const auto it = m_previews.find(object_id.id);
    if (it != m_previews.end()) {
        if (it->second.node != nullptr) {
            m_scene_presenter.scene().remove_child(it->second.node);
            it->second.node = nullptr;
        }
        m_previews.erase(it);
    }

    const Scene::AuxiliaryElementId support_id{Scene::AuxiliaryElementId::Type::SlaSupports, object_id.id};
    const Scene::AuxiliaryElementId pad_id{Scene::AuxiliaryElementId::Type::SlaPad, object_id.id};
    m_support_geometry_manager.release(support_id);
    m_support_geometry_manager.release(pad_id);
    m_support_mesh_manager.release(support_id);
    m_support_mesh_manager.release(pad_id);

    m_scene_presenter.set_sla_lift(object_id, 0.);

    // Whatever is waiting for or building this object is not wanted anymore.
    m_pending.erase(object_id.id);
    m_schedule.forget(object_id);
}

void SlaSupportPreviewService::drop_main_node()
{
    if (m_main_node != nullptr) {
        if (m_scene_presenter.has_project(m_main_node_project_id)) {
            m_scene_presenter.project_scene(m_main_node_project_id).remove_child(m_main_node);
        }
        m_main_node = nullptr;
    }
    m_main_node_project_id = Domain::INVALID_ID;
    m_support_geometry_manager.release_all();
    m_support_mesh_manager.release_all();
}

void SlaSupportPreviewService::cancel_worker()
{
    m_queue.clear();
    if (m_worker.joinable()) {
        m_worker.request_stop();
        m_worker.join();
    }
    m_worker_running = false;
    // The worker is gone without a result of its own (the project changed, or the service is
    // going), so the schedule must not wait for one either.
    m_schedule.finish_build();
}

void SlaSupportPreviewService::start_due_build(const SlaSupportPreviewSchedule::Request& request)
{
    const auto pending = m_pending.find(request.object_id.id);
    if (pending == m_pending.end() || pending->second.object_id != request.object_id) {
        return; // the object lost its preview while its window ran
    }

    Pending snapshot = std::move(pending->second);
    m_pending.erase(pending);

    if (!m_schedule.is_current(request)) {
        return; // a newer key took over this object, its own request is waiting
    }

    // The object may be gone by now; the request only remembers what it was built from.
    const Domain::SelectionId project_id = m_project_interactor.selected_project_id();
    if (!m_project_interactor.project_exists(project_id)) {
        return;
    }
    const Domain::ModelObject* model_object = m_project_interactor.project(project_id).find_object_by_id(request.object_id.id);
    if (model_object == nullptr || model_object->id() != request.object_id) {
        return;
    }

    Job job;
    job.request    = request;
    job.pending    = std::move(snapshot);
    // The only thing read out of the model for the worker: one shared pointer and one matrix per
    // volume. Not a vertex, and no config or facet either, so a 50-100 MB figure does not stall the
    // UI here (M2.21c); the worker copies what it needs, and only what it needs, on its own thread.
    job.model_mesh = Slic3r::sla::support_tool_model_mesh(*model_object);
    m_queue.push_back(std::move(job));

    start_next_job();
}

void SlaSupportPreviewService::start_next_job()
{
    if (!m_alive->load() || m_worker_running) {
        return;
    }

    // A job whose key changed again while it waited is stale: it is dropped, not built. A burst of
    // edits therefore costs the build of its final state only.
    std::erase_if(m_queue, [this](const Job& job) { return !m_schedule.is_current(job.request); });
    if (m_queue.empty()) {
        return;
    }

    if (m_worker.joinable()) {
        m_worker.join();
    }

    Job job = std::move(m_queue.front());
    m_queue.pop_front();
    m_worker_running = true;

    // The stop of the worker thread is the stop SlaSupportTool accepts: a newer key for this
    // object ends the build instead of letting it finish into the bin.
    m_schedule.begin_build(job.request, [this]() {
        if (m_worker.joinable()) {
            m_worker.request_stop();
        }
    });

    m_worker = Biz::JThread::JThread(
        [this](Biz::JThread::StopToken stop_token, Job worker_job) mutable {
            const Slic3r::sla::SupportToolStop stop = [&stop_token]() { return stop_token.stop_requested(); };

            Slic3r::sla::SupportToolTree tree = Slic3r::sla::build_support_tree_for_tool(
                worker_job.model_mesh,
                worker_job.pending.placement.object_to_world,
                worker_job.pending.points,
                worker_job.pending.full_config,
                worker_job.pending.object_config,
                stop
            );
            const bool cancelled = stop_token.stop_requested();

            const SlaSupportPreviewSchedule::Request request = worker_job.request;
            const ObjectID object_id                        = worker_job.request.object_id;
            const std::shared_ptr<std::atomic<bool>> alive   = m_alive;

            const bool dispatched = Biz::Platform::PlatformServices::instance()
                                       .main_thread_dispatcher()
                                       .dispatch_on_main_thread([this, alive, request, object_id, tree = std::move(tree), cancelled]() mutable {
                    if (!alive->load()) {
                        return; // the service is gone, its destructor joined the worker already
                    }

                    // The worker thread returns right after dispatching, joining it here is what
                    // hands the worker slot over to the next object.
                    if (m_worker.joinable()) {
                        m_worker.join();
                    }
                    m_worker_running = false;

                    // A result of a key an edit has replaced since is thrown away, never shown.
                    if (!cancelled && m_schedule.is_current(request)) {
                        build_nodes(object_id, tree);
                    }
                    m_schedule.finish_build();
                    start_next_job();
                });

            if (!dispatched) {
                // Nobody will ever run the callback, free the slot so the main thread can go on.
                m_worker_running = false;
                m_schedule.finish_build();
            }
        },
        std::move(job)
    );
}

void SlaSupportPreviewService::build_nodes(
    ObjectID                            object_id,
    const Slic3r::sla::SupportToolTree& tree
)
{
    const auto it = m_previews.find(object_id.id);
    if (it == m_previews.end()) {
        SPDLOG_INFO("[SupportPick] SlaSupportPreviewService::build_nodes object_id={} preview not found, returning", object_id.id);
        return; // the object lost its preview while the tree was being built
    }

    // The raft the Auto rule resolved and the lift that goes with it (M7.8.4). Both are what the
    // tree below was built for, and the model is drawn by the very same lift, so the object and its
    // supports meet where the build says they do.
    it->second.auto_raft    = tree.raft ? std::optional<Domain::sla::RaftType>(tree.raft->type)
                                        : std::nullopt;
    it->second.elevation_mm = tree.elevation_mm;
    m_scene_presenter.set_sla_lift(object_id, tree.elevation_mm);

    Scene::Scene& scene = m_scene_presenter.scene();

    if (m_main_node == nullptr) {
        Scene::NodeBuilder builder{scene};
        builder.set_debug_name("SlaSupportPreviewService - Main");
        std::unique_ptr<Scene::Node> main_node = builder.build();
        m_main_node                            = main_node.get();
        scene.add_child(main_node.release(), &scene.root());
        m_main_node_project_id = m_project_interactor.selected_project_id();
    }

    // The mesh and the geometry of the previous tree of this object are freed here, before the new
    // ones are installed: the node referencing them has to go first, and an object never holds
    // more than the tree and the raft it shows (a stale result never reaches this point).
    if (it->second.node != nullptr) {
        scene.remove_child(it->second.node);
        it->second.node = nullptr;
    }
    const Scene::AuxiliaryElementId support_id{Scene::AuxiliaryElementId::Type::SlaSupports, object_id.id};
    const Scene::AuxiliaryElementId pad_id{Scene::AuxiliaryElementId::Type::SlaPad, object_id.id};
    m_support_geometry_manager.release(support_id);
    m_support_geometry_manager.release(pad_id);
    m_support_mesh_manager.release(support_id);
    m_support_mesh_manager.release(pad_id);

    if ((!tree.tree || tree.tree->empty()) && (!tree.pad || tree.pad->empty())) {
        SPDLOG_INFO("[SupportPick] SlaSupportPreviewService::build_nodes object_id={} empty tree and pad, returning", object_id.id);
        return;
    }

    // [SupportPick] Log tree rebuild info
    // Compute raw tree mesh bbox (before node transform)
    Domain::Vec3d tree_bbox_min{0,0,0}, tree_bbox_max{0,0,0};
    bool has_tree_bbox = false;
    if (tree.tree && !tree.tree->empty()) {
        const auto& vertices = tree.tree->its.vertices;
        bool first = true;
        for (const auto& vertex : vertices) {
            const Domain::Vec3d v = vertex.cast<double>();
            if (first) {
                tree_bbox_min = tree_bbox_max = v;
                first = false;
            } else {
                tree_bbox_min = tree_bbox_min.cwiseMin(v);
                tree_bbox_max = tree_bbox_max.cwiseMax(v);
            }
        }
        has_tree_bbox = !first;
    }

    // Node transform translation (from final_trafo)
    Domain::Vec3d node_translation{0,0,0};
    if (tree.elevation_mm != 0.) {
        node_translation = Domain::Vec3d(0., 0., tree.elevation_mm);
    }

    SPDLOG_INFO("[SupportPick] SlaSupportPreviewService::build_nodes object_id={} node_translation=({}, {}, {}) lift_mm={} tree_bbox_min=({}, {}, {}) tree_bbox_max=({}, {}, {}) has_tree={} has_pad={}",
        object_id.id,
        node_translation.x(), node_translation.y(), node_translation.z(),
        tree.elevation_mm,
        has_tree_bbox ? tree_bbox_min.x() : 0.0, has_tree_bbox ? tree_bbox_min.y() : 0.0, has_tree_bbox ? tree_bbox_min.z() : 0.0,
        has_tree_bbox ? tree_bbox_max.x() : 0.0, has_tree_bbox ? tree_bbox_max.y() : 0.0, has_tree_bbox ? tree_bbox_max.z() : 0.0,
        tree.tree && !tree.tree->empty(), tree.pad && !tree.pad->empty()
    );

    // The tree and the raft are resin too, but they have to be told apart from the model, so each
    // gets its own theme token (PLAN 2.1) instead of the model's resin colour.
    const auto& theme = AppServices::instance().theme();
    const auto material_for = [this, &theme](Platform::Color color) {
        const ColorRGBA mesh_color = theme.color(color, Platform::ColorGroup::Default);
        return Render::Material{}
            .set_shader(m_device.context().shader_manager().shader("gouraud_light"))
            .set_uniform("uniform_color", mesh_color)
            .set_transparent(mesh_color.is_transparent());
    };

    // The meshes come back in the world placement of the object and not lifted (M2.34, see
    // sla_support_tree_placement and libslic3r/SLASupportTool.hpp), so the node raises them by the
    // support elevation and by nothing else: the tree stands on the plate and the model sits on top
    // of it, and a pinhead is at the support point it belongs to. That elevation is the one the
    // tree was built for, which is what set_sla_lift above lifted the model by as well, and it is
    // the same node transform sla_support_tree_placement builds for a lift (M7.8.4). The transform
    // of the build plate is not applied here any more: the instance matrix the meshes were placed
    // with is a world matrix and already carries that offset. No AABB, the preview is not clickable
    // and never steals a pick from the model.
    const Transform3d final_trafo = tree.elevation_mm == 0. ? Transform3d::Identity()
                                                            : Domain::translation_transform(
                                                                  Domain::Vec3d(0., 0., tree.elevation_mm)
                                                                );

    Scene::NodeBuilder object_builder{scene};
    object_builder.set_debug_name(fmt::format("SlaSupportPreviewService - obj {}", object_id.id));

    if (tree.tree && !tree.tree->empty()) {
        const Scene::TriangleMesh* trimesh = m_support_mesh_manager.get_or_create(
            support_id,
            [&]() { return std::make_unique<Scene::TriangleMesh>(tree.tree); }
        );
        const Render::Geometry* geom = m_support_geometry_manager.get_or_create(
            support_id,
            [&]() { return Render::geometry_from_triangle_mesh(m_device, trimesh->triangles()); }
        );

        const Render::Material material = material_for(Platform::Color::SlaSupport);
        object_builder.child([&](Scene::NodeBuilder& bldr) {
            bldr.set_debug_name("SlaSupportPreviewService - Support Structure")
                .set_mesh(geom, material, Scene::RenderLayerId(PlaterSceneLayer::DocumentObjects))
                .set_transform(final_trafo);
        });
    }

    if (tree.pad && !tree.pad->empty()) {
        const Scene::TriangleMesh* trimesh = m_support_mesh_manager.get_or_create(
            pad_id,
            [&]() { return std::make_unique<Scene::TriangleMesh>(tree.pad); }
        );
        const Render::Geometry* geom = m_support_geometry_manager.get_or_create(
            pad_id,
            [&]() { return Render::geometry_from_triangle_mesh(m_device, trimesh->triangles()); }
        );

        const Render::Material material = material_for(Platform::Color::SlaPad);
        object_builder.child([&](Scene::NodeBuilder& bldr) {
            bldr.set_debug_name("SlaSupportPreviewService - Pad")
                .set_mesh(geom, material, Scene::RenderLayerId(PlaterSceneLayer::DocumentObjects))
                .set_transform(final_trafo);
        });
    }

    std::unique_ptr<Scene::Node> object_node = object_builder.build();
    it->second.node                          = object_node.get();
    scene.add_child(object_node.release(), m_main_node);
}

} // namespace Slic3r::App::Plater
