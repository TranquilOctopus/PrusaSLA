// M2.16b: the per point support geometry M2.13 and M2.16 store on a support
// point and save in the 3MF has to reach the mesh: the shape of the contact (a
// cone or a ball instead of the double sphere pinhead), an optional knot ball
// at the junction between the head and the pillar, a regular polygon stem
// instead of the round one, and a stem that tapers towards its base.
//
// The meshes are checked one part at a time, because that is where the geometry
// is built: the head and the pillar of a support point. Every shape has to be a
// closed mesh, a value other than the default one has to change the mesh in the
// expected direction, and the defaults have to give the meshes of the tree as
// it was built before.
//
// The points in the tree level tests are placed by hand on a cube standing on
// the plate, so nothing but the point decides what is built: no support point
// generator, no printer preset.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "Slic3r/Biz/Algorithms/AABBMesh.hpp"
#include "Slic3r/Biz/Algorithms/Execution/ExecutionSeq.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"
#include "libslic3r/SLA/BranchingTreeSLA.hpp"
#include "libslic3r/SLA/DefaultSupportTree.hpp"
#include "libslic3r/SLA/SupportTree.hpp"
#include "libslic3r/SLA/SupportTreeBuilder.hpp"
#include "libslic3r/SLA/SupportTreeMesher.hpp"
#include "libslic3r/SLA/SupportTreeUtils.hpp"

using Catch::Approx;
namespace triangle_mesh = Slic3r::Biz::Algorithms::TriangleMesh;
namespace sla = Slic3r::sla;
using Slic3r::Domain::SLA::SupportPoint;
using Slic3r::Domain::SLA::SupportPoints;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec3f;

