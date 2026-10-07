#include "libslic3r/SLA/RaftAuto.hpp"

#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Transformation.hpp"
#include "libslic3r/SLA/CavityDetection.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r::sla {

namespace {

// The lift under which a part no longer stands on the plate. The elevation of a print is never
// smaller than this, and a part lifted by less than it stands on the plate as far as this rule is
// concerned.
constexpr double elevation_epsilon_mm = 1e-3;

// The layers of the underside: one below the bottom of the part, then one per layer height up to the
// scan height, plus one layer above that, because a pocket is only a cup once a layer closes it and
// that layer may well be the first one past the scan.
//
// The layer below the bottom is the part's own first printed layer when the part is held above the
// plate (R6.2: a pocket seals against "the raft-less first layers" as well as against the plate). A
// part that stands on the plate prints nothing there, so that layer comes back empty and says
// nothing; it is what gives a pocket of a lifted part the solid under it that a pocket of a standing
// part gets from the film.
void scan_layers(const indexed_triangle_set& mesh,
                 double                       layer_height_mm,
                 double                       lift_mm,
                 const RaftAutoOptions&       opts,
                 std::vector<float>&          zs,
                 std::vector<float>&          thicknesses_mm)
{
    const Domain::BoundingBox3d bb = Domain::bounding_box(mesh);
    const double bottom = bb.min.z() + lift_mm;
    const double scan_top = std::min(bb.max.z() + lift_mm, bottom + opts.scan_height_mm);

    zs.clear();
    thicknesses_mm.clear();
    zs.push_back(float(bottom - layer_height_mm * 0.5));
    thicknesses_mm.push_back(float(layer_height_mm));
    for (double z = bottom + layer_height_mm * 0.5; z < scan_top; z += layer_height_mm) {
        zs.push_back(float(z));
        thicknesses_mm.push_back(float(layer_height_mm));
    }
    zs.push_back(float(scan_top + layer_height_mm * 0.5));
    thicknesses_mm.push_back(float(layer_height_mm));
}

// Early-out scan: only the virtual layer under the part and the first real layer.
// If the first real layer has no hole with area >= min_cup_opening_mm2, there is no cup.
bool first_layer_has_cup_opening(const indexed_triangle_set& mesh_in_print_pose,
                                 double                     layer_height_mm,
                                 double                     object_elevation_mm,
                                 const RaftAutoOptions&     opts,
                                 const ThrowOnCancel&       throw_on_cancel)
{
    const Domain::BoundingBox3d bb = Domain::bounding_box(mesh_in_print_pose);
    const double bottom = bb.min.z() + object_elevation_mm;

    // Two layers: virtual layer below the part, and the first real layer.
    std::vector<float> early_zs;
    std::vector<float> early_thicknesses;
    early_zs.push_back(float(bottom - layer_height_mm * 0.5));
    early_thicknesses.push_back(float(layer_height_mm));
    early_zs.push_back(float(bottom + layer_height_mm * 0.5));
    early_thicknesses.push_back(float(layer_height_mm));

    MeshSlicingParamsEx params;
    if (object_elevation_mm != 0.)
        params.trafo = Domain::translation_transform(Domain::Vec3d(0., 0., object_elevation_mm));

    const std::vector<Domain::ExPolygons> early_layers = throw_on_cancel
        ? slice_mesh_ex(mesh_in_print_pose, early_zs, params, throw_on_cancel)
        : slice_mesh_ex(mesh_in_print_pose, early_zs, params);

    if (early_layers.size() < 2)
        return false;

    // The first real layer is at index 1. Check its holes for openings large enough to be a cup.
    // A suction cup's opening is a hole in the first real layer of the part (a pocket open below
    // onto the film, or onto the first printed layers of a lifted part). We check the raw holes
    // of the ExPolygons; holes_of() in CavityDetection.cpp also subtracts solid inside the holes,
    // but for the early-out the raw hole area is a conservative overestimate: if even the raw hole
    // is smaller than min_cup_opening_mm2, the cup cannot exist.
    constexpr double sf = Slic3r::Biz::Algorithms::Scaling::SCALING_FACTOR;
    const Domain::ExPolygons& first_real_layer = early_layers[1];
    for (const Domain::ExPolygon& region : first_real_layer) {
        for (const Domain::Polygon& hole : region.holes) {
            const double area_mm2 = std::abs(hole.area()) * sf * sf;
            if (area_mm2 >= opts.min_cup_opening_mm2)
                return true;
        }
    }
    return false;
}

} // namespace

RaftAutoDecision auto_raft_decision(const indexed_triangle_set& mesh_in_print_pose,
                                    double                     layer_height_mm,
                                    double                     object_elevation_mm,
                                    const RaftAutoOptions&     opts,
                                    const ThrowOnCancel&       throw_on_cancel)
{
    RaftAutoDecision decision;

    // Nothing to read, or nothing to read it with: a mesh of no triangles, a layer height of no
    // layer. Neither has a cup in it.
    if (mesh_in_print_pose.vertices.empty() || mesh_in_print_pose.indices.empty())
        return decision;
    if (!(layer_height_mm > 0.))
        return decision;

    // CHEAP EARLY OUT: a suction cup's opening is on the FIRST real layer of the part.
    // Slice only the virtual layer under the part and the first real layer.
    // If the first real layer has no hole with opening area >= min_cup_opening_mm2,
    // there is no cup and we can return immediately without slicing the rest.
    if (!first_layer_has_cup_opening(mesh_in_print_pose, layer_height_mm, object_elevation_mm, opts, throw_on_cancel))
        return decision;

    std::vector<float> zs;
    std::vector<float> thicknesses_mm;
    scan_layers(mesh_in_print_pose, layer_height_mm, object_elevation_mm, opts, zs,
                thicknesses_mm);
    if (zs.empty())
        return decision;

    // The lift goes into the slicing parameters rather than into a copy of the mesh, so the mesh is
    // only read from here on: the layers above are the ones of the part in the pose it prints in.
    MeshSlicingParamsEx params;
    if (object_elevation_mm != 0.)
        params.trafo = Domain::translation_transform(Domain::Vec3d(0., 0., object_elevation_mm));

    const std::vector<Domain::ExPolygons> layers = throw_on_cancel
        ? slice_mesh_ex(mesh_in_print_pose, zs, params, throw_on_cancel)
        : slice_mesh_ex(mesh_in_print_pose, zs, params);

    // The cup detection lives in Slic3r::SLA, not in Slic3r::sla, and it takes the very
    // std::vector<Domain::ExPolygons> slice_mesh_ex returned, so the two need no conversion.
    const SLA::CavityAnalysis cavities = SLA::detect_cavities(
        layers, thicknesses_mm, SLA::CavityDetectionOptions{opts.min_cup_opening_mm2});

    // No cup under the part: no raft (R6.1). A pocket that is still open on the last layer read
    // vents instead, which the detection does not report as a cup, so it ends here as well.
    if (cavities.cups.empty())
        return decision;

    // The lowest cup is the one to answer for: the rule is about the first layers of the part.
    const SLA::CupHit& cup = cavities.cups.front();
    decision.suction          = true;
    decision.first_cup_layer  = cup.first_layer;
    decision.opening_area_mm2 = cup.opening_area_mm2;
    // Which raft removes the suction is where the part sits, see the header.
    decision.raft_type = object_elevation_mm > elevation_epsilon_mm
                             ? Domain::sla::RaftType::Full
                             : Domain::sla::RaftType::AroundObject;

    return decision;
}

} // namespace Slic3r::sla