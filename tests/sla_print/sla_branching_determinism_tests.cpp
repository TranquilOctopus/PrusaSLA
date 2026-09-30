// M4.5c: the branching tree is the same tree twice, and every head of it belongs
// to the support point it was built for.
//
// The tree of a model used to come out differently from one run to the next, for
// three reasons. The points of the bed and of the model it merges branches between
// were drawn from std::rand (libigl's random_points_on_mesh on Eigen's generator,
// BranchingTree/PointCloud.cpp); the searches of the leaves - the ground ones and
// the pinhead ones - ran on several threads over the one process wide generator
// NLopt draws from, so they raced each other; and the candidates a node tries were
// sorted by their distance alone, which leaves two of them at the same distance in
// whatever order the point cloud handed them over. All three are seeded or ordered
// from the input now: the sampler hashes the mesh and the radius it samples at,
// every search is seeded and runs to its end before the next one starts, and a tie
// between two candidates is broken by node id. The cases below build the same model
// five times over and compare the vertex and face data of the trees bit for bit,
// and build it once more with the thread count of the whole process pinned to one.
//
// The second half is the head bookkeeping. create_branching_tree used to add every
// head under the number of the leaf it was built for, and to give a head up by
// that number as well. The leaf of a head is not its support point: a point that
// gets no head of its own and a point that is dropped as the duplicate of a point
// next to it both move the leaves off the support points, so the head asked for by
// the number of a leaf is the head of some other point from there on. Heads are
// added under the id of their support point now, the builder remembers which ids
// have a head at all, and a head is given up by the id of its own support point.
#include <catch2/catch_test_macros.hpp>

#include <tbb/global_control.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "Slic3r/Biz/Algorithms/AABBMesh.hpp"
#include "Slic3r/Biz/Algorithms/Execution/Execution.hpp"
#include "Slic3r/Biz/Algorithms/Execution/ExecutionSeq.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "libslic3r/SLA/BranchingTreeSLA.hpp"
#include "libslic3r/SLA/SupportTree.hpp"
#include "libslic3r/SLA/SupportTreeBuilder.hpp"
#include "libslic3r/SLA/SupportTreeUtils.hpp"

namespace triangle_mesh = Slic3r::Biz::Algorithms::TriangleMesh;
namespace execution     = Slic3r::Biz::Algorithms::Execution;
using Slic3r::Domain::SLA::SupportPoint;
using Slic3r::Domain::SLA::SupportPoints;
using Slic3r::sla::Head;
using Slic3r::sla::SupportTreeBuilder;