namespace {

// The detail level the support tree mesh builder uses by default.
constexpr size_t steps = 45;

// The head of a point on the bottom of the cube below, with the sizes
// SupportTreeConfig defaults to.
constexpr double r_back = 0.5;
constexpr double r_pin = 0.2;
constexpr double width = 1.0;
constexpr double penetration = 0.5;

// Where that point is and where the parts built for it end up.
constexpr double px = 8.;
constexpr double py = 20.;
constexpr double pillar_height = 5.;

sla::Head make_head(sla::HeadTipShape shape = sla::HeadTipShape::Default, double knot = 0.)
{
    sla::Head head{r_back, r_pin, width, penetration, Vec3d::Zero(), Vec3d{px, py, 0.}};
    head.dir            = sla::DOWN;
    head.tip_shape      = shape;
    head.knot_radius_mm = knot;

    return head;
}

sla::Pillar make_pillar(sla::StemGeometry stem = {}, double start_radius = 0.5)
{
    sla::Pillar p{Vec3d{px, py, 0.}, pillar_height, start_radius,
                  stem.end_radius(start_radius)};
    p.stem = stem;

    return p;
}

double volume(const indexed_triangle_set &mesh)
{
    return std::abs(double(Slic3r::Domain::its_volume(mesh)));
}

// A closed, consistently wound surface: every directed edge of every triangle
// is there exactly once, so nothing is open and no two triangles share a face.
bool closed(const indexed_triangle_set &mesh)
{
    std::map<std::pair<int, int>, int> edges;

    for (const auto &idx : mesh.indices)
        for (int i = 0; i < 3; ++i) {
            ++edges[{idx[i], idx[(i + 1) % 3]}];
            ++edges[{idx[(i + 1) % 3], idx[i]}];
        }

    for (const auto &edge : edges)
        if (edge.second != 1)
            return false;

    return !edges.empty();
}

bool same_mesh(const indexed_triangle_set &a, const indexed_triangle_set &b)
{
    if (a.vertices.size() != b.vertices.size() || a.indices.size() != b.indices.size())
        return false;

    for (size_t i = 0; i < a.vertices.size(); ++i)
        if ((a.vertices[i] - b.vertices[i]).norm() > 0.f)
            return false;

    return a.indices == b.indices;
}

double min_z(const indexed_triangle_set &mesh)
{
    double ret = std::numeric_limits<double>::max();
    for (const auto &v : mesh.vertices)
        ret = std::min(ret, double(v.z()));

    return ret;
}

double max_z(const indexed_triangle_set &mesh)
{
    double ret = std::numeric_limits<double>::lowest();
    for (const auto &v : mesh.vertices)
        ret = std::max(ret, double(v.z()));

    return ret;
}

// How many vertices are within eps of the given height.
size_t count_near(const indexed_triangle_set &mesh, double z, double eps)
{
    size_t ret = 0;
    for (const auto &v : mesh.vertices)
        if (std::abs(double(v.z()) - z) <= eps)
            ++ret;

    return ret;
}

// The largest distance of a vertex from the axis through (cx, cy), either
// everywhere or only at the given height.
double max_radius(const indexed_triangle_set &mesh, double cx, double cy,
                  double z = std::numeric_limits<double>::max())
{
    double ret = 0.;
    for (const auto &v : mesh.vertices)
        if (z == std::numeric_limits<double>::max() ||
            std::abs(double(v.z()) - z) < 1e-4)
            ret = std::max(ret, std::hypot(double(v.x()) - cx, double(v.y()) - cy));

    return ret;
}

// The cube stands on the plate, so its bottom face is at z = 0. It is wide enough
// for the points to be further apart than the longest distance the pillar
// interconnection may bridge, so every point keeps the pillar of its own.
constexpr double cube_edge = 40.;
constexpr double elevation = 6.;

const Vec3d point_pos{8., 20., 0.};

SupportPoint geometry_point()
{
    SupportPoint sp;
    sp.pos               = Vec3f{8.f, 20.f, 0.f};
    sp.head_front_radius = 0.2f;

    return sp;
}

Slic3r::sla::SupportableMesh make_supportable_mesh(const SupportPoints &pts)
{
    Slic3r::sla::SupportTreeConfig cfg;
    cfg.object_elevation_mm = elevation;

    Slic3r::sla::SupportableMesh sm{
        .emesh = Slic3r::AABBMesh(
            triangle_mesh::its_make_cube(cube_edge, cube_edge, cube_edge)),
        .pts = std::make_shared<const SupportPoints>(pts),
        .cfg = cfg};

    return sm;
}

const sla::Head *head_at(const sla::SupportTreeBuilder &builder, const Vec3d &pos)
{
    for (const sla::Head &head : builder.heads())
        if (head.is_valid() && (head.pos - pos).norm() < 1e-3)
            return &head;

    return nullptr;
}

// The pillar a base at the given x belongs to, which is the pillar that reaches
// the ground there.
const sla::Pillar *ground_pillar_at(const sla::SupportTreeBuilder &builder, double x)
{
    const sla::Pedestal *base = nullptr;
    for (const sla::Pedestal &ped : builder.pedestals())
        if (std::abs(ped.pos.x() - x) < 1e-3)
            base = &ped;

    if (base == nullptr)
        return nullptr;

    for (const sla::Pillar &pill : builder.pillars())
        if ((pill.endpt - base->pos).norm() < 1e-3)
            return &pill;

    return nullptr;
}

const sla::Pedestal *pedestal_at(const sla::SupportTreeBuilder &builder, double x)
{
    for (const sla::Pedestal &ped : builder.pedestals())
        if (std::abs(ped.pos.x() - x) < 1e-3)
            return &ped;

    return nullptr;
}

} // namespace

TEST_CASE("The default tip shape builds the pinhead of before", "[suptreetree]")
{
    const indexed_triangle_set legacy = sla::pinhead(r_pin, r_back, width, steps);
    const indexed_triangle_set dflt =
        sla::pinhead(sla::HeadTipShape::Default, r_pin, r_back, width, steps);

    // The refactoring into the shapes of M2.16b left the default alone.
    CHECK(same_mesh(legacy, dflt));

    const indexed_triangle_set mesh = sla::get_mesh(make_head(), steps);

    CHECK(closed(mesh));
    CHECK(volume(mesh) == Approx(volume(dflt)));
}

