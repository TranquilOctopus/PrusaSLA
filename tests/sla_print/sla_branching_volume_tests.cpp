// M4.5a: how much resin the branching support tree spends, and the key that takes
// some of it out. A branch is built as thick as the weight of the node it hangs
// on, and the weight of a node is the length of the longest chain of branches
// merged into it, so a trunk is as thick as the whole subtree it holds up and the
// pillar of a tall model is the fattest thing in it. That is where most of the
// resin of a print goes: on the two models below the tree of today builds pillars
// of over a millimetre for a model of twenty.
//
// branchingsupport_pillar_radius_cap caps that growth at a multiple of the radius
// of the head the branch carries. The radius it is measured from is the radius of
// that head, which is the thinnest a branch ever is, so the cap can only take
// resin out of the widening and never out of the connection of a head. Zero, the
// default of the key, is the tree as it was built before.
//
// Both models are written in code with their support points on their own surface,
// so nothing but the cap and the config decide the tree. Every model is built
// twice, with the cap off and on, and the two trees are compared on three
// things: the resin they are made of, how many heads keep a branch, and whether
// any branch came out thinner than the head it serves.
//
// The tree is reproducible since M4.5c, which seeded the sampler of the points it
// merges branches between from the model and seeded every NLopt search per call,
// so both builds of a fixture are given the same candidates to choose from and
// the numbers below are of one tree rather than of two that happen to look alike.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "Slic3r/Biz/Algorithms/AABBMesh.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "libslic3r/SLA/BranchingTreeSLA.hpp"
#include "libslic3r/SLA/SupportTree.hpp"
#include "libslic3r/SLA/SupportTreeBuilder.hpp"
#include "libslic3r/SLA/SupportTreeMesher.hpp"

using Catch::Approx;
namespace triangle_mesh = Slic3r::Biz::Algorithms::TriangleMesh;
using Slic3r::Domain::ConfigItemDef;
using Slic3r::Domain::SLA::SupportPoint;
using Slic3r::Domain::SLA::SupportPoints;