namespace {

constexpr double pi = 3.14159265358979323846;

// A 20 mm sphere: three rings of six points on its lower third, which is as much
// of a sphere as a head fits under, a head needing the surface to face down within
// 30 degrees of straight down (SupportTreeConfig::normal_cutoff_angle).
constexpr double sphere_radius = 20.;

// The two points of the close pair are this far apart, half of the 0.1 mm that
// create_branching_tree drops a duplicate at.
constexpr double pair_distance = 0.05;

// Where the close pair of the fixture below sits: a ring of its own, between two
// rings and two sectors of the sphere, so that the two of them are the only points
// anywhere near each other.
constexpr double pair_theta = 7.5 * pi / 180.;
constexpr double pair_phi   = 30. * pi / 180.;

// A point on a sphere of the given radius whose centre is at the given height:
// theta away from the pole that points down, phi around it.
Slic3r::Domain::Vec3f on_sphere(double radius, double centre_z, double theta, double phi)
{
    const double st = std::sin(theta);

    return Slic3r::Domain::Vec3f{
        float(radius * st * std::cos(phi)),
        float(radius * st * std::sin(phi)),
        float(centre_z - radius * std::cos(theta))
    };
}

// The heads and the tree are built in double while the points of the point cloud
// are kept as float, so a head is recognized by where it is rather than by the
// number it carries.
constexpr double at_point = 1e-3;

struct Fixture
{
    std::string name;
    SupportPoints pts;
};

// Two rings of six points on the lower third of the sphere. Two rings are enough
// for a branch of a point to meet the branch of another one on the bed, and the
// trees of the repeat builds below are trees of twelve points rather than of
// eighteen.
constexpr int rings = 2;

void ring_points(Fixture &fx)
{
    for (int ring = 1; ring <= rings; ++ring)
        for (int sector = 0; sector < 6; ++sector) {
            SupportPoint p;
            p.pos = on_sphere(
                sphere_radius,
                sphere_radius,
                (15. * ring) * pi / 180.,
                (2. * pi * sector) / 6.
            );
            p.head_front_radius = 0.2f;
            fx.pts.push_back(p);
        }
}

// The plain model: twelve points, no two of them near each other, all of them
// with a head under them.
Fixture plain_fixture()
{
    Fixture fx;
    fx.name = "sphere with twelve points";
    ring_points(fx);

    return fx;
}

// The same model with two things in it that a head number cannot tell apart from
// the number of its leaf: a pair of points 0.05 mm apart, of which the tree keeps
// one and drops the other as its duplicate, and a point on the upper part of the
// sphere where the surface does not face down, which gets no head at all.
Fixture close_points_fixture()
{
    Fixture fx;
    fx.name = "sphere with a close pair and a headless point";
    ring_points(fx);

    // The first ring of six, then the pair, then the rest, so that the point that
    // keeps its head is not the last one and the leaves after it all move up by one.
    auto after_first_ring   = fx.pts.begin() + 6;
    SupportPoint first      = *after_first_ring;
    SupportPoint second     = first;
    first.pos               = on_sphere(sphere_radius, sphere_radius, pair_theta, pair_phi);
    first.head_front_radius = 0.2f;
    second.pos              = first.pos;
    second.pos.z() += float(pair_distance);
    second.head_front_radius = 0.2f;
    fx.pts.insert(after_first_ring, {first, second});

    // On the upper part of the sphere, where no head fits under the surface.
    SupportPoint up;
    up.pos               = on_sphere(sphere_radius, sphere_radius, 170. * pi / 180., 0.);
    up.head_front_radius = 0.2f;
    fx.pts.push_back(up);

    return fx;
}

indexed_triangle_set sphere_mesh()
{
    // The second argument of its_make_sphere is the wanted edge length relative to
    // the radius, which puts the facets a few hundredths of a millimetre inside the
    // smooth surface the points are placed on.
    indexed_triangle_set mesh = triangle_mesh::its_make_sphere(sphere_radius, 0.08);

    for (auto &v : mesh.vertices)
        v.z() += float(sphere_radius); // it is centred on the origin

    return mesh;
}

Slic3r::sla::SupportableMesh make_supportable_mesh(const Fixture &fx)
{
    Slic3r::sla::SupportTreeConfig cfg;

    // Enough of a gap under the sphere that the branches of the points reach the
    // bed and merge there, which is where the sampled points are picked from.
    cfg.object_elevation_mm = 30.;
    // Every branch of this tree has to reach the plate, so what the cases measure
    // is the branches and the pillars and nothing else.
    cfg.ground_facing_only = true;

    return Slic3r::sla::SupportableMesh{
        .emesh = Slic3r::AABBMesh(sphere_mesh()),
        .pts   = std::make_shared<const SupportPoints>(fx.pts),
        .cfg   = cfg
    };
}

// One tree, and the mesh it came out as. The builder owns the mesh, so it lives
// next to it.
struct Tree
{
    SupportTreeBuilder builder;
    uint64_t hash = 0;
};

uint64_t bits_of(float f)
{
    return uint64_t(std::bit_cast<uint32_t>(f));
}

// FNV-1a, 64 bit, the same construction as sla_layer_hash_tests.cpp: integers
// only, never raw struct bytes, so the value does not depend on padding, on
// endianness or on the allocator.
class Fnv1a
{
public:
    void feed(uint64_t value)
    {
        for (int byte = 0; byte < 8; ++byte) {
            m_hash ^= (value >> (byte * 8)) & 0xffu;
            m_hash *= 0x100000001b3ull;
        }
    }

