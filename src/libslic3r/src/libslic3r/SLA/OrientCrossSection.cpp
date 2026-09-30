#include <libslic3r/SLA/OrientCrossSection.hpp>

#include <libslic3r/SLA/CavityDetection.hpp>
#include <libslic3r/TriangleMeshSlicer.hpp>

#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace Slic3r::sla {

namespace {

// Coordinates are scaled up, so a scaled area is a mm² area divided by SCALING_FACTOR squared.
constexpr double sf = Biz::Algorithms::Scaling::SCALING_FACTOR;

/// The area of a set of regions, in mm².
double area_mm2(const Domain::ExPolygons& regions)
{
    double total = 0.;
    for (const Domain::ExPolygon& region : regions)
        total += std::abs(region.area()) * sf * sf;
    return total;
}

} // namespace

CrossSectionScore score_cross_sections(
    const Domain::TriangleMesh& mesh,
    const Transform3f& rotation,
    const CrossSectionWeights& weights
)
{
    CrossSectionScore result;
    if (mesh.its.vertices.empty() || mesh.its.indices.empty())
        return result;

    // The rotation goes into the slicing parameters instead of into a copy of the mesh, so the mesh
    // is only read from here on. The slice planes are in millimetres, which is the Z the mesh's
    // vertices are in and the Z the slicer cuts at.
    const Transform3f tr{rotation};
    float zmin = std::numeric_limits<float>::max();
    float zmax = std::numeric_limits<float>::lowest();
    for (const Vec3f& vertex : mesh.its.vertices) {
        const float z = (tr * vertex).z();
        zmin          = std::min(zmin, z);
        zmax          = std::max(zmax, z);
    }

    const float height = zmax - zmin;
    if (!(height > 0.f))
        return result; // flat enough that there is nothing between two planes to slice

    // One plane per mm, but never more planes than a pose of cross_section_plane_height_mm mm
    // needs, so that a tall pose costs no more than a short one.
    const float step =
        std::max(float(cross_section_slice_step_mm), height / float(cross_section_plane_height_mm));
    const size_t plane_count = std::max(size_t(1), size_t(std::ceil(height / step)));

    // The planes sit between the extremes rather than on them: a plane on a horizontal face of the
    // mesh does not cut it (the slicer counts a downward face as being above the plane), and a
    // pose is measured by the cross sections inside it, not by its top and bottom face.
    std::vector<float> zs;
    zs.reserve(plane_count);
    for (size_t i = 0; i < plane_count; ++i)
        zs.push_back(zmin + (float(i) + 0.5f) * step);

    MeshSlicingParamsEx params;
    params.trafo = Domain::Transform3d{tr.cast<double>()};

    const std::vector<Domain::ExPolygons> layers = slice_mesh_ex(mesh.its, zs, params);

    for (const Domain::ExPolygons& layer : layers)
        result.peak_area_mm2 = std::max(result.peak_area_mm2, area_mm2(layer));

    // The cups are the same ones the post-slice detection reports (PLAN B5b, M4.8e), run on the
    // coarse slices of this pose: a pocket that is enclosed in its layer and stays enclosed going
    // up until a layer closes it. Only the least peel goal skips them, it does not score them.
    if (weights.cup_opening > 0.) {
        const std::vector<float> thicknesses(layers.size(), step);
        const SLA::CavityAnalysis cavities = SLA::detect_cavities(layers, thicknesses);
        for (const SLA::CupHit& cup : cavities.cups)
            result.cup_opening_mm2 += cup.opening_area_mm2;
    }

    result.score =
        weights.peak_area * result.peak_area_mm2 + weights.cup_opening * result.cup_opening_mm2;

    return result;
}

} // namespace Slic3r::sla