TEST_CASE("A cone tip is a pointed contact, a ball tip a sphere", "[suptreetree]")
{
    const indexed_triangle_set dflt = sla::get_mesh(make_head(), steps);
    const indexed_triangle_set cone =
        sla::get_mesh(make_head(sla::HeadTipShape::Cone), steps);
    const indexed_triangle_set ball =
        sla::get_mesh(make_head(sla::HeadTipShape::Ball), steps);

    CHECK(closed(dflt));
    CHECK(closed(cone));
    CHECK(closed(ball));

    // Both take the round bulge the front sphere of the default pinhead leaves
    // where the robe ends off the head: the robe tapers to the full front radius
    // and the contact closes it there, so both carry less resin than before.
    CHECK(volume(cone) < volume(dflt));
    CHECK(volume(ball) < volume(dflt));

    // Both are as long as the default pinhead and reach into the model as deep,
    // so only the shape of the contact is different. All of them are bounded by
    // the detail level, none of them is the sphere of many rings.
    CHECK(cone.indices.size() < dflt.indices.size());
    CHECK(ball.indices.size() < dflt.indices.size());
    CHECK(min_z(cone) == Approx(min_z(dflt)));
    CHECK(min_z(ball) == Approx(min_z(dflt)));
    CHECK(max_z(cone) == Approx(max_z(dflt)));
    CHECK(max_z(ball) == Approx(max_z(dflt)));

    // A cone has nothing but its tip that high and a ball nothing but the pole of
    // its sphere, while the sphere of the default pinhead already has its last
    // rings up there. All three reach as deep as the default tip, which is the
    // pole of the front sphere, and all three start at the same back of the head.
    CHECK(count_near(cone, max_z(cone), 0.5 * r_pin) == 1);
    CHECK(count_near(ball, max_z(ball), 0.5 * r_pin) == 1);
    CHECK(count_near(dflt, max_z(dflt), 0.5 * r_pin) > 1);
}

TEST_CASE("A knot is a ball at the junction of head and pillar", "[suptreetree]")
{
    constexpr double knot = 0.8;

    const indexed_triangle_set plain = sla::get_mesh(make_head(), steps);
    const indexed_triangle_set knotted = sla::get_mesh(make_head(sla::HeadTipShape::Default, knot), steps);

    CHECK(closed(plain));
    CHECK(closed(knotted));

    const sla::Head head = make_head(sla::HeadTipShape::Default, knot);

    // The knot is thicker than the back of the pinhead, so it grows the head.
    CHECK(head.knot_radius_mm == Approx(knot));
    CHECK(head.junction_radius() == Approx(knot));
    const sla::Head plain_head = make_head();
    CHECK(plain_head.junction_radius() == Approx(r_back));

    CHECK(volume(knotted) > volume(plain));
    CHECK(max_z(knotted) == Approx(max_z(plain)));

    // A ball of the knot radius around the junction, which is where the pillar
    // starts.
    CHECK(min_z(knotted) == Approx(head.junction_point().z() - knot));
}

TEST_CASE("Stem sides build a polygon pillar of the same radius", "[suptreetree]")
{
    const sla::Pillar round = make_pillar();
    sla::Pillar hex = round;
    hex.stem.sides = 6;

    const indexed_triangle_set round_mesh = sla::get_mesh(round, steps);
    const indexed_triangle_set hex_mesh = sla::get_mesh(hex, steps);

    CHECK(closed(round_mesh));
    CHECK(closed(hex_mesh));

    // The radius of the polygon is its circumscribed radius, so the hexagon
    // carries about as much as the round pillar of the same radius and is never
    // the same mesh.
    const double round_area = 0.5 * double(steps) * std::sin(2 * PI / double(steps));
    const double hex_area = 0.5 * 6. * std::sin(2 * PI / 6.);
    CHECK(volume(hex_mesh) == Approx(volume(round_mesh) * hex_area / round_area)
                               .epsilon(1e-3));
    CHECK(max_radius(hex_mesh, px, py) == Approx(max_radius(round_mesh, px, py)));

    // A polygon pillar has as many corners as the support point asked for and no
    // more triangles than that.
    CHECK(hex_mesh.vertices.size() == 2 * 6 + 2);
    CHECK(hex_mesh.indices.size() <= 4 * 6);
    CHECK(round_mesh.indices.size() > hex_mesh.indices.size());
}