namespace {

constexpr double pi = 3.14159265358979323846;

constexpr size_t steps = 45;

// The radius of the back of the pinhead the tree builds for a point that asks for
// no size of its own. Every node of these trees carries the largest head radius
// above it, so this is the radius a branch is at the very least, and the radius
// every cap of the test is a multiple of.
constexpr double head_radius = 0.5;

// A branch may get this many times as fat as the head it carries: 0.75 mm with the
// head above, where the tree of today builds pillars of 1.3 mm and more for a
// model of twenty.
constexpr double radius_cap = 1.5;

// The points of the point cloud are kept as float while the heads and the
// branches are built in double, so a branch that starts at a head is found by
// looking for its start point within this.
constexpr double at_point = 1e-3;

struct Fixture
{
    std::string          name;
    indexed_triangle_set mesh;
    SupportPoints        pts;
    double               elevation = 20.;
};

// A point on a sphere of the given radius whose centre is at the given height:
// theta away from the pole that points down, phi around it.
Slic3r::Domain::Vec3f on_sphere(double radius, double centre_z, double theta, double phi)
{
    const double st = std::sin(theta);

    return Slic3r::Domain::Vec3f{float(radius * st * std::cos(phi)),
                                 float(radius * st * std::sin(phi)),
                                 float(centre_z - radius * std::cos(theta))};
}

// The lower half of a sphere of the given radius, closed with a disc on its rim
// so that it is a solid the tree can tell inside from outside by. The faces are
// wound so that the normals point out of it.
indexed_triangle_set lower_hemisphere(double radius, double rim, int sectors, int rings)
{
    using Slic3r::Domain::Index3;

    indexed_triangle_set its;

    const auto ring_of = [sectors](int ring) { return 1 + (ring - 1) * sectors; };

    its.vertices.emplace_back(0.f, 0.f, float(rim - radius)); // the pole at the bottom
    for (int ring = 1; ring <= rings; ++ring)
        for (int sector = 0; sector < sectors; ++sector) {
            const double theta = pi / 2 * ring / rings;
            const double phi   = 2 * pi * sector / sectors;
            its.vertices.emplace_back(float(radius * std::sin(theta) * std::cos(phi)),
                                      float(radius * std::sin(theta) * std::sin(phi)),
                                      float(rim - radius * std::cos(theta)));
        }

    const int last = ring_of(rings);
    its.vertices.emplace_back(0.f, 0.f, float(rim)); // the centre of the closing disc

    // The fan from the pole down to the first ring.
    for (int sector = 0; sector < sectors; ++sector)
        its.indices.emplace_back(Index3{0, 1 + (sector + 1) % sectors, 1 + sector});

    // The rings between the pole and the rim.
    for (int ring = 1; ring < rings; ++ring) {
        const int a = ring_of(ring), b = ring_of(ring + 1);
        for (int sector = 0; sector < sectors; ++sector) {
            const int next = (sector + 1) % sectors;
            its.indices.emplace_back(Index3{a + sector, a + next, b + sector});
            its.indices.emplace_back(Index3{a + next, b + next, b + sector});
        }
    }

    // The disc that closes the rim, seen from above.
    for (int sector = 0; sector < sectors; ++sector)
        its.indices.emplace_back(Index3{last + sectors, last + sector,
                                        last + (sector + 1) % sectors});

    return its;
}

// A sphere of the given radius standing on the plate, with three rings of six
// support points on the lower third of it, which is as much of a sphere as a head
// fits under: a head needs the surface to face down within 30 degrees of straight
// down (SupportTreeConfig::normal_cutoff_angle).
Fixture sphere_fixture()
{
    constexpr double radius = 20.;

    Fixture fx;
    fx.name      = "sphere";
    // Enough of a gap under the sphere that the tree of today builds a pillar
    // fatter than the cap, which is what the cap then takes the resin out of.
    fx.elevation = 30.;
    // The second argument of its_make_sphere is the wanted edge length relative
    // to the radius, which puts the facets a few hundredths of a millimetre off
    // the smooth surface the points are placed on.
    fx.mesh = triangle_mesh::its_make_sphere(radius, 0.08);
    for (auto &v : fx.mesh.vertices)
        v.z() += float(radius); // it is centred on the origin

    for (int ring = 1; ring <= 3; ++ring)
        for (int sector = 0; sector < 6; ++sector) {
            SupportPoint p;
            p.pos               = on_sphere(radius, radius, (15. * ring) * pi / 180.,
                                            (2. * pi * sector) / 6.);
            p.head_front_radius = 0.2f;
            fx.pts.push_back(p);
        }

    return fx;
}

// A helmet: a dome of the given radius with its curved side down, held above the
// plate by a stem, so the points under the dome have a trunk of the stem's height
// to hang on and the stem is in the way of the branches that reach the plate.
Fixture dome_fixture()
{
    constexpr double radius = 20., rim = 45., stem_r = 6.;

    Fixture fx;
    fx.name      = "helmet-like dome";
    fx.elevation = 35.;
    fx.mesh      = lower_hemisphere(radius, rim, 24, 8);

    indexed_triangle_set stem =
        triangle_mesh::its_make_cylinder(stem_r, rim - radius, 2 * pi / 24);
    // The stem is a primitive of the mesh library, whose faces may be ordered
    // either way, and the tree reads the winding of the model to tell a hit from
    // a miss.
    if (Slic3r::Domain::its_volume(stem) < 0.f)
        for (auto &f : stem.indices)
            std::swap(f[0], f[2]);
    Slic3r::Domain::its_merge(fx.mesh, stem);

    // The rings stay clear of the stem, so that a branch of a point has somewhere
    // to go beside it.
    for (int ring = 1; ring <= 3; ++ring)
        for (int sector = 0; sector < 6; ++sector) {
            SupportPoint p;
            p.pos               = on_sphere(radius, rim, (10. + 15. * ring) * pi / 180.,
                                            (2. * pi * sector) / 6.);
            p.head_front_radius = 0.2f;
            fx.pts.push_back(p);
        }

    return fx;
}

Slic3r::sla::SupportableMesh make_supportable_mesh(const Fixture &fx, double cap)
{
    Slic3r::sla::SupportTreeConfig cfg;
    cfg.object_elevation_mm = fx.elevation;
    cfg.pillar_radius_cap   = cap;
    // Every branch of these trees has to reach the plate, so what the case
    // measures is the branches and the pillars and nothing else. It also keeps
    // the model out of the point cloud the tree merges branches between, which
    // is sampled at random.
    cfg.ground_facing_only = true;

    return Slic3r::sla::SupportableMesh{
        .emesh = Slic3r::AABBMesh(fx.mesh),
        .pts   = std::make_shared<const SupportPoints>(fx.pts),
        .cfg   = cfg};
}

// The resin the tree is made of, primitive by primitive. The primitives overlap
// where they meet, so a joint is counted once per primitive and the number is an
// upper bound of what the print holds. It is the same measure for both trees,
// which is all the comparison needs.
double tree_volume(const Slic3r::sla::SupportTreeBuilder &builder)
{
    const auto vol = [](const indexed_triangle_set &m) {
        return std::abs(double(Slic3r::Domain::its_volume(m)));
    };

    double v = 0.;

    for (const auto &h : builder.heads())
        if (h.is_valid())
            v += vol(Slic3r::sla::get_mesh(h, steps));
    for (const auto &p : builder.pillars())
        v += vol(Slic3r::sla::get_mesh(p, steps));
    for (const auto &p : builder.pedestals())
        v += vol(Slic3r::sla::get_mesh(p, steps));
    for (const auto &j : builder.junctions())
        v += vol(Slic3r::sla::get_mesh(j, steps));
    for (const auto &b : builder.diffbridges())
        v += vol(Slic3r::sla::get_mesh(b, steps));
    for (const auto &a : builder.anchors())
        v += vol(Slic3r::sla::get_mesh(a, steps));

    return v;
}

// The branch that starts at this head, if the tree built one. A leaf that hangs
// on a branch gets a cone at its junction point, a leaf that reaches the plate
// on its own gets the top of a pillar there.
struct Branch
{
    bool   found  = false;
    double radius = 0.;
};

Branch branch_at(const Slic3r::sla::SupportTreeBuilder &builder,
                 const Slic3r::sla::Head                &head)
{
    const Slic3r::Vec3d at = head.junction_point();

    for (const auto &b : builder.diffbridges())
        if ((b.startp - at).norm() <= at_point)
            return {true, b.r};

    for (const auto &p : builder.pillars())
        if ((p.startpoint() - at).norm() <= at_point)
            return {true, p.r_start};

    return {};
}

struct TreeStats
{
    double volume    = 0.; // the resin the tree is made of
    double max_rad   = 0.; // the fattest branch in it
    size_t connected = 0;  // heads a branch starts at
    size_t loose     = 0;  // heads the builder kept although no branch reaches them
    size_t dropped   = 0;  // heads the tree gave up on
    size_t pillars   = 0;
    size_t branches  = 0; // the cones between two nodes
};

TreeStats measure(const Slic3r::sla::SupportTreeBuilder &builder)
{
    TreeStats s;

    s.volume  = tree_volume(builder);
    s.pillars = builder.pillars().size();
    s.branches = builder.diffbridges().size();

    const auto widen = [&s](double r) { s.max_rad = std::max(s.max_rad, r); };
    for (const auto &b : builder.diffbridges()) {
        widen(b.r);
        widen(b.end_r);
    }
    for (const auto &p : builder.pillars()) {
        widen(p.r_start);
        widen(p.r_end);
    }
    for (const auto &j : builder.junctions())
        widen(j.r);

    for (const auto &h : builder.heads()) {
        if (!h.is_valid()) {
            ++s.dropped;
            continue;
        }

        const Branch b = branch_at(builder, h);
        if (!b.found) {
            ++s.loose;
            continue;
        }

        ++s.connected;

        // The one thing the cap must not do: a branch has to be at least as fat
        // as the head it carries, or the head would hang on resin thinner than
        // itself.
        INFO("head at " << h.pos.x() << ", " << h.pos.y() << ", " << h.pos.z()
                        << " with a branch of " << b.radius << " and a head radius of "
                        << h.junction_radius());
        CHECK(b.radius >= h.junction_radius());
    }

    return s;
}

TreeStats build_tree_of(const Fixture                  &fx,
                        double                           cap,
                        Slic3r::sla::SupportTreeBuilder &builder)
{
    const Slic3r::sla::SupportableMesh sm = make_supportable_mesh(fx, cap);

    Slic3r::sla::create_branching_tree(builder, sm);

    return measure(builder);
}

// What one of the two trees came out as, as a line of log for the case.
std::string describe(const TreeStats &s)
{
    std::stringstream ss;

    ss << "volume " << s.volume << " mm3, fattest branch " << s.max_rad << " mm, "
       << s.connected << " heads with a branch, " << s.loose << " without, " << s.dropped
       << " dropped, " << s.pillars << " pillars, " << s.branches << " branches";

    return ss.str();
}

// The two trees of one model, with the cap off and on.
void check_volume(const Fixture &fx)
{
    Slic3r::sla::SupportTreeBuilder off;
    Slic3r::sla::SupportTreeBuilder on;

    const TreeStats base = build_tree_of(fx, 0., off);
    const TreeStats thin = build_tree_of(fx, radius_cap, on);

    // Everything either tree came out as is reported, so the numbers a run of the
    // benchmark set can be compared with are in the log of this case.
    INFO("model " << fx.name << ": " << fx.pts.size() << " points, "
                  << fx.elevation << " mm elevation");
    INFO("cap off: " << describe(base));
    INFO("cap on: " << describe(thin));

    // Without the cap the tree of today is built, and it is fatter than the cap
    // on these models: if it were not, the cap would have nothing to take out
    // here and the rest of the case would prove nothing.
    REQUIRE(base.connected > 0);
    CHECK(base.max_rad > radius_cap * head_radius);

    // With the cap on, no branch of the tree is fatter than the head it carries.
    CHECK(thin.max_rad <= radius_cap * head_radius + at_point);

    // And the tree is made of less resin for it.
    CHECK(thin.volume < base.volume);

    // Nothing of the connection of a head is given up for the resin: no head
    // that had a branch loses it, and no head is left without a branch by more
    // of them than before.
    CHECK(thin.connected >= base.connected);
    CHECK(thin.loose <= base.loose);
}

const ConfigItemDef *find_def(const std::string &name)
{
    for (const auto &def : Slic3r::Domain::get_defs_sla().defs())
        if (def.name == name)
            return &def;

    return nullptr;
}

} // namespace

