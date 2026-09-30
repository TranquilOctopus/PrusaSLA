#ifndef SUPPORTTREEMESHER_HPP
#define SUPPORTTREEMESHER_HPP

#include <stddef.h>
#include <algorithm>
#include <tuple>
#include <cstddef>

#include "libslic3r/Point.hpp"
#include "libslic3r/SLA/SupportTreeBuilder.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "admesh/stl.h"
#include "libslic3r/libslic3r.h"
//#include "libslic3r/SLA/Contour3D.hpp"

namespace Slic3r { namespace sla {

// The shape of the contact (tip) end of a head, the same choice a support point
// carries as its per point tip_shape: Default is the two sphere pinhead this
// slicer has always built, Cone a pointed cone and Ball a full sphere of the
// front radius. Only the contact end changes, the back sphere, the robe and
// with them the length and the junction of the head are the same for all three.
using HeadTipShape = Domain::SLA::SupportPoint::TipShape;

using Portion = std::tuple<double, double>;

inline Portion make_portion(double a, double b)
{
    return std::make_tuple(a, b);
}

indexed_triangle_set sphere(double  rho,
                            Portion portion = make_portion(0., PI),
                            double  fa      = (2. * PI / 360.));

// Down facing cylinder in Z direction with arguments:
// r: radius
// h: height
// ssteps: how many edges will create the base circle
// sp: starting point
inline indexed_triangle_set cylinder(double       r,
                              double       h,
                              size_t       steps = 45)
{

    namespace TriMesh = Biz::Algorithms::TriangleMesh;
    return TriMesh::its_make_cylinder(r, h, 2 * PI / steps);
}

indexed_triangle_set pinhead(double r_pin,
                             double r_back,
                             double length,
                             size_t steps = 45);

// The pinhead with the contact end shaped as the support point asked for:
// Default is the pinhead above, Cone has no front sphere but a point where its
// pole was, and Ball a ball of the front radius. In all three the head is
// as long as the default pinhead and reaches into the model as deep, so only
// the contact itself is different and the pillar is placed at the same junction.
indexed_triangle_set pinhead(HeadTipShape shape,
                             double      r_pin,
                             double      r_back,
                             double      length,
                             size_t      steps = 45);

indexed_triangle_set halfcone(double       baseheight,
                              double       r_bottom,
                              double       r_top,
                              const Vec3d &pt    = Vec3d::Zero(),
                              size_t       steps = 45);

// The thinnest a flat disc pillar base may get, no matter which base height is configured.
inline constexpr double flat_base_height_mm = 0.5;

// A down facing prism or frustum with a regular polygon cross section of the
// given number of corners. The radii are the circumscribed ones, so a polygon
// of the same radius carries about as much resin as the round one, and the
// triangle count is bounded by 4 * sides instead of 4 * steps.
indexed_triangle_set polygon_cone(double       baseheight,
                                  double       r_bottom,
                                  double       r_top,
                                  const Vec3d &pt,
                                  size_t       sides,
                                  double       phase = 0.);

indexed_triangle_set get_mesh(const Head &h, size_t steps);

inline indexed_triangle_set get_mesh(const Pillar &p, size_t steps)
{
    if(p.height > EPSILON) { // Endpoint is below the starting point
        // We just create a bridge geometry with the pillar parameters and
        // move the data.
        //return cylinder(p.r_start, p.height, steps, p.endpoint());
        // A support point may ask for a regular polygon stem (M2.16b): the
        // sides are the corners of the cross section, the radii stay the
        // circumscribed ones.
        if (!p.stem.round())
            return polygon_cone(p.height, p.r_end, p.r_start, p.endpt,
                                p.stem.sides, PI / p.stem.sides);

        return halfcone(p.height, p.r_end, p.r_start, p.endpt, steps);
    }

    return {};
}

inline indexed_triangle_set get_mesh(const Pedestal &p, size_t steps)
{
    if (p.height <= 0. || steps <= 0)
        return {};

    // A cylinder is a straight foot of the base diameter and height, a flat disc a thin foot
    // of that diameter: however tall the base is configured, a disc stays at most
    // flat_base_height_mm thick, and the pillar above it runs straight down into it.
    if (p.shape == Domain::sla::SupportBaseShape::Cylinder ||
        p.shape == Domain::sla::SupportBaseShape::Flat) {
        const double h = p.shape == Domain::sla::SupportBaseShape::Flat ?
                             std::min(p.height, flat_base_height_mm) :
                             p.height;

        if (p.r_bottom <= 0. || h <= 0.)
            return {};

        indexed_triangle_set mesh = cylinder(p.r_bottom, h, steps);
        Vec3f pos = p.pos.cast<float>();
        for (auto &v : mesh.vertices) v += pos;

        return mesh;
    }

    return halfcone(p.height, p.r_bottom, p.r_top, p.pos, steps);
}

inline indexed_triangle_set get_mesh(const Junction &j, size_t steps)
{
    indexed_triangle_set mesh = sphere(j.r, make_portion(0, PI), 2 * PI / steps);
    Vec3f pos = j.pos.cast<float>();
    for(auto& p : mesh.vertices) p += pos;
    return mesh;
}

indexed_triangle_set get_mesh(const Bridge &br, size_t steps);

indexed_triangle_set get_mesh(const DiffBridge &br, size_t steps);

}} // namespace Slic3r::sla

#endif // SUPPORTTREEMESHER_HPP