TEST_CASE("A stem taper thins the pillar towards its base", "[suptreetree]")
{
    sla::StemGeometry stem;
    stem.taper_mm      = 0.2;
    stem.min_radius_mm = 0.25;

    const sla::Pillar tapered = make_pillar(stem);

    // The pillar is as wide as it was asked for where it starts and taper_mm
    // thinner where it ends.
    CHECK(tapered.r_start == Approx(0.5));
    CHECK(tapered.r_end == Approx(0.3));

    const indexed_triangle_set plain = sla::get_mesh(make_pillar(), steps);
    const indexed_triangle_set mesh = sla::get_mesh(tapered, steps);

    CHECK(closed(plain));
    CHECK(closed(mesh));

    CHECK(max_radius(mesh, px, py) == Approx(0.5));
    CHECK(max_radius(mesh, px, py, pillar_height) == Approx(0.5));
    CHECK(max_radius(mesh, px, py, 0.) == Approx(0.3));
    CHECK(volume(mesh) < volume(plain));
}

TEST_CASE("The stem taper is in mm and stops at the minimum pillar radius",
          "[suptreetree]")
{
    sla::StemGeometry stem;
    stem.min_radius_mm = 0.25;

    // Without a taper the radius is exactly what it was, even below the minimum.
    CHECK(stem.end_radius(0.5) == Approx(0.5));
    CHECK(stem.end_radius(0.1) == Approx(0.1));

    stem.taper_mm = 0.2;
    CHECK(stem.end_radius(0.5) == Approx(0.3));

    // And it never thins a pillar below the minimum pillar radius.
    CHECK(stem.end_radius(0.4) == Approx(0.25));
    stem.taper_mm = 100.;
    CHECK(stem.end_radius(0.5) == Approx(0.25));
    stem.taper_mm = -1.;
    CHECK(stem.end_radius(0.5) == Approx(0.5));
}

TEST_CASE("A support point without a stem of its own gets the round pillar",
          "[suptreetree]")
{
    Slic3r::sla::SupportableMesh sm = make_supportable_mesh(SupportPoints{geometry_point()});
    SupportPoint sp = geometry_point();

    // A point that asks for nothing gets the round, untapered pillar, even
    // though its own radius would be below the minimum one.
    sla::StemGeometry none = sla::stem_geometry(sm, &sp);
    CHECK(none.round());
    CHECK(none.sides == 0);
    CHECK(none.taper_mm == Approx(0.));
    CHECK(none.end_radius(0.1) == Approx(0.1));

    // Without a point there is nothing to read either.
    CHECK(sla::stem_geometry(sm, nullptr).round());

    // Only 3 to 12 sides are built, anything else falls back to the round pillar.
    for (uint8_t sides : {uint8_t(2), uint8_t(13), uint8_t(200)}) {
        sp.stem_sides = sides;
        CHECK(sla::stem_geometry(sm, &sp).round());
    }

    for (uint8_t sides : {uint8_t(3), uint8_t(4), uint8_t(6), uint8_t(12)}) {
        sp.stem_sides = sides;
        CHECK(sla::stem_geometry(sm, &sp).sides == sides);
    }

    // The minimum radius of a taper is the radius the algorithm may shrink a
    // head to, so no pillar gets thinner than the thinnest one anyway.
    sp.stem_taper = 0.2f;
    sla::StemGeometry tapered = sla::stem_geometry(sm, &sp);
    CHECK(tapered.taper_mm == Approx(0.2));
    CHECK(tapered.min_radius_mm == Approx(sm.cfg.head_fallback_radius_mm));

    // A missing or negative taper means no taper.
    for (float taper : {0.f, -0.5f}) {
        sp.stem_taper = taper;
        CHECK(sla::stem_geometry(sm, &sp).taper_mm == Approx(0.));
    }
}

