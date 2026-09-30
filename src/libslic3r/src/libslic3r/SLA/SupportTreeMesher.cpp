#include "SupportTreeMesher.hpp"

#include <cmath>
#include <cassert>
#include <cstdlib>

namespace Slic3r { namespace sla {

using Domain::its_merge;

indexed_triangle_set sphere(double rho, Portion portion, double fa) {

    indexed_triangle_set ret;

    // prohibit close to zero radius
    if(rho <= 1e-6 && rho >= -1e-6) return ret;

    auto& vertices = ret.vertices;
    auto& facets = ret.indices;

    // Algorithm:
    // Add points one-by-one to the sphere grid and form facets using relative
    // coordinates. Sphere is composed effectively of a mesh of stacked circles.

    // adjust via rounding to get an even multiple for any provided angle.
    double angle = (2 * PI / floor(2*PI / fa) );

    // Ring to be scaled to generate the steps of the sphere
    std::vector<double> ring;

    for (double i = 0; i < 2*PI; i+=angle) ring.emplace_back(i);

    const auto sbegin = size_t(2*std::get<0>(portion)/angle);
    const auto send = size_t(2*std::get<1>(portion)/angle);

    const size_t steps = ring.size();
    const double increment = 1.0 / double(steps);

    // special case: first ring connects to 0,0,0
    // insert and form facets.
    if (sbegin == 0)
        vertices.emplace_back(
            Vec3f(0.f, 0.f, float(-rho + increment * sbegin * 2. * rho)));

    auto id = coord_t(vertices.size());
    for (size_t i = 0; i < ring.size(); i++) {
        // Fixed scaling
        const double z = -rho + increment*rho*2.0 * (sbegin + 1.0);
        // radius of the circle for this step.
        const double r = std::sqrt(std::abs(rho*rho - z*z));
        Vec2d b = Eigen::Rotation2Dd(ring[i]) * Eigen::Vector2d(0, r);
        vertices.emplace_back(Vec3d(b(0), b(1), z).cast<float>());

        if (sbegin == 0)
            (i == 0) ? facets.emplace_back(Domain::Index3{coord_t(ring.size()), 0, 1}) :
                       facets.emplace_back(Domain::Index3{id - 1, 0, id});
        ++id;
    }

    // General case: insert and form facets for each step,
    // joining it to the ring below it.
    for (size_t s = sbegin + 2; s < send - 1; s++) {
        const double z = -rho + increment * double(s * 2. * rho);
        const double r = std::sqrt(std::abs(rho*rho - z*z));

        for (size_t i = 0; i < ring.size(); i++) {
            Vec2d b = Eigen::Rotation2Dd(ring[i]) * Eigen::Vector2d(0, r);
            vertices.emplace_back(Vec3d(b(0), b(1), z).cast<float>());
            auto id_ringsize = coord_t(id - int(ring.size()));
            if (i == 0) {
                // wrap around
                facets.emplace_back(Domain::Index3{id - 1, id, id + coord_t(ring.size() - 1)});
                facets.emplace_back(Domain::Index3{id - 1, id_ringsize, id});
            } else {
                facets.emplace_back(Domain::Index3{id_ringsize - 1, id_ringsize, id});
                facets.emplace_back(Domain::Index3{id - 1, id_ringsize - 1, id});
            }
            id++;
        }
    }

    // special case: last ring connects to 0,0,rho*2.0
    // only form facets.
    if(send >= size_t(2*PI / angle)) {
        vertices.emplace_back(0.f, 0.f, float(-rho + increment*send*2.0*rho));
        for (size_t i = 0; i < ring.size(); i++) {
            auto id_ringsize = coord_t(id - int(ring.size()));
            if (i == 0) {
                // third vertex is on the other side of the ring.
                facets.emplace_back(Domain::Index3{id - 1, id_ringsize, id});
            } else {
                auto ci = coord_t(id_ringsize + coord_t(i));
                facets.emplace_back(Domain::Index3{ci - 1, ci, id});
            }
        }
    }
    id++;

    return ret;
}

indexed_triangle_set pinhead(double r_pin,
                             double r_back,
                             double length,
                             size_t steps)
{
    return pinhead(HeadTipShape::Default, r_pin, r_back, length, steps);
}

indexed_triangle_set pinhead(HeadTipShape shape,
                             double      r_pin,
                             double      r_back,
                             double      length,
                             size_t      steps)
{
    assert(steps > 0);
    assert(length >= 0.);
    assert(r_back > 0.);
    assert(r_pin > 0.);

    indexed_triangle_set mesh;

    // We create two spheres which will be connected with a robe that fits
    // both circles perfectly.

    // Set up the model detail level
    const double detail = 2 * PI / steps;

    // We don't generate whole circles. Instead, we generate only the
    // portions which are visible (not covered by the robe) To know the
    // exact portion of the bottom and top circles we need to use some
    // rules of tangent circles from which we can derive (using simple
    // triangles the following relations:

    // The height of the whole mesh
    const double h   = r_back + r_pin + length;
    double       phi = PI / 2. - std::acos((r_back - r_pin) / h);

    if (std::isnan(phi))
        return mesh;

    // To generate a whole circle we would pass a portion of (0, Pi)
    // To generate only a half horizontal circle we can pass (0, Pi/2)
    // The calculated phi is an offset to the half circles needed to smooth
    // the transition from the circle to the robe geometry

    auto s1 = sphere(r_back, make_portion(0, PI / 2 + phi), detail);

    // The contact end of the head. The default pinhead closes the robe with the
    // visible part of a sphere of the front radius. The shapes of M2.16b close
    // that circle differently, each where the front sphere had its centre, so
    // the tip of the head ends up exactly as far into the model as the tip of
    // the default pinhead: the length of the head and with it the position of
    // the junction stay the same.
    indexed_triangle_set contact = sphere(r_pin, make_portion(PI / 2 + phi, PI), detail);
    for (auto &p : contact.vertices) p.z() += h;

    // How many corners a circle of this detail level has and the angle between
    // them: sphere() rounds the detail to a whole circle.
    const double  ring_angle = 2 * PI / std::floor(2 * PI / detail);
    const size_t  corners    = size_t(std::floor(2 * PI / ring_angle));

    if (shape == HeadTipShape::Cone) {
        // A point instead of the front sphere: the circle the robe ends at is
        // closed to a single vertex where the pole of the sphere was.
        contact.indices.clear();
        contact.vertices.resize(corners);
        contact.vertices.emplace_back(0.f, 0.f, float(h + r_pin));

        for (size_t i = 0; i < corners; ++i)
            contact.indices.emplace_back(Domain::Index3{coord_t(i),
                                                        coord_t((i + 1) % corners),
                                                        coord_t(corners)});
    } else if (shape == HeadTipShape::Ball) {
        // A ball of the front radius instead of the pinched cap of it. Its
        // equator is the circle the robe ends at now and sits where the front
        // sphere of the default pinhead had its centre, so the tip of the head
        // reaches as deep as the tip of the default one. Only the upper half of
        // the sphere is built: the lower one is inside the robe, and its circle
        // is the one the robe closes.
        contact.vertices.clear();
        contact.indices.clear();

        for (size_t i = 0; i < corners; ++i) {
            Vec2d b = Eigen::Rotation2Dd(double(i) * ring_angle) * Eigen::Vector2d(0, r_pin);
            contact.vertices.emplace_back(float(b(0)), float(b(1)), float(h));
        }
        contact.vertices.emplace_back(0.f, 0.f, float(h + r_pin));

        for (size_t i = 0; i < corners; ++i)
            contact.indices.emplace_back(Domain::Index3{coord_t(i),
                                                        coord_t((i + 1) % corners),
                                                        coord_t(corners)});
    }

    its_merge(mesh, s1);
    its_merge(mesh, contact);

    for (size_t idx1 = s1.vertices.size() - steps, idx2 = s1.vertices.size();
         idx1 < s1.vertices.size() - 1; idx1++, idx2++) {
        coord_t i1s1 = coord_t(idx1), i1s2 = coord_t(idx2);
        coord_t i2s1 = i1s1 + 1, i2s2 = i1s2 + 1;

        mesh.indices.emplace_back(Domain::Index3{i1s1, i2s1, i2s2});
        mesh.indices.emplace_back(Domain::Index3{i1s1, i2s2, i1s2});
    }

    auto i1s1 = coord_t(s1.vertices.size()) - coord_t(steps);
    auto i2s1 = coord_t(s1.vertices.size()) - 1;
    auto i1s2 = coord_t(s1.vertices.size());
    auto i2s2 = coord_t(s1.vertices.size()) + coord_t(steps) - 1;

    mesh.indices.emplace_back(Domain::Index3{i2s2, i2s1, i1s1});
    mesh.indices.emplace_back(Domain::Index3{i1s2, i2s2, i1s1});

    return mesh;
}

indexed_triangle_set halfcone(double       baseheight,
                              double       r_bottom,
                              double       r_top,
                              const Vec3d &pos,
                              size_t       steps)
{
    assert(steps > 0);

    if (baseheight <= 0 || steps <= 0 || (r_bottom <= 0. && r_top <= 0.))
        return {};

    indexed_triangle_set base;

    double a    = 2 * PI / steps;
    auto   last = int(steps - 1);
    Vec3d  ep{pos.x(), pos.y(), pos.z() + baseheight};
    for (size_t i = 0; i < steps; ++i) {
        double phi = i * a;
        auto x   = float(pos.x() + r_top * std::cos(phi));
        auto y   = float(pos.y() + r_top * std::sin(phi));
        base.vertices.emplace_back(x, y, float(ep.z()));
    }

    for (size_t i = 0; i < steps; ++i) {
        double phi = i * a;
        auto x   = float(pos.x() + r_bottom * std::cos(phi));
        auto y   = float(pos.y() + r_bottom * std::sin(phi));
        base.vertices.emplace_back(x, y, float(pos.z()));
    }

    base.vertices.emplace_back(pos.cast<float>());
    base.vertices.emplace_back(ep.cast<float>());

    auto &indices = base.indices;
    auto  hcenter = int(base.vertices.size() - 1);
    auto  lcenter = int(base.vertices.size() - 2);
    auto  offs    = int(steps);
    for (int i = 0; i < last; ++i) {
        indices.emplace_back(Domain::Index3{i, i + offs, offs + i + 1});
        indices.emplace_back(Domain::Index3{i, offs + i + 1, i + 1});
        indices.emplace_back(Domain::Index3{i, i + 1, hcenter});
        indices.emplace_back(Domain::Index3{lcenter, offs + i + 1, offs + i});
    }

    indices.emplace_back(Domain::Index3{0, last, offs});
    indices.emplace_back(Domain::Index3{last, offs + last, offs});
    indices.emplace_back(Domain::Index3{hcenter, last, 0});
    indices.emplace_back(Domain::Index3{offs, offs + last, lcenter});

    return base;
}

indexed_triangle_set polygon_cone(double       baseheight,
                                  double       r_bottom,
                                  double       r_top,
                                  const Vec3d &pos,
                                  size_t       sides,
                                  double       phase)
{
    if (baseheight <= 0 || sides < 3 || (r_bottom <= 0. && r_top <= 0.))
        return {};

    indexed_triangle_set base;

    double a    = 2 * PI / sides;
    auto   last = int(sides - 1);
    Vec3d  ep{pos.x(), pos.y(), pos.z() + baseheight};

    for (size_t i = 0; i < sides; ++i) {
        double phi = i * a + phase;
        base.vertices.emplace_back(float(pos.x() + r_top * std::cos(phi)),
                                   float(pos.y() + r_top * std::sin(phi)),
                                   float(ep.z()));
    }

    for (size_t i = 0; i < sides; ++i) {
        double phi = i * a + phase;
        base.vertices.emplace_back(float(pos.x() + r_bottom * std::cos(phi)),
                                   float(pos.y() + r_bottom * std::sin(phi)),
                                   float(pos.z()));
    }

    base.vertices.emplace_back(pos.cast<float>());
    base.vertices.emplace_back(ep.cast<float>());

    auto &indices = base.indices;
    auto  hcenter = int(base.vertices.size() - 1);
    auto  lcenter = int(base.vertices.size() - 2);
    auto  offs    = int(sides);
    for (int i = 0; i < last; ++i) {
        indices.emplace_back(Domain::Index3{i, i + offs, offs + i + 1});
        indices.emplace_back(Domain::Index3{i, offs + i + 1, i + 1});
        indices.emplace_back(Domain::Index3{i, i + 1, hcenter});
        indices.emplace_back(Domain::Index3{lcenter, offs + i + 1, offs + i});
    }

    indices.emplace_back(Domain::Index3{0, last, offs});
    indices.emplace_back(Domain::Index3{last, offs + last, offs});
    indices.emplace_back(Domain::Index3{hcenter, last, 0});
    indices.emplace_back(Domain::Index3{offs, offs + last, lcenter});

    return base;
}

indexed_triangle_set get_mesh(const Head &h, size_t steps)
{
    indexed_triangle_set mesh = pinhead(h.tip_shape, h.r_pin_mm, h.r_back_mm, h.width_mm, steps);

    for (auto& p : mesh.vertices) p.z() -= (h.fullwidth() - h.r_back_mm);

    using Quaternion = Eigen::Quaternion<float>;

       // We rotate the head to the specified direction. The head's pointing
       // side is facing upwards so this means that it would hold a support
       // point with a normal pointing straight down. This is the reason of
       // the -1 z coordinate
    auto quatern = Quaternion::FromTwoVectors(Vec3f{0.f, 0.f, -1.f},
                                              h.dir.cast<float>());

    Vec3f pos = h.pos.cast<float>();
    for (auto& p : mesh.vertices) p = quatern * p + pos;

    // The optional knot: a ball at the junction between the back of the
    // pinhead and the pillar. Its centre is the junction the pillar starts at,
    // so a knot thicker than the pillar also thickens the pillar there. (M2.16b)
    if (h.knot_radius_mm > 0.) {
        auto knot = sphere(h.knot_radius_mm, make_portion(0, PI), 2 * PI / steps);
        Vec3f jpos = h.junction_point().cast<float>();
        for (auto &p : knot.vertices) p += jpos;
        its_merge(mesh, knot);
    }

    return mesh;
}

indexed_triangle_set get_mesh(const Bridge &br, size_t steps)
{
    using Quaternion = Eigen::Quaternion<float>;
    Vec3d v = (br.endp - br.startp);
    Vec3d dir = v.normalized();
    double d = v.norm();

    indexed_triangle_set mesh = cylinder(br.r, d, steps);

    auto quater = Quaternion::FromTwoVectors(Vec3f{0.f, 0.f, 1.f},
                                             dir.cast<float>());

    Vec3f startp = br.startp.cast<float>();
    for(auto& p : mesh.vertices) p = quater * p + startp;

    return mesh;
}

indexed_triangle_set get_mesh(const DiffBridge &br, size_t steps)
{
    double h = br.get_length();
    indexed_triangle_set mesh = halfcone(h, br.r, br.end_r, Vec3d::Zero(), steps);

    using Quaternion = Eigen::Quaternion<float>;

       // We rotate the head to the specified direction. The head's pointing
       // side is facing upwards so this means that it would hold a support
       // point with a normal pointing straight down. This is the reason of
       // the -1 z coordinate
    auto quatern = Quaternion::FromTwoVectors(Vec3f{0.f, 0.f, 1.f},
                                              br.get_dir().cast<float>());

    Vec3f startp = br.startp.cast<float>();
    for(auto& p : mesh.vertices) p = quatern * p + startp;

    return mesh;
}

}} // namespace Slic3r::sla
