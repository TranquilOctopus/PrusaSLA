#include "libslic3r/SLA/HollowingLattice.hpp"

#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/CGAL/Algorithms/MeshBoolean.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"
#include "Slic3r/Exception.hpp"
#include "Slic3r/Log.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace Slic3r {

namespace MeshBoolean = Biz::CGAL::Algorithms::MeshBoolean;

namespace sla {

namespace {

// Two struts of one direction may not touch. A pair of boxes that overlap is not a solid a mesh
// boolean can take, and the struts of one direction are exactly what the boolean gets, so a strut
// is never made as wide as the spacing: a tenth of the spacing is always kept as the gap between
// two neighbours.
constexpr double MIN_STRUT_GAP_RATIO = 0.1;

// Never more than this many struts along one axis. A fine spacing in a big cavity would ask for
// hundreds of thousands of boxes, which is neither a print anyone wants nor a boolean that ever
// finishes. The spacing is stretched to fit instead, so the pattern stays even.
constexpr size_t MAX_STRUTS_PER_AXIS = 128;

// The axes of the struts of one direction: as many as fit at the wanted spacing in the interval,
// and as close to the middle of the interval as that many allow.
std::vector<double> strut_axes(double from, double to, double spacing_mm)
{
    std::vector<double> axes;

    if (!(spacing_mm > 0.) || !(to > from))
        return axes;

    const double length = to - from;
    // One axis is always there, a cavity thinner than the spacing still gets a strut in it. As many
    // as fit, and no more: one more would leave the outermost struts hanging over the ends of the
    // interval, and the spacing is the gap between two neighbours, not a distance to the end.
    const double wanted = length / spacing_mm;
    size_t count        = MAX_STRUTS_PER_AXIS;
    double step         = spacing_mm;

    if (wanted < double(MAX_STRUTS_PER_AXIS)) {
        count = std::max(size_t(1), size_t(wanted));
    } else {
        // The spacing would mean more struts than the cap allows, so it is stretched to spread them
        // over the whole interval instead. The pattern stays even and the cavity stays filled.
        step = length / double(count - 1);
    }

    const double middle = 0.5 * (from + to);

    axes.reserve(count);
    for (size_t i = 0; i < count; ++i)
        axes.push_back(middle + step * (double(i) - 0.5 * double(count - 1)));

    return axes;
}

// A square prism of width_mm, centred on the line (u, v) of the two axes that are not its own, and
// running the whole length of the box along that own axis, reaching overlap_mm past both ends of
// that length.
indexed_triangle_set make_strut(
    const Domain::BoundingBox3d &bb,
    int                          axis,
    double                       u,
    double                       v,
    double                       width_mm,
    double                       overlap_mm
)
{
    using Biz::Algorithms::TriangleMesh::its_make_cube;

    const int u_axis  = (axis + 1) % 3;
    const int v_axis  = (axis + 2) % 3;
    const double half = 0.5 * width_mm;

    Domain::Vec3d size = Domain::Vec3d::Zero();
    size[axis]         = bb.max[axis] - bb.min[axis] + 2. * overlap_mm;
    size[u_axis]       = width_mm;
    size[v_axis]       = width_mm;

    // its_make_cube builds from the origin, so the strut starts at the near end of the box, less the
    // overlap when there is one, and at its own centre line less half its width on the two other
    // axes.
    Domain::Vec3d origin = bb.min;
    origin[axis]         -= overlap_mm;
    origin[u_axis]        = u - half;
    origin[v_axis]        = v - half;

    const Domain::Vec3f offset = origin.cast<float>();
    indexed_triangle_set strut = its_make_cube(size.x(), size.y(), size.z());
    for (Domain::Vec3f &p : strut.vertices)
        p += offset;

    return strut;
}

// All the struts of one direction inside the box: a grid of square prisms. The number of them is
// the product of the number of axes of the two directions they cross, which is the reason for the
// cap in strut_axes. A strut reaches overlap_mm past both ends of the box along its own axis, which
// puts its ends inside the wall of the cavity and off the plane of that wall.
indexed_triangle_set make_strut_bundle(
    const Domain::BoundingBox3d &bb,
    int                          axis,
    double                       width_mm,
    double                       spacing_mm,
    double                       overlap_mm
)
{
    const std::vector<double> u_axes =
        strut_axes(bb.min[(axis + 1) % 3], bb.max[(axis + 1) % 3], spacing_mm);
    const std::vector<double> v_axes =
        strut_axes(bb.min[(axis + 2) % 3], bb.max[(axis + 2) % 3], spacing_mm);

    indexed_triangle_set bundle;
    for (double u : u_axes)
        for (double v : v_axes)
            Domain::its_merge(bundle, make_strut(bb, axis, u, v, width_mm, overlap_mm));

    return bundle;
}

// The axes the pattern stands on: the columns of a grid run along Z, and a cubic lattice adds the
// same columns along X and along Y.
std::vector<int> lattice_axes(Domain::sla::HollowingInfillType type)
{
    switch (type) {
    case Domain::sla::HollowingInfillType::Grid:
        return {2};
    case Domain::sla::HollowingInfillType::Cubic:
        return {0, 1, 2};
    case Domain::sla::HollowingInfillType::None:
        break;
    }

    return {};
}

} // namespace

std::vector<double> hollowing_lattice_axes(double from, double to, double spacing_mm)
{
    return strut_axes(from, to, spacing_mm);
}

std::vector<indexed_triangle_set> make_hollowing_lattice(
    const Domain::BoundingBox3d &bb,
    const HollowingInfillConfig &cfg,
    const JobController &ctl,
    double             wall_overlap_mm
)
{
    std::vector<indexed_triangle_set> lattice;

    if (!bb.defined)
        return lattice;

    for (int axis : lattice_axes(cfg.type)) {
        if (ctl.stopcondition && ctl.stopcondition())
            return {};

        // A strut as wide as the spacing would touch its neighbour, and two touching boxes are not
        // a solid the boolean can take.
        const double width = std::min(cfg.strut_mm, cfg.spacing_mm * (1. - MIN_STRUT_GAP_RATIO));
        if (!(width > 0.))
            continue;

        indexed_triangle_set bundle =
            make_strut_bundle(bb, axis, width, cfg.spacing_mm, wall_overlap_mm);

        if (!bundle.indices.empty())
            lattice.emplace_back(std::move(bundle));
    }

    return lattice;
}

bool subtract_lattice_from_cavity(
    indexed_triangle_set &cavity,
    const HollowingInfillConfig &cfg,
    const JobController &ctl
)
{
    if (cavity.indices.empty())
        return false;

    // The struts are built to reach past the bounds of the box they stand in, so that they end
    // inside the wall of the cavity and cross it. A strut that stopped at the bound would end with
    // a face in the plane of the wall of the cavity, which is the plane the flat of that wall lies
    // in, and a boolean of two surfaces that share a face over the whole of a strut cross section
    // is the one configuration corefinement does not answer (see LATTICE_WALL_OVERLAP_MM).
    const std::vector<indexed_triangle_set> lattice =
        make_hollowing_lattice(Domain::bounding_box(cavity), cfg, ctl, LATTICE_WALL_OVERLAP_MM);

    if (lattice.empty())
        return false;

    // One direction at a time: the struts of a direction are disjoint, so every one of them is a
    // solid the boolean can take, while the directions cross each other and could not be one mesh.
    indexed_triangle_set carved = cavity;
    try {
        for (const indexed_triangle_set &struts : lattice) {
            if (ctl.stopcondition && ctl.stopcondition())
                return false;

            MeshBoolean::cgal::minus(carved, struts);
        }
    } catch (const Slic3r::RuntimeError &) {
        SPDLOG_WARN(
            "Hollowing: the infill lattice could not be cut out of the cavity, "
            "hollowing without it."
        );
        return false;
    } catch (const Slic3r::HardCrash &) {
        SPDLOG_WARN("Hollowing: the infill lattice crashed a mesh boolean, hollowing without it.");
        return false;
    }

    if (carved.indices.empty())
        return false;

    cavity = std::move(carved);
    return true;
}

} // namespace sla
} // namespace Slic3r