TEST_CASE("DefaultSupportTree::Point geometry reaches the tree", "[suptreetree]")
{
    SupportPoints pts{geometry_point()};
    pts[0].tip_shape    = SupportPoint::TipShape::Cone;
    pts[0].knot_radius = 0.8f;
    pts[0].stem_sides  = 6;
    pts[0].stem_taper  = 0.2f;

    Slic3r::sla::SupportableMesh sm = make_supportable_mesh(pts);

    sla::SupportTreeBuilder builder;
    sla::create_default_tree(builder, sm);

    const sla::Head *head = head_at(builder, point_pos);
    REQUIRE(head != nullptr);

    CHECK(head->tip_shape == SupportPoint::TipShape::Cone);
    CHECK(head->knot_radius_mm == Approx(0.8));
    CHECK(head->stem.sides == 6);
    CHECK(head->stem.taper_mm == Approx(0.2));

    REQUIRE(head->pillar_id >= 0);
    const sla::Pillar &pillar = builder.pillar(head->pillar_id);

    // The pillar of that point carries its cross section and taper, and the base
    // it stands on ends where the pillar is.
    CHECK(pillar.stem.sides == 6);
    CHECK(pillar.r_start == Approx(head->r_back_mm));
    CHECK(pillar.r_end == Approx(head->r_back_mm - 0.2));

    const sla::Pedestal *base = pedestal_at(builder, px);
    REQUIRE(base != nullptr);
    CHECK(base->r_top == Approx(pillar.r_end));
    CHECK(base->r_bottom >= base->r_top);

    CHECK(closed(sla::get_mesh(*head, steps)));
    CHECK(closed(sla::get_mesh(pillar, steps)));
}

TEST_CASE("DefaultSupportTree::A point without the fields keeps the old mesh",
          "[suptreetree]")
{
    Slic3r::sla::SupportableMesh sm = make_supportable_mesh(SupportPoints{geometry_point()});

    sla::SupportTreeBuilder builder;
    sla::create_default_tree(builder, sm);

    const sla::Head *head = head_at(builder, point_pos);
    REQUIRE(head != nullptr);

    CHECK(head->tip_shape == SupportPoint::TipShape::Default);
    CHECK(head->knot_radius_mm == Approx(0.));
    CHECK(head->stem.round());
    CHECK(head->stem.taper_mm == Approx(0.));
    CHECK(head->junction_radius() == Approx(head->r_back_mm));

    REQUIRE(head->pillar_id >= 0);
    const sla::Pillar &pillar = builder.pillar(head->pillar_id);

    CHECK(pillar.stem.round());
    CHECK(pillar.r_end == Approx(pillar.r_start));

    // The pillar of a point without a stem of its own is the cone of before.
    CHECK(same_mesh(sla::get_mesh(pillar, steps),
                    sla::halfcone(pillar.height, pillar.r_start, pillar.r_start,
                                  pillar.endpt, steps)));

    const sla::Pedestal *base = pedestal_at(builder, px);
    REQUIRE(base != nullptr);
    CHECK(base->r_top == Approx(pillar.r_start));
}

// M2.24: the length of the tapered tip. A point carries its own (M2.13), the
// configured support_tip_length is the default of the points that carry none,
// and zero there keeps the pinhead width, which is what the tree built before.
TEST_CASE("DefaultSupportTree::The tip length is the one of the point, else the configured one",
          "[suptreetree]")
{
    auto tip_length_of = [](const Slic3r::sla::SupportableMesh &sm) {
        sla::SupportTreeBuilder builder;
        sla::create_default_tree(builder, sm);

        const sla::Head *head = head_at(builder, point_pos);
        REQUIRE(head != nullptr);

        return head->width_mm;
    };

    Slic3r::sla::SupportableMesh sm = make_supportable_mesh(SupportPoints{geometry_point()});

    // The pinhead width, the length the tree has always built with.
    CHECK(tip_length_of(sm) == Approx(sm.cfg.head_width_mm));

    // A configured tip length is the one every point without a tip length of its
    // own is built with.
    sm.cfg.tip_length_mm = 0.4;
    CHECK(tip_length_of(sm) == Approx(0.4));

    // A point of its own keeps it, whatever the default says.
    SupportPoints pts{geometry_point()};
    pts[0].tip_length = 0.6f;
    Slic3r::sla::SupportableMesh own = make_supportable_mesh(pts);
    own.cfg.tip_length_mm = 0.4;
    CHECK(tip_length_of(own) == Approx(0.6));
}

