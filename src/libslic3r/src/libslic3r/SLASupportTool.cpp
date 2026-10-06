#include "libslic3r/SLASupportTool.hpp"

#include "libslic3r/ConfigViews.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/SLA/SupportFacetPaint.hpp"
#include "libslic3r/SLA/SupportAnchors.hpp"
#include "libslic3r/SLA/SupportPointGenerator.hpp"
#include "libslic3r/SLA/SupportRoles.hpp"
#include "libslic3r/SLA/SupportTree.hpp"
#include "libslic3r/SLA/SupportIslands/SampleConfigFactory.hpp"
#include "libslic3r/SLA/Pad.hpp"
#include "libslic3r/SLA/JobController.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"
#include "admesh/stl.h"

#include <cmath>
#include <Slic3r/Biz/Algorithms/ModelObject.hpp>
#include <Slic3r/Biz/Algorithms/TriangleMesh.hpp>
#include <Slic3r/Biz/Algorithms/BoundingBox.hpp>
#include <Slic3r/Biz/Algorithms/AABBMesh.hpp>
#include <Slic3r/Domain/TriangleMesh.hpp>
#include <Slic3r/Domain/ConfigCommon.hpp>
#include <Slic3r/Domain/Constants.hpp>
#include <Slic3r/Domain/SlaLayerHeight.hpp>
#include <Slic3r/Exception.hpp>
#include <Slic3r/Log.hpp>

