#include "libslic3r/SLASupportTool.hpp"

#include "libslic3r/ConfigViews.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/SLA/SupportPointGenerator.hpp"
#include "libslic3r/SLA/SupportTree.hpp"
#include "libslic3r/SLA/SupportIslands/SampleConfigFactory.hpp"
#include "libslic3r/SLA/Pad.hpp"
#include "libslic3r/SLA/JobController.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"
#include "admesh/stl.h"

#include <Slic3r/Biz/Algorithms/ModelObject.hpp>
#include <Slic3r/Biz/Algorithms/TriangleMesh.hpp>
#include <Slic3r/Biz/Algorithms/BoundingBox.hpp>
#include <Slic3r/Biz/Algorithms/AABBMesh.hpp>
#include <Slic3r/Domain/TriangleMesh.hpp>
#include <Slic3r/Domain/ConfigCommon.hpp>
#include <Slic3r/Domain/Constants.hpp>
#include <Slic3r/Domain/SlaLayerHeight.hpp>
#include <Slic3r/Exception.hpp>

namespace Slic3r::sla {

namespace {

using Domain::its_merge;

// Build the object's merged mesh (MODEL PART volumes only) transformed by object_to_world.
Domain::TriangleMesh build_object_mesh(const Domain::ModelObject& object,
                                       const Domain::Transform3d& object_to_world)
{
    indexed_triangle_set its;
    for (const Domain::ModelVolume* vol : object.volumes) {
        if (vol && vol->mesh_ptr() && vol->is_model_part()) {
            indexed_triangle_set vol_mesh = vol->mesh().its;
            its_transform(vol_mesh, object_to_world * vol->get_matrix());
            its_merge(its, vol_mesh);
        }
    }
    Domain::TriangleMeshStats stats = Biz::Algorithms::TriangleMesh::calculate_stats(its);
    return Domain::TriangleMesh(std::move(its), std::move(stats));
}

// Compute slice heights: zmin + layer_height * (i + 0.5) up to zmax.
std::vector<float> compute_slice_heights(const Domain::TriangleMesh& mesh,
                                         double layer_height)
{
    auto bb = mesh.bounding_box();
    double zmin = bb.min.z();
    double zmax = bb.max.z();

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

SupportToolTree build_support_tree_for_tool(const Domain::ModelObject& object,
    const Domain::Transform3d& object_to_world,
    const Domain::SLA::SupportPoints& points,
    const Domain::FullConfigSLAPtr& full_config,
    const Domain::PartialObjectConfigSLAPtr& object_settings,
    const SupportToolStop& stop)
{
    try {
        if (stop && stop()) return empty_tree();

        const SLAPrintObjectConfigView cfg{full_config, object_settings};

        // Check if supports are enabled
        bool supports_enable = cfg.get<bool>("supports_enable");
        if (!supports_enable || points.empty()) {
            return empty_tree();
        }

        // Build merged mesh in world frame
        Domain::TriangleMesh mesh = build_object_mesh(object, object_to_world);
        if (mesh.empty()) return empty_tree();

        // Points are in object's mesh frame; transform to world frame
        Domain::SLA::SupportPoints world_points = points;
        for (auto& sp : world_points) {
            sp.pos = (object_to_world * sp.pos.cast<double>()).cast<float>();
        }

        // Create SupportableMesh (aggregate: cfg and pad_cfg have no default ctor)
        sla::SupportableMesh supportable_mesh{
            .emesh    = AABBMesh(mesh.its),
            .pts      = std::make_shared<const Domain::SLA::SupportPoints>(std::move(world_points)),
            .cfg      = make_support_cfg(cfg),
            .pad_cfg  = make_pad_cfg(cfg),
            .zoffset  = mesh.bounding_box().min.z(),
        };

        // JobController with stop condition
        sla::JobController ctl;
        ctl.stopcondition = stop;
        ctl.cancelfn = [&stop]() {
            if (stop && stop()) throw Slic3r::RuntimeError("Support tool canceled");
        };

        // Create support tree
        indexed_triangle_set tree_its = sla::create_support_tree(supportable_mesh, ctl);
        std::shared_ptr<const Domain::TriangleMesh> tree_mesh;
        if (!tree_its.empty()) {
            Domain::TriangleMeshStats stats = Biz::Algorithms::TriangleMesh::calculate_stats(tree_its);
            tree_mesh = std::make_shared<const Domain::TriangleMesh>(std::move(tree_its), std::move(stats));
        }

        // Create pad if enabled
        std::shared_ptr<const Domain::TriangleMesh> pad_mesh;
        if (cfg.get<bool>("pad_enable")) {
            if (stop && stop()) return {tree_mesh, nullptr};

            const indexed_triangle_set empty_its;
            const indexed_triangle_set& tree_its_for_pad = tree_mesh ? tree_mesh->its : empty_its;
            indexed_triangle_set pad_its = sla::create_pad(supportable_mesh, tree_its_for_pad, ctl);
            if (validate_pad(pad_its, supportable_mesh.pad_cfg)) {
                Domain::TriangleMeshStats stats = Biz::Algorithms::TriangleMesh::calculate_stats(pad_its);
                pad_mesh = std::make_shared<const Domain::TriangleMesh>(std::move(pad_its), std::move(stats));
            }
        }

        return SupportToolTree{tree_mesh, pad_mesh};

    } catch (const Slic3r::RuntimeError&) {
        return empty_tree();
    } catch (...) {
        return empty_tree();
    }
}

Domain::SLA::SupportPoints generate_support_points_for_tool(const Domain::ModelObject& object,
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
        Domain::TriangleMesh mesh = build_object_mesh(object, object_to_world);
        if (mesh.empty()) return result;

        // Compute slice heights
        double layer_height = Domain::sla_effective_layer_height(cfg);
        std::vector<float> heights = compute_slice_heights(mesh, layer_height);
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

        std::vector<Domain::ExPolygons> slices = slice_mesh_ex(mesh.its, heights, params, throw_on_cancel);

        // Prepare generator data
        sla::PrepareSupportConfig prepare_cfg;
        sla::SupportPointGeneratorData gen_data = sla::prepare_generator_data(
            std::move(slices), heights, prepare_cfg, throw_on_cancel, [](int){});

        // Configure support point generator
        sla::SupportPointGeneratorConfig config;
        config.density_relative = float(cfg.get<int>("support_points_density_relative") / 100.f);

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

        // Generate support points
        sla::LayerSupportPoints layer_support_points = sla::generate_support_points(
            gen_data, config, throw_on_cancel, [](int){});

        // Move points onto mesh surface
        double allowed_move = (heights.size() > 1 ? heights[1] - heights[0] : layer_height) +
            std::numeric_limits<float>::epsilon();
        Domain::SLA::SupportPoints support_points = sla::move_on_mesh_surface(
            layer_support_points, AABBMesh(mesh.its), allowed_move, throw_on_cancel);

        // Zero-elevation filter
        if (is_zero_elevation(cfg)) {
            float lvl = float(mesh.bounding_box().min.z() + Domain::EPSILON);
            std::erase_if(support_points, [lvl](const Domain::SLA::SupportPoint& sp) {
                return sp.pos.z() <= lvl;
            });
        }

        // Transform points back to object's mesh frame
        Domain::Transform3d world_to_object = object_to_world.inverse();
        for (auto& sp : support_points) {
            sp.pos = (world_to_object * sp.pos.cast<double>()).cast<float>();
        }

        return support_points;

    } catch (const Slic3r::RuntimeError&) {
        return {};
    } catch (...) {
        return {};
    }
}

double support_tool_elevation(const Domain::FullConfigSLAPtr& full_config,
                              const Domain::PartialObjectConfigSLAPtr& object_settings)
{
    const SLAPrintObjectConfigView cfg{full_config, object_settings};
    if (is_zero_elevation(cfg)) return 0.;

    bool supports_enable = cfg.get<bool>("supports_enable");
    double ret = supports_enable ? cfg.get<double>("support_object_elevation") : 0.;

    if (supports_enable && cfg.get<bool>("pad_enable")) {
        sla::PadConfig pcfg = make_pad_cfg(cfg);
        if (!pcfg.embed_object.enabled) {
            ret += pcfg.required_elevation();
        }
    }

    return ret;
}

} // namespace Slic3r::sla