TEST_CASE("BranchingSupportTree::The tip length is the one of the point, else the configured one",
          "[suptreetree]")
{
    // The branching tree builds its heads with the same pinhead placement as the
    // default tree, so it has to read the tip length the same way (M2.27).
    auto tip_length_of = [](const Slic3r::sla::SupportableMesh &sm) {
        sla::SupportTreeBuilder builder;
        sla::create_branching_tree(builder, sm);

        const sla::Head *head = head_at(builder, point_pos);
        REQUIRE(head != nullptr);

        return head->width_mm;
    };

    Slic3r::sla::SupportableMesh sm = make_supportable_mesh(SupportPoints{geometry_point()});

    // The pinhead width, the length the tree has always built with.
    CHECK(tip_length_of(sm) == Approx(sm.cfg.head_width_mm));

    // A configured tip length is the one every point without a tip length of its
    // own is built with.
    sm.cfg.tip_length_mm = 0.4;
    CHECK(tip_length_of(sm) == Approx(0.4));

    // A point of its own keeps it, whatever the default says.
    SupportPoints pts{geometry_point()};
    pts[0].tip_length = 0.6f;
    Slic3r::sla::SupportableMesh own = make_supportable_mesh(pts);
    own.cfg.tip_length_mm = 0.4;
    CHECK(tip_length_of(own) == Approx(0.6));
}

// M2.12: a support point may ask for a base diameter and height of its own. The
// support presets write them, but every other point carries them too, so both
// trees have to read them off the point and not only from the global config.
TEST_CASE("A point's own base diameter reaches the foot of both trees",
          "[suptreetree]")
{
    SupportPoints pts{geometry_point()};
    pts[0].base_diameter = 3.f;
    pts[0].base_height   = 0.8f;

    Slic3r::sla::SupportableMesh sm = make_supportable_mesh(pts);

    // A point with no base of its own keeps the configured one.
    CHECK(sla::base_size(sm, &pts[0]).radius == Approx(1.5));
    CHECK(sla::base_size(sm, &geometry_point()).radius == Approx(sm.cfg.base_radius_mm));

    sla::SupportTreeBuilder default_builder;
    sla::create_default_tree(default_builder, sm);

    sla::SupportTreeBuilder branching_builder;
    sla::create_branching_tree(branching_builder, sm);

    // Both trees stand the pillar on a foot of 3 mm and 0.8 mm, not the 4 mm of
    // the global config, and the foot is wider than the pillar it belongs to.
    for (const sla::Pedestal *base : {pedestal_at(default_builder, px),
                                      pedestal_at(branching_builder, px)}) {
        REQUIRE(base != nullptr);
        CHECK(base->r_bottom == Approx(1.5));
        CHECK(base->height == Approx(0.8));
        CHECK(base->r_bottom > base->r_top);
    }
}

// M2.27: in zero elevation mode the branching tree keeps the room the base needs
// clear of the model, and the base of a point with a diameter of its own needs
// more room than the configured one, the way the default tree has always kept it
// in create_ground_pillar().
TEST_CASE("The room a base needs is the one of the point's own diameter",
          "[suptreetree]")
{
    using Slic3r::Biz::Algorithms::Execution::ex_seq;

    // A box standing on the plate next to the pillar: far enough that the pillar
    // passes by, near enough that the foot of the configured base diameter would
    // reach into it. The elevation is zero, the only mode where the room of the
    // base is kept at all.
    indexed_triangle_set box = triangle_mesh::its_make_cube(4., 4., 4.);
    for (auto &v : box.vertices)
        v += Vec3f{1.5f, -2.f, -4.f};

    sla::SupportTreeConfig cfg;
    cfg.object_elevation_mm = 0.;

    sla::SupportableMesh sm{.emesh = Slic3r::AABBMesh(box), .cfg = cfg};

    const double gap = std::sqrt(sm.emesh.squared_distance(Vec3d::Zero()));
    const sla::Junction j{Vec3d{0., 0., 5.}, cfg.head_back_radius_mm};

    // How high the pillar of a route straight down from the junction has to stop
    // to leave the base the room it asks for.
    auto base_stops_at = [&sm, &j](double base_radius) {
        return sla::check_ground_route(ex_seq, sm, j, sla::DOWN, 0.,
                                       sla::DefaultWideningModel{sm},
                                       sla::GroundRouteCheck::PillarOnly,
                                       base_radius).z();
    };

    // The box is 1.5 mm from the axis of the pillar, and the configured 4 mm base
    // does not fit in what is left of it.
    CHECK(gap == Approx(1.5));
    CHECK(sm.cfg.pillar_base_safety_distance_mm + sm.cfg.base_radius_mm > gap);

    // Without a base diameter of its own the route is the one of the tree before
    // the per point base diameter existed: the configured radius decides.
    CHECK(base_stops_at(0.) ==
          Approx(cfg.pillar_base_safety_distance_mm + cfg.base_radius_mm - gap));

    // A point with a wider base of its own has to stop that much higher, so the
    // base it asks for is the one that clears the model.
    CHECK(base_stops_at(4.) ==
          Approx(cfg.pillar_base_safety_distance_mm + 4. - gap));
}

