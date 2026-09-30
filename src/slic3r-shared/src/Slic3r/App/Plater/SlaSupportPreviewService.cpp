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
        // still refreshes the preview geometry.
        mix_float(point.tip_length);
        mix_float(point.contact_depth);
        mix_float(point.pillar_diameter);
        mix_float(point.base_diameter);
        mix_float(point.base_height);
        // The same for the per-point tip and stem geometry (M2.16c): a change of the tip shape, the
        // knot, the stem cross-section or the stem taper is a new tree as well.
        mix(static_cast<std::uint64_t>(point.tip_shape));
        mix_float(point.knot_radius);
        mix(static_cast<std::uint64_t>(point.stem_sides));
        mix_float(point.stem_taper);
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
    m_config_container_scope(project_interactor, *this)
{
    refresh();
}

SlaSupportPreviewService::~SlaSupportPreviewService()
{
    m_alive->store(false);
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
    const Transform3d bed_trafo = bed != nullptr ? bed->transformation.get_matrix() : Transform3d::Identity();

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
                candidate.elevation = sla::support_tool_elevation(config->full, config->object);
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
        ObjectPreview& preview         = m_previews[candidate.public_part.object_id.id];
        preview.key                    = candidate.public_part.key;
        preview.elevation              = candidate.elevation;
        m_scene_presenter.set_sla_lift(candidate.public_part.object_id, candidate.elevation);
    }

    for (const ObjectID& object_id : diff.to_recompute) {
        const auto it = std::find_if(candidates.begin(), candidates.end(), [&](const Candidate& candidate) {
            return candidate.public_part.object_id == object_id;
        });
        if (it == candidates.end() || !it->public_part.wants_preview) {
            continue;
        }

        Job job;
        job.cloned_object   = std::unique_ptr<Domain::ModelObject>(Domain::ModelObject::new_clone(*it->model_object));
        job.instance_matrix = it->instance->get_matrix();
        job.points          = it->model_object->sla_support_points;
        job.full_config     = it->config.full;
        job.object_config   = it->config.object;
        job.bed_trafo       = bed_trafo;
        job.elevation       = it->elevation;
        job.object_id       = object_id;
        job.generation      = ++m_generations[object_id.id];

        // A tree that is still queued for this object is stale the moment a new key arrives.
        std::erase_if(m_queue, [&](const Job& queued) { return queued.object_id == object_id; });
        m_queue.push_back(std::move(job));
    }

    start_next_job();
}

void SlaSupportPreviewService::clear()
{
    for (const auto& entry : m_previews) {
        m_scene_presenter.set_sla_lift(ObjectID{entry.first}, 0.);
    }
    m_previews.clear();
    m_generations.clear();
    cancel_worker();
    drop_main_node();
}

void SlaSupportPreviewService::drop_object(ObjectID object_id)
{
    ++m_generations[object_id.id];

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

    // Whatever is still running or queued for this object is not wanted anymore.
    std::erase_if(m_queue, [&](const Job& queued) { return queued.object_id == object_id; });
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
}

void SlaSupportPreviewService::start_next_job()
{
    if (!m_alive->load() || m_worker_running || m_queue.empty()) {
        return;
    }

    if (m_worker.joinable()) {
        m_worker.join();
    }

    Job job = std::move(m_queue.front());
    m_queue.pop_front();
    m_worker_running = true;

    m_worker = Biz::JThread::JThread(
        [this](Biz::JThread::StopToken stop_token, Job worker_job) mutable {
            const Slic3r::sla::SupportToolStop stop = [&stop_token]() { return stop_token.stop_requested(); };

            Slic3r::sla::SupportToolTree tree = sla::build_support_tree_for_tool(
                *worker_job.cloned_object,
                worker_job.instance_matrix,
                worker_job.points,
                worker_job.full_config,
                worker_job.object_config,
                stop
            );
            const bool cancelled = stop_token.stop_requested();

            const ObjectID object_id                          = worker_job.object_id;
            const std::size_t generation                      = worker_job.generation;
            const double elevation                           = worker_job.elevation;
            const Transform3d bed_trafo                       = worker_job.bed_trafo;
            const std::shared_ptr<std::atomic<bool>> alive    = m_alive;

            const bool dispatched = Biz::Platform::PlatformServices::instance()
                                       .main_thread_dispatcher()
                                       .dispatch_on_main_thread([this, alive, object_id, generation, elevation, bed_trafo, tree = std::move(tree), cancelled]() mutable {
                    if (!alive->load()) {
                        return; // the service is gone, its destructor joined the worker already
                    }

                    // The worker thread returns right after dispatching, joining it here is what
                    // hands the worker slot over to the next object.
                    if (m_worker.joinable()) {
                        m_worker.join();
                    }
                    m_worker_running = false;

                    if (!cancelled) {
                        const auto generation_it = m_generations.find(object_id.id);
                        if (generation_it != m_generations.end() && generation_it->second == generation) {
                            build_nodes(object_id, bed_trafo, elevation, tree);
                        }
                    }
                    start_next_job();
                });

            if (!dispatched) {
                // Nobody will ever run the callback, free the slot so the main thread can go on.
                m_worker_running = false;
            }
        },
        std::move(job)
    );
}

void SlaSupportPreviewService::build_nodes(
    ObjectID                           object_id,
    const Transform3d&                 bed_trafo,
    double                             elevation,
    const Slic3r::sla::SupportToolTree& tree
)
{
    const auto it = m_previews.find(object_id.id);
    if (it == m_previews.end()) {
        return; // the object lost its preview while the tree was being built
    }

    Scene::Scene& scene = m_scene_presenter.scene();

    if (m_main_node == nullptr) {
        Scene::NodeBuilder builder{scene};
        builder.set_debug_name("SlaSupportPreviewService - Main");
        std::unique_ptr<Scene::Node> main_node = builder.build();
        m_main_node                            = main_node.get();
        scene.add_child(main_node.release(), &scene.root());
        m_main_node_project_id = m_project_interactor.selected_project_id();
    }

    // The old geometry of this object goes first, the node referencing it has to go with it.
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
        return;
    }

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

    // The meshes come back in the world placement of the object, not lifted: the bed transform and
    // the support elevation belong to the node, so the tree stands on the plate and the model sits
    // on top of it. No AABB, the preview is not clickable and never steals a pick from the model.
    const Transform3d final_trafo = bed_trafo * Domain::translation_transform(Vec3d(0., 0., elevation));

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