    uint64_t value() const
    {
        return m_hash;
    }

private:
    uint64_t m_hash = 0xcbf29ce484222325ull;
};

uint64_t mesh_hash(const indexed_triangle_set &m)
{
    Fnv1a h;

    h.feed(m.vertices.size());
    h.feed(m.indices.size());

    for (const auto &v : m.vertices)
        for (int d = 0; d < 3; ++d)
            h.feed(bits_of(v[d]));

    for (const auto &f : m.indices)
        for (int d = 0; d < 3; ++d)
            h.feed(uint64_t(uint32_t(f[d])));

    return h.value();
}

// The first vertex or face two meshes of a tree disagree on, and an empty string
// when they are the same mesh down to the last bit. The bits of a float are
// compared rather than the numbers, so a tree that moved by a single epsilon is
// not the same tree.
std::string first_difference(const indexed_triangle_set &a, const indexed_triangle_set &b)
{
    std::ostringstream ss;

    if (a.vertices.size() != b.vertices.size()) {
        ss << a.vertices.size() << " vertices against " << b.vertices.size();
        return ss.str();
    }

    if (a.indices.size() != b.indices.size()) {
        ss << a.indices.size() << " faces against " << b.indices.size();
        return ss.str();
    }

    for (size_t i = 0; i < a.vertices.size(); ++i)
        for (int d = 0; d < 3; ++d)
            if (bits_of(a.vertices[i][d]) != bits_of(b.vertices[i][d])) {
                ss << "vertex " << i << " axis " << d << " is " << a.vertices[i][d]
                   << " against " << b.vertices[i][d];
                return ss.str();
            }

    for (size_t i = 0; i < a.indices.size(); ++i)
        for (int d = 0; d < 3; ++d)
            if (a.indices[i][d] != b.indices[i][d]) {
                ss << "face " << i << " corner " << d << " is " << a.indices[i][d]
                   << " against " << b.indices[i][d];
                return ss.str();
            }

    return {};
}

Tree build_tree(const Fixture &fx)
{
    Tree t;

    Slic3r::sla::create_branching_tree(t.builder, make_supportable_mesh(fx));

    t.hash = mesh_hash(t.builder.retrieve_mesh(Slic3r::sla::MeshType::Support));

    return t;
}

// The same model built over and over has to give the same tree every time.
void check_repeatable(const Fixture &fx, int runs)
{
    const Tree base = build_tree(fx);

    // A tree of nothing would compare equal to another tree of nothing.
    REQUIRE(base.builder.heads().size() > 0);

    std::ostringstream about;
    about << "model " << fx.name << ": " << fx.pts.size() << " points, tree of "
          << base.builder.heads().size() << " heads, " << base.builder.pillars().size()
          << " pillars, " << base.builder.diffbridges().size() << " branches, hash "
          << base.hash;
    INFO(about.str());

    for (int run = 2; run <= runs; ++run) {
        const Tree        again = build_tree(fx);
        const std::string why   = first_difference(base.builder.retrieve_mesh(),
                                                     again.builder.retrieve_mesh());

        std::ostringstream said;
        said << "build " << run << " of " << fx.name << ": hash " << again.hash
             << " against " << base.hash << " (" << why << ")";
        INFO(said.str());

        CHECK(why.empty());
        CHECK(again.hash == base.hash);
    }
}

} // namespace

TEST_CASE("The branching tree of a model is the same tree every time", "[suptreetree]")
{
    check_repeatable(plain_fixture(), 5);
}

TEST_CASE(
    "The branching tree of a model with close and headless points is the same tree every time",
    "[suptreetree]"
)
{
    // Two of the builds are enough here: what this model adds to the case above is
    // the shape of the bookkeeping, not the number of runs, and it takes a build
    // per point that has to be routed.
    check_repeatable(close_points_fixture(), 2);
}