TEST_CASE("BranchingSupportTree::Point geometry reaches the ground pillar",
          "[suptreetree]")
{
    SupportPoints pts{geometry_point()};
    pts[0].tip_shape    = SupportPoint::TipShape::Ball;
    pts[0].knot_radius = 0.8f;
    pts[0].stem_sides  = 6;
    pts[0].stem_taper  = 0.2f;

    Slic3r::sla::SupportableMesh sm = make_supportable_mesh(pts);

    sla::SupportTreeBuilder builder;
    sla::create_branching_tree(builder, sm);

    // The head is built by the same placement code as the default tree's.
    const sla::Head *head = head_at(builder, point_pos);
    REQUIRE(head != nullptr);
    CHECK(head->tip_shape == SupportPoint::TipShape::Ball);
    CHECK(head->knot_radius_mm == Approx(0.8));

    // The pillar of that leaf reaches the ground with the stem of its point.
    const sla::Pillar *pillar = ground_pillar_at(builder, px);
    REQUIRE(pillar != nullptr);
    CHECK(pillar->stem.sides == 6);
    CHECK(pillar->stem.taper_mm == Approx(0.2));
    CHECK(pillar->r_end < pillar->r_start);
    CHECK(pillar->r_end >= sm.cfg.head_fallback_radius_mm);

    CHECK(closed(sla::get_mesh(*head, steps)));
    CHECK(closed(sla::get_mesh(*pillar, steps)));

    // The branches the leaf shares with other points are still round.
    for (const sla::Pillar &pill : builder.pillars())
        if (std::abs(pill.endpt.x() - px) > 1e-3)
            CHECK(pill.stem.round());
}

TEST_CASE("The per point geometry changes the tree mesh", "[suptreetree]")
{
    SupportPoints plain_pts{geometry_point()};

    SupportPoints cone_pts{geometry_point()};
    cone_pts[0].tip_shape = SupportPoint::TipShape::Cone;

    SupportPoints knot_pts{geometry_point()};
    knot_pts[0].knot_radius = 0.8f;

    Slic3r::sla::SupportableMesh plain_sm = make_supportable_mesh(plain_pts);
    Slic3r::sla::SupportableMesh cone_sm = make_supportable_mesh(cone_pts);
    Slic3r::sla::SupportableMesh knot_sm = make_supportable_mesh(knot_pts);

    sla::SupportTreeBuilder plain_builder;
    sla::create_default_tree(plain_builder, plain_sm);
    sla::SupportTreeBuilder cone_builder;
    sla::create_default_tree(cone_builder, cone_sm);
    sla::SupportTreeBuilder knot_builder;
    sla::create_default_tree(knot_builder, knot_sm);

    const double plain_vol = volume(plain_builder.merged_mesh(steps));
    const double cone_vol = volume(cone_builder.merged_mesh(steps));
    const double knot_vol = volume(knot_builder.merged_mesh(steps));

    REQUIRE(plain_vol > 0.);
    CHECK(cone_vol < plain_vol);
    CHECK(knot_vol > plain_vol);

    CHECK(cone_builder.merged_mesh(steps).indices.size() <
          plain_builder.merged_mesh(steps).indices.size());
    CHECK(knot_builder.merged_mesh(steps).indices.size() >
          plain_builder.merged_mesh(steps).indices.size());
}