TEST_CASE("BranchingSupportTree::The radius cap of a sphere takes resin, not heads",
          "[suptreetree]")
{
    check_volume(sphere_fixture());
}

TEST_CASE("BranchingSupportTree::The radius cap of a helmet-like dome takes resin, not heads",
          "[suptreetree]")
{
    check_volume(dome_fixture());
}

TEST_CASE("A branch is as thick as today until a cap is asked for", "[suptreetree]")
{
    Slic3r::sla::SupportTreeConfig cfg;

    // Nothing configured: the tree is the one that was built before the key.
    CHECK(cfg.pillar_radius_cap == Approx(0.));
    REQUIRE(cfg.pillar_widening_factor == Approx(0.5));

    const double head_of = Slic3r::sla::SupportTreeConfig{}.head_back_radius_mm;
    CHECK(head_of == Approx(head_radius));

    // The radius the tree of today builds a node with: the radius of the head it
    // carries plus the widening its weight earns.
    const auto widened = [cfg](double r_min, double weight) {
        return r_min + Slic3r::sla::branching_widening_scale * cfg.pillar_widening_factor *
                           weight;
    };

    for (const double r_min : {0.25, 0.5, 1.}) {
        for (const double weight : {0., 10., 100., 500.}) {
            INFO("radius " << r_min << " of a node of weight " << weight);
            CHECK(Slic3r::sla::branch_radius(cfg, r_min, weight) == widened(r_min, weight));

            // A cap that no radius of this tree reaches is the tree of today, so
            // that is what the default of the key has to mean.
            Slic3r::sla::SupportTreeConfig roomy = cfg;
            roomy.pillar_radius_cap = 20.;
            CHECK(Slic3r::sla::branch_radius(roomy, r_min, weight) == widened(r_min, weight));

            // A cap that does reach it takes the widening off and nothing else. A
            // cap below one is read as one, since a branch thinner than its head
            // is not a branch.
            for (const double cap : {0.1, 0.5, 1., 2., 3., 10.}) {
                Slic3r::sla::SupportTreeConfig capped = cfg;
                capped.pillar_radius_cap = cap;

                const double r      = Slic3r::sla::branch_radius(capped, r_min, weight);
                const double tight = std::max(1., cap);
                CHECK(r == std::min(widened(r_min, weight), tight * r_min));
                CHECK(r >= r_min);
                CHECK(r <= widened(r_min, weight));
            }
        }
    }
}