// The thread count of a build cannot be asked for on a TBB that has no
// global_control, so the case is only there where there is one. The bound is the
// smallest of the global_control objects alive, so this one thread is what the
// build gets whichever else is in force.
#ifdef TBB_HAS_GLOBAL_CONTROL
TEST_CASE("The branching tree does not depend on how many threads it runs on", "[suptreetree]")
{
    const Fixture fx = plain_fixture();
    const Tree many  = build_tree(fx);

    REQUIRE(many.builder.heads().size() > 0);

    std::string why;
    uint64_t    one_hash = 0;
    {
        // One thread for the whole build: the searches of the leaves run one after
        // another, and so do the ray casts inside them. A tree that came out
        // differently here would mean that what the scheduling decides is part of it.
        tbb::global_control threads{tbb::global_control::max_allowed_parallelism, 1};

        const Tree one = build_tree(fx);

        one_hash = one.hash;
        why      = first_difference(many.builder.retrieve_mesh(), one.builder.retrieve_mesh());
    }

    INFO("one thread: hash " << one_hash << " against " << many.hash << " (" << why << ")");

    CHECK(why.empty());
    CHECK(one_hash == many.hash);
}
#endif

TEST_CASE(
    "A point and the head of its own id stay together when a neighbour is a duplicate and "
    "a point gets no head",
    "[suptreetree]"
)
{
    const Fixture fx                      = close_points_fixture();
    const Slic3r::sla::SupportableMesh sm = make_supportable_mesh(fx);

    SupportTreeBuilder builder;
    Slic3r::sla::create_branching_tree(builder, sm);

    // The tree builds no head of its own: it uses the one the placement of the point
    // gives it, so the placement is the oracle of which head belongs where.
    size_t with_head = 0;
    for (size_t i = 0; i < fx.pts.size(); ++i) {
        const std::optional<Head> placed =
            Slic3r::sla::calculate_pinhead_placement(execution::ex_seq, sm, i);

        const Head *head = builder.head_of(unsigned(i));

        std::ostringstream where;
        where << "point " << i << " at " << fx.pts[i].pos.x() << ", " << fx.pts[i].pos.y()
              << ", " << fx.pts[i].pos.z();
        INFO(where.str());

        if (!placed.has_value()) {
            // A point the surface under it does not face down for gets no head at
            // all, and asking the builder for one must not hand out the head of a
            // point that did get one.
            CHECK(head == nullptr);
            continue;
        }

        REQUIRE(head != nullptr);
        ++with_head;

        // The head under this id is the head of this point and not the head of the
        // point that happens to have the same number among the leaves, which is
        // what a duplicate dropped or a point with no head before it shifts the
        // leaves by.
        CHECK(head->id == long(i));
        CHECK((head->pos - placed->pos).norm() <= at_point);
    }

    // The point on the upper part of the sphere got no head, and the point dropped
    // as the duplicate of the one next to it did not either, so the model has both
    // kinds of point that have a head missing and the rest have one.
    REQUIRE(with_head > 0);
    CHECK(with_head < fx.pts.size());
    CHECK(builder.heads().size() == with_head);

    // Every head of the builder is the head of one of these points, whichever of
    // them the tree gave up on, and every head that was not given up is still the
    // head of its own point.
    size_t dropped = 0;
    for (const Head &h : builder.heads()) {
        std::ostringstream said;
        said << "a head at " << h.pos.x() << ", " << h.pos.y() << ", " << h.pos.z()
             << " with the id " << h.id;
        INFO(said.str());

        const auto point = std::find_if(fx.pts.begin(), fx.pts.end(),
                                        [&h](const SupportPoint &p) {
                                            return (h.pos - p.pos.cast<double>()).norm() <=
                                                   at_point;
                                        });
        CHECK(point != fx.pts.end());
        CHECK(unsigned(point - fx.pts.begin()) == unsigned(h.id));

        if (!h.is_valid())
            ++dropped;
    }

    CHECK(dropped <= with_head);
}
