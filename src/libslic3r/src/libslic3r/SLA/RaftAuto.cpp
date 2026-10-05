#include "libslic3r/SLA/RaftAuto.hpp"

#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Transformation.hpp"
#include "libslic3r/SLA/CavityDetection.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r::sla {

namespace {

using Domain::ExPolygons;
using Domain::indexed_triangle_set;

// The lift under which a part no longer stands on the plate. The elevation of a print is never
// smaller than this, and a part lifted by less than it stands on the plate as far as this rule is
// concerned.
constexpr double elevation_epsilon_mm = 1e-3;

// The layers of the underside: from the first one above the bottom of the part up to the scan
// height, plus one layer above that, because a pocket is only a cup once a layer closes it and that
// layer may well be the first one past the scan.
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
    for (double z = bottom + layer_height_mm * 0.5; z < scan_top; z += layer_height_mm) {
        zs.push_back(float(z));
        thicknesses_mm.push_back(float(layer_height_mm));
    }
    zs.push_back(float(scan_top + layer_height_mm * 0.5));
    thicknesses_mm.push_back(float(layer_height_mm));
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

    const ExPolygons layers = throw_on_cancel
        ? slice_mesh_ex(mesh_in_print_pose, zs, params, throw_on_cancel)
        : slice_mesh_ex(mesh_in_print_pose, zs, params);

    const CavityAnalysis cavities =
        detect_cavities(layers, thicknesses_mm, CavityDetectionOptions{opts.min_cup_opening_mm2});

    // No cup under the part: no raft (R6.1). A pocket that is still open on the last layer read
    // vents instead, which the detection does not report as a cup, so it ends here as well.
    if (cavities.cups.empty())
        return decision;

    // The lowest cup is the one to answer for: the rule is about the first layers of the part.
    const CupHit& cup = cavities.cups.front();
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