TEST_CASE("The branchingsupport_pillar_radius_cap setting sits with the widening factor",
          "[suptreetree]")
{
    const ConfigItemDef *widening = find_def("branchingsupport_pillar_widening_factor");
    const ConfigItemDef *cap      = find_def("branchingsupport_pillar_radius_cap");

    REQUIRE(widening != nullptr);
    REQUIRE(cap != nullptr);

    // Hidden and next to the factor it limits, like the other settings of the
    // branching tree, and it starts at the tree of today.
    CHECK(cap->category == ConfigItemDef::Category::Hidden);
    CHECK(cap->category == widening->category);
    CHECK(cap->option_group == widening->option_group);
    CHECK(cap->gui_type == ConfigItemDef::GUIType::textfield);
    CHECK_FALSE(cap->row_group.empty());
    CHECK_FALSE(cap->tooltip.empty());
    CHECK(cap->init_fn().get<double>() == Approx(0.));
    REQUIRE(cap->min.has_value());
    CHECK(cap->min.value() == Approx(0.));
    REQUIRE(cap->max.has_value());
    CHECK(cap->max.value() > cap->min.value());

    // Only the branching tree grows its branches with the weight of the subtree
    // they carry, so the cap is its key alone.
    CHECK(find_def("support_pillar_radius_cap") == nullptr);
}