namespace Slic3r::sla {

namespace {

using Domain::its_merge;

// Build the object's merged mesh (MODEL PART volumes only) transformed by object_to_world. Every
// vertex is copied here, so it belongs to the thread that owns the build, not to the one that took
// the snapshot (support_tool_model_mesh()).
indexed_triangle_set build_object_its(const SupportToolModelMesh&          model_mesh,
                                      const Domain::Transform3d& object_to_world)
{
    indexed_triangle_set its;
    for (const SupportToolModelMesh::Part& part : model_mesh.parts) {
        indexed_triangle_set vol_mesh = part.mesh->its;
        its_transform(vol_mesh, object_to_world * part.matrix);
        its_merge(its, vol_mesh);
    }
    return its;
}

// Compute slice heights: zmin + layer_height * (i + 0.5) up to zmax.
std::vector<float> compute_slice_heights(const indexed_triangle_set& its, double layer_height)
{
    const Domain::BoundingBox3d bb = Domain::bounding_box(its);
    double zmin = bb.min.z();
    double zmax = bb.max.z();

    // A box that is not a range of coordinates (a vertex is a NaN or an infinity) or a layer height
    // that is not a step leaves nothing to slice. The loop below never ends on an infinite box and
    // never advances on an infinite step, so this is where the tool gives up on such a mesh.
    if (!std::isfinite(zmin) || !std::isfinite(zmax))
        return {};
    if (!(layer_height > 0.))
        return {};

    std::vector<float> heights;
    for (double z = zmin + layer_height * 0.5; z < zmax; z += layer_height) {
        heights.push_back(float(z));
    }
    return heights;
}

SupportToolTree empty_tree() {
    return SupportToolTree{nullptr, nullptr};
}

} // namespace

SupportToolModelMesh support_tool_model_mesh(const Domain::ModelObject& object)
{
    SupportToolModelMesh snapshot;
    snapshot.parts.reserve(object.volumes.size());
    for (const Domain::ModelVolume* vol : object.volumes) {
        if (vol == nullptr || !vol->is_model_part())
            continue;
        // Shared with the model, not copied: this is what keeps the snapshot cheap. The volume
        // keeps holding its mesh, so the worker sees the geometry of the moment it was taken even
        // if the model swaps its mesh or its transform right after.
        std::shared_ptr<const Domain::TriangleMesh> mesh = vol->mesh_ptr();
        if (!mesh)
            continue;
        // The painting is indexed by the triangles of this very mesh, so it has to travel with it.
        snapshot.parts.push_back({std::move(mesh), vol->get_matrix(), vol->supported_facets.get_data()});
    }
    return snapshot;
}

SupportToolTree build_support_tree_for_tool(const SupportToolModelMesh& model_mesh,
    const Domain::Transform3d& object_to_world,
    const Domain::SLA::SupportPoints& points,
    const Domain::FullConfigSLAPtr& full_config,
    const Domain::PartialObjectConfigSLAPtr& object_settings,
    const SupportToolStop& stop)
{
    try {
        if (stop && stop()) return empty_tree();

        const SLAPrintObjectConfigView cfg{full_config, object_settings};

        // Build merged mesh in world frame: the object as it prints, without the lift (M2.34).
        indexed_triangle_set its = build_object_its(model_mesh, object_to_world);

        // The raft of a raft_type Auto is a decision about this mesh (rulebook R6, M7.8.4), and it
        // is the same pure function the slice runs (resolve_object_raft in SLAPrint.cpp), so the
        // preview and the print cannot show two different rafts. It is read before anything else,
        // because it decides whether the object stands on the plate, and with that the elevation.
        const std::optional<ObjectRaft> raft = resolve_object_raft(
            cfg, its, support_tool_elevation(full_config, object_settings));

        SupportToolTree out;
        out.raft         = raft;
        out.elevation_mm = support_tool_elevation(full_config, object_settings, raft);

        if (its.empty()) return out;

        // A support tree needs support points. A raft around the object does not: that one is cut
        // from the object itself, so an object the generator found no point on at all still gets
        // the raft the slice gives it, and the raft that comes out of a frame around the object
        // that stands on the plate is empty rather than absent. A raft under the object is a slab
        // at the foot of the pillars, so with no tree to stand on there is nothing to build,
        // which is what SLAPrint::Steps::generate_pad() keeps as well.
        bool supports_enable = cfg.get<bool>("supports_enable");
        const bool tree_built = supports_enable && !points.empty();
        if (!tree_built && !is_zero_elevation(cfg, raft)) {
            return out;
        }

        Domain::TriangleMeshStats mesh_stats = Biz::Algorithms::TriangleMesh::calculate_stats(its);
        Domain::TriangleMesh       mesh{std::move(its), std::move(mesh_stats)};

        // JobController with stop condition
        sla::JobController ctl;
        ctl.stopcondition = stop;
        ctl.cancelfn = [&stop]() {
            if (stop && stop()) throw Slic3r::RuntimeError("Support tool canceled");
        };

        // Points are in object's mesh frame; transform to world frame
        Domain::SLA::SupportPoints world_points = points;
        {
            // The points of a big model are millions of them, and this walk is one of the steps of
            // the build, so it asks the stop function on the way (M4.16).
            size_t id = 0;
            for (auto& sp : world_points) {
                if ((id++ % 4096) == 0)
                    ctl.cancelfn();
                sp.pos = (object_to_world * sp.pos.cast<double>()).cast<float>();
            }
        }

        // Create SupportableMesh (aggregate: cfg and pad_cfg have no default ctor)
        sla::SupportableMesh supportable_mesh{
            .emesh    = AABBMesh(mesh.its),
            .pts      = std::make_shared<const Domain::SLA::SupportPoints>(std::move(world_points)),
            .cfg      = make_support_cfg(cfg, raft),
            .pad_cfg  = make_pad_cfg(cfg, raft),
            .zoffset  = mesh.bounding_box().min.z(),
        };

        // Create support tree
        std::shared_ptr<const Domain::TriangleMesh> tree_mesh;
        if (tree_built) {
            indexed_triangle_set tree_its = sla::create_support_tree(supportable_mesh, ctl);
            if (!tree_its.empty()) {
                Domain::TriangleMeshStats stats = Biz::Algorithms::TriangleMesh::calculate_stats(tree_its);
                tree_mesh = std::make_shared<const Domain::TriangleMesh>(std::move(tree_its), std::move(stats));
            }
        }

        // Create pad if enabled
        if (is_pad_enabled(cfg, raft)) {
            if (stop && stop()) {
                out.tree = std::move(tree_mesh);
                return out;
            }

            const indexed_triangle_set empty_its;
            const indexed_triangle_set& tree_its_for_pad = tree_mesh ? tree_mesh->its : empty_its;
            indexed_triangle_set pad_its = sla::create_pad(supportable_mesh, tree_its_for_pad, ctl);
            if (validate_pad(pad_its, supportable_mesh.pad_cfg)) {
                Domain::TriangleMeshStats stats = Biz::Algorithms::TriangleMesh::calculate_stats(pad_its);
                out.pad = std::make_shared<const Domain::TriangleMesh>(std::move(pad_its), std::move(stats));
            }
        }

        out.tree = std::move(tree_mesh);

        return out;

    } catch (const Slic3r::RuntimeError& e) {
        // "Support tool canceled" is expected when the stop condition triggers; log at debug only.
        std::string what = e.what();
        if (what == "Support tool canceled") {
            SPDLOG_DEBUG("Support tool: canceled");
        } else {
            SPDLOG_ERROR("Support tool: {}", what);
        }
        return empty_tree();
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Support tool: {}", e.what());
        return empty_tree();
    } catch (...) {
        SPDLOG_ERROR("Support tool: unknown exception");
        return empty_tree();
    }
}

SupportToolTree build_support_tree_for_tool(const Domain::ModelObject& object,
    const Domain::Transform3d& object_to_world,
    const Domain::SLA::SupportPoints& points,
    const Domain::FullConfigSLAPtr& full_config,
    const Domain::PartialObjectConfigSLAPtr& object_settings,
    const SupportToolStop& stop)
{
    // The object is read here, so this is the entry point for a caller that owns the model and does
    // not share the main thread with it. A caller whose model may change while the build runs
    // snapshots it with support_tool_model_mesh() and calls the overload above instead.
    return build_support_tree_for_tool(support_tool_model_mesh(object), object_to_world, points,
                                       full_config, object_settings, stop);
}

Domain::SLA::SupportPoints generate_support_points_for_tool(const SupportToolModelMesh& model_mesh,
    const Domain::Transform3d& object_to_world,
    const Domain::FullConfigSLAPtr& full_config,
    const Domain::PartialObjectConfigSLAPtr& object_settings,
    const SupportToolStop& stop)
{
    Domain::SLA::SupportPoints result;
    try {
        if (stop && stop()) return result;

        const SLAPrintObjectConfigView cfg{full_config, object_settings};

        // Build merged mesh in world frame
        indexed_triangle_set its = build_object_its(model_mesh, object_to_world);
        if (its.empty()) return result;

        // The same raft decision the tree is built from (M7.8.4): points on the bottom of an object
        // that prints in a raft around it would stand inside that raft, so the zero elevation filter
        // below needs to know what raft_type resolved to and not just that it is Auto.
        const std::optional<ObjectRaft> raft = resolve_object_raft(
            cfg, its, support_tool_elevation(full_config, object_settings));

        // Compute slice heights
        double layer_height = Domain::sla_effective_layer_height(cfg);
        std::vector<float> heights = compute_slice_heights(its, layer_height);
        if (heights.empty()) return result;

        // Slice the mesh
        MeshSlicingParamsEx params;
        params.closing_radius = float(cfg.get<double>("slice_closing_radius"));
        switch (cfg.get<Domain::SlicingMode>("slicing_mode")) {
            case Domain::SlicingMode::Regular:    params.mode = MeshSlicingParams::SlicingMode::Regular; break;
            case Domain::SlicingMode::EvenOdd:    params.mode = MeshSlicingParams::SlicingMode::EvenOdd; break;
            case Domain::SlicingMode::CloseHoles: params.mode = MeshSlicingParams::SlicingMode::Positive; break;
        }

        ThrowOnCancel throw_on_cancel = [&stop]() {
            if (stop && stop()) throw Slic3r::RuntimeError("Support tool canceled");
        };

        std::vector<Domain::ExPolygons> slices = slice_mesh_ex(its, heights, params, throw_on_cancel);

        // The facets the user painted on the model: no support point on a blocked facet and support
        // points on an enforced one, in every layer. An object with nothing painted gives an empty
        // paint, and the points of such an object are the points of the algorithm without painting.
        sla::SupportFacetPaint facet_paint =
            sla::support_facet_paint(model_mesh, object_to_world, heights, throw_on_cancel);

        // Prepare generator data
        sla::PrepareSupportConfig prepare_cfg;
        prepare_cfg.overhang_angle_threshold =
            cfg.get<double>("support_points_overhang_angle");
        sla::SupportPointGeneratorData gen_data = sla::prepare_generator_data(
            std::move(slices), heights, prepare_cfg, throw_on_cancel, [](int){}, std::move(facet_paint));

        // Configure support point generator
        sla::SupportPointGeneratorConfig config;
        config.density_relative = float(cfg.get<int>("support_points_density_relative") / 100.f);
        config.minimal_point_distance = cfg.get<double>("support_points_minimal_distance");

        switch (cfg.get<Domain::sla::SupportTreeType>("support_tree_type")) {
            case Domain::sla::SupportTreeType::Default:
            case Domain::sla::SupportTreeType::Organic:
                config.head_diameter = float(cfg.get<double>("support_head_front_diameter"));
                break;
            case Domain::sla::SupportTreeType::Branching:
                config.head_diameter = float(cfg.get<double>("branchingsupport_head_front_diameter"));
                break;
        }

        config.island_configuration = sla::SampleConfigFactory::apply_density(
            sla::SampleConfigFactory::create(config.head_diameter), config.density_relative);

        // The radius one point may hold is a size, so it scales with the size of the part (M7.8.7):
        // a miniature head a few millimetres across gets the density the studio gives such a model
        // by hand, a plate is big enough to keep the points it had. See support_curve_size_factor
        // in SupportPointGenerator.hpp.
        config.support_curve = sla::support_curve_for_part(config.support_curve, gen_data);

        // Generate support points
        sla::LayerSupportPoints layer_support_points = sla::generate_support_points(
            gen_data, config, throw_on_cancel, [](int){});

        // Move points onto mesh surface
        double allowed_move = (heights.size() > 1 ? heights[1] - heights[0] : layer_height) +
            std::numeric_limits<float>::epsilon();
        const AABBMesh emesh(its);
        Domain::SLA::SupportPoints support_points = sla::move_on_mesh_surface(
            layer_support_points, emesh, allowed_move, throw_on_cancel);

        // What every point carries, which is what the tip class of its support is picked from
        // (M7.8.2, the support rulebook R4.1 and R4.3 - R4.6): the anchor of the lowest island, an
        // island, a small island, a thin fragile feature or an overhang, and the minimum tip of a
        // detailed region whatever any of those would have been (M7.8.5, R4.9). A point that sits on
        // small surface detail moves to the plain surface next to it first, and the role it ends up
        // with is the one of the spot it holds. The head radius of a point stays the one the
        // generator gave it, as it was before.
        sla::classify_support_point_roles(
            support_points, emesh, gen_data.layers, layer_height, {}, throw_on_cancel);

        // The heavy anchors the rulebook asks for on the flat, low-detail areas of the surface that
        // faces the plate (M7.8.3, R4.2): a few of them, more the bigger the footprint of the object
        // is, and the largest tip on a very large one. It runs after the roles, so that it can tell
        // the points of the other rules from the ones it adds itself, and before the zero-elevation
        // filter below, which takes away the anchors of an object standing on the plate. Running
        // last is also what keeps an anchor an anchor: R4.9 leaves the anchors alone in the pass
        // above, and this one only ever adds an anchor or turns an overhang or an island into one,
        // never back into a Detail.
        sla::add_heavy_anchors(support_points, emesh, {}, config.head_diameter / 2.f, throw_on_cancel);

        // Zero-elevation filter, on the raft M7.8.4 resolved at the top of this function: an Auto
        // that found a suction cup under a part standing on the plate resolved to the raft around
        // the object, so the points on the bottom of the part are taken away, and one that found no
        // cup, or a part held above the plate, keeps them. It runs after the anchors of M7.8.3,
        // which is where it belongs: those anchors are added to the underside and then filtered.
        if (is_zero_elevation(cfg, raft)) {
            float lvl = float(Domain::bounding_box(its).min.z() + Domain::EPSILON);
            std::erase_if(support_points, [lvl](const Domain::SLA::SupportPoint& sp) {
                return sp.pos.z() <= lvl;
            });
        }

        // Transform points back to object's mesh frame
        Domain::Transform3d world_to_object = object_to_world.inverse();
        {
            // The same walk as above, on the way back (M4.16).
            size_t id = 0;
            for (auto& sp : support_points) {
                if ((id++ % 4096) == 0)
                    throw_on_cancel();
                sp.pos = (world_to_object * sp.pos.cast<double>()).cast<float>();
            }
        }

        return support_points;

    } catch (const Slic3r::RuntimeError& e) {
        // "Support tool canceled" is expected when the stop condition triggers; log at debug only.
        std::string what = e.what();
        if (what == "Support tool canceled") {
            SPDLOG_DEBUG("Support tool: canceled");
        } else {
            SPDLOG_ERROR("Support tool: {}", what);
        }
        return {};
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Support tool: {}", e.what());
        return {};
    } catch (...) {
        SPDLOG_ERROR("Support tool: unknown exception");
        return {};
    }
}

Domain::SLA::SupportPoints generate_support_points_for_tool(const Domain::ModelObject& object,
    const Domain::Transform3d& object_to_world,
    const Domain::FullConfigSLAPtr& full_config,
    const Domain::PartialObjectConfigSLAPtr& object_settings,
    const SupportToolStop& stop)
{
    return generate_support_points_for_tool(support_tool_model_mesh(object), object_to_world,
                                            full_config, object_settings, stop);
}

double support_tool_elevation(const Domain::FullConfigSLAPtr& full_config,
                              const Domain::PartialObjectConfigSLAPtr& object_settings,
                              const std::optional<ObjectRaft>& raft)
{
    const SLAPrintObjectConfigView cfg{full_config, object_settings};
    if (is_zero_elevation(cfg, raft)) return 0.;

    bool supports_enable = cfg.get<bool>("supports_enable");
    double ret = supports_enable ? cfg.get<double>("support_object_elevation") : 0.;

    if (supports_enable && is_pad_enabled(cfg, raft)) {
        sla::PadConfig pcfg = make_pad_cfg(cfg, raft);
        if (!pcfg.embed_object.enabled) {
            ret += pcfg.required_elevation();
        }
    }

    return ret;
}

} // namespace Slic3r::sla