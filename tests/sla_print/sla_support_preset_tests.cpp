// M2.18c/d: the support presets have to reach the generated support tree, not only
// the point they are stored on. The support tool puts a tip diameter, a pillar
// diameter, a base diameter and a base height on every point it places
// (M2.18a/b), so both tree builders have to read those back instead of using the
// global config only, and neither of them may widen a thin pillar back to it.
//
// The points here are placed by hand on a cube standing on the plate, so nothing
// but the point decides how thick the pillar and the base below it become: no
// support point generator, no printer preset.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>

#include "Slic3r/Biz/Algorithms/AABBMesh.hpp"
#include "Slic3r/Biz/Algorithms/Execution/ExecutionSeq.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "libslic3r/SLA/BranchingTreeSLA.hpp"
#include "libslic3r/SLA/DefaultSupportTree.hpp"
#include "libslic3r/SLA/SupportTree.hpp"
#include "libslic3r/SLA/SupportTreeBuilder.hpp"
#include "libslic3r/SLA/SupportTreeUtils.hpp"

using Catch::Approx;
namespace triangle_mesh = Slic3r::Biz::Algorithms::TriangleMesh;
using Slic3r::Domain::SLA::SupportPoint;
using Slic3r::Domain::SLA::SupportPoints;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec3f;

namespace {

// The cube stands on the plate, so its bottom face is at z = 0. It is wide enough
// for the two points to be further apart than the longest distance the pillar
// interconnection may bridge, so every point keeps the pillar of its own.
constexpr double cube_edge = 40.;

// The gap under the cube has to fit a pinhead and its pillar. It also has to stay
// below 20 times the radius of the thinnest preset pillar, because the mini pillar
// path widens a thin and long pillar back to the configured radius, which would
// hide the preset the test is about. The tests that are after that path build
// their own, taller mesh.
constexpr double elevation = 6.;

// A gap long enough to make the mini pillar path of the default tree widen a
// thin pillar, which is what the tests about it need. It stays below the height
// where the algorithm starts to add extra pillars next to a lonely one.
constexpr double tall_elevation = 12.;

// Light preset: 0.3 mm tip, 0.8 mm pillar, 2 mm base of 0.5 mm.
const Vec3d light_pos{8., 20., 0.};
SupportPoint light_point()
{
    SupportPoint sp;
    sp.pos               = Vec3f{8.f, 20.f, 0.f};
    sp.head_front_radius = 0.15f;
    sp.pillar_diameter   = 0.8f;
    sp.base_diameter     = 2.f;
    sp.base_height       = 0.5f;

    return sp;
}

// Heavy preset: 0.6 mm tip, 1.8 mm pillar, 4 mm base of 1 mm.
const Vec3d heavy_pos{32., 20., 0.};
SupportPoint heavy_point()
{
    SupportPoint sp;
    sp.pos               = Vec3f{32.f, 20.f, 0.f};
    sp.head_front_radius = 0.3f;
    sp.pillar_diameter   = 1.8f;
    sp.base_diameter     = 4.f;
    sp.base_height       = 1.f;

    return sp;
}

// Two points, far enough from each other to get a pillar of their own.
SupportPoints preset_points()
{
    return SupportPoints{light_point(), heavy_point()};
}

// The cube above, as a mesh that outlives every tree built from it. The AABBMesh of
// a SupportableMesh is a view on a triangle mesh and not a copy of it: it keeps the
// pointer, builds its AABB tree on it and reads its vertices and its indices for
// every query, so a cube built inside make_supportable_mesh() below is freed before
// the first of them and the tree then runs on released memory.
const indexed_triangle_set &cube_mesh()
{
    static const indexed_triangle_set cube =
        triangle_mesh::its_make_cube(cube_edge, cube_edge, cube_edge);

    return cube;
}

Slic3r::sla::SupportableMesh make_supportable_mesh(const SupportPoints &pts,
                                                  double              object_elevation = elevation)
{
    Slic3r::sla::SupportTreeConfig cfg;
    cfg.object_elevation_mm = object_elevation;

    Slic3r::sla::SupportableMesh sm{
        .emesh = Slic3r::AABBMesh(cube_mesh()),
        .pts = std::make_shared<const SupportPoints>(pts),
        .cfg = cfg};

    return sm;
}

const Slic3r::sla::Head *head_at(const Slic3r::sla::SupportTreeBuilder &builder,
                                 const Vec3d                 &pos)
{
    for (const Slic3r::sla::Head &head : builder.heads())
        if (head.is_valid() && (head.pos - pos).norm() < 1e-3)
            return &head;

    return nullptr;
}

const Slic3r::sla::Pedestal *pedestal_at(const Slic3r::sla::SupportTreeBuilder &builder,
                                         double                              x)
{
    for (const Slic3r::sla::Pedestal &ped : builder.pedestals())
        if (std::abs(ped.pos.x() - x) < 1e-3)
            return &ped;

    return nullptr;
}

const Slic3r::sla::Pillar *pillar_at(const Slic3r::sla::SupportTreeBuilder &builder,
                                     double                              x)
{
    for (const Slic3r::sla::Pillar &pill : builder.pillars())
        if (std::abs(pill.endpt.x() - x) < 1e-3)
            return &pill;

    return nullptr;
}

} // namespace

TEST_CASE("DefaultSupportTree::Point presets size their own pillar", "[suptreetree]")
{
    SupportPoints pts = preset_points();
    Slic3r::sla::SupportableMesh sm = make_supportable_mesh(pts);

    Slic3r::sla::SupportTreeBuilder builder;
    Slic3r::sla::create_default_tree(builder, sm);

    const Slic3r::sla::Head *light = head_at(builder, light_pos);
    const Slic3r::sla::Head *heavy = head_at(builder, heavy_pos);

    REQUIRE(light != nullptr);
    REQUIRE(heavy != nullptr);

    // The pillar under a head is as wide as the head itself.
    CHECK(light->r_back_mm == Approx(0.4));
    CHECK(heavy->r_back_mm == Approx(0.9));

    REQUIRE(light->pillar_id >= 0);
    REQUIRE(heavy->pillar_id >= 0);

    CHECK(builder.pillar(light->pillar_id).r_start == Approx(0.4));
    CHECK(builder.pillar(heavy->pillar_id).r_start == Approx(0.9));
    CHECK(builder.pillar(light->pillar_id).r_start !=
          Approx(builder.pillar(heavy->pillar_id).r_start));
}

TEST_CASE("DefaultSupportTree::Point presets size their own base", "[suptreetree]")
{
    SupportPoints pts = preset_points();
    Slic3r::sla::SupportableMesh sm = make_supportable_mesh(pts);

    Slic3r::sla::SupportTreeBuilder builder;
    Slic3r::sla::create_default_tree(builder, sm);

    REQUIRE(builder.pedestals().size() == 2);

    // A base is widened to the pillar it belongs to, so the light pillar keeps its
    // 2 mm base and the heavy one its 4 mm base.
    const Slic3r::sla::Pedestal *light = pedestal_at(builder, 8.);
    const Slic3r::sla::Pedestal *heavy = pedestal_at(builder, 32.);

    REQUIRE(light != nullptr);
    REQUIRE(heavy != nullptr);

    CHECK(light->height == Approx(0.5));
    CHECK(heavy->height == Approx(1.0));
    CHECK(light->r_bottom == Approx(1.0));
    CHECK(heavy->r_bottom == Approx(2.0));
    CHECK(light->r_top == Approx(0.4));
    CHECK(heavy->r_top == Approx(0.9));
}

TEST_CASE("DefaultSupportTree::Points without a preset keep the global size",
          "[suptreetree]")
{
    SupportPoints pts(2);
    pts[0].pos = Vec3f{8.f, 20.f, 0.f};
    pts[1].pos = Vec3f{32.f, 20.f, 0.f};
    for (SupportPoint &sp : pts)
        sp.head_front_radius = 0.2f;

    Slic3r::sla::SupportableMesh sm = make_supportable_mesh(pts);

    Slic3r::sla::SupportTreeBuilder builder;
    Slic3r::sla::create_default_tree(builder, sm);

    const double r = sm.cfg.head_back_radius_mm;
    const double base_r = sm.cfg.base_radius_mm;

    for (const Slic3r::sla::Head *head : {head_at(builder, light_pos),
                                          head_at(builder, heavy_pos)}) {
        REQUIRE(head != nullptr);
        CHECK(head->r_back_mm == Approx(r));
        REQUIRE(head->pillar_id >= 0);
        CHECK(builder.pillar(head->pillar_id).r_start == Approx(r));
    }

    REQUIRE(builder.pedestals().size() == 2);
    for (const Slic3r::sla::Pedestal &ped : builder.pedestals()) {
        CHECK(ped.r_bottom == Approx(base_r));
        CHECK(ped.height == Approx(sm.cfg.base_height_mm));
    }
}

TEST_CASE("Point pillar diameter is clamped by the global config", "[suptreetree]")
{
    Slic3r::sla::SupportableMesh sm = make_supportable_mesh(preset_points());
    const Slic3r::sla::SupportTreeConfig &cfg = sm.cfg;

    SupportPoint sp;

    // No override at all: the globally configured pillar radius.
    REQUIRE(Slic3r::sla::head_back_radius(sm, sp) == Approx(cfg.head_back_radius_mm));

    sp.pillar_diameter = 1.8f;
    REQUIRE(Slic3r::sla::head_back_radius(sm, sp) == Approx(0.9));

    // Thinner than the fallback radius the algorithm may shrink a head to.
    sp.pillar_diameter = 0.1f;
    REQUIRE(Slic3r::sla::head_back_radius(sm, sp) ==
            Approx(cfg.head_fallback_radius_mm));

    // And a fat one is cut back to a small multiple of the configured radius.
    sp.pillar_diameter = 100.f;
    REQUIRE(Slic3r::sla::head_back_radius(sm, sp) ==
            Approx(cfg.head_back_radius_limit_mm()));
}

TEST_CASE("BranchingSupportTree::Point presets size their own pillar", "[suptreetree]")
{
    using Slic3r::Biz::Algorithms::Execution::ex_seq;

    SupportPoints pts = preset_points();
    Slic3r::sla::SupportableMesh sm = make_supportable_mesh(pts);

    std::optional<Slic3r::sla::Head> light =
        Slic3r::sla::calculate_pinhead_placement(ex_seq, sm, 0);
    std::optional<Slic3r::sla::Head> heavy =
        Slic3r::sla::calculate_pinhead_placement(ex_seq, sm, 1);

    REQUIRE(light.has_value());
    REQUIRE(heavy.has_value());

    CHECK(light->r_back_mm == Approx(0.4));
    CHECK(heavy->r_back_mm == Approx(0.9));

    Slic3r::sla::SupportTreeBuilder builder;
    Slic3r::sla::create_branching_tree(builder, sm);

    const Slic3r::sla::Head *light_head = head_at(builder, light_pos);
    const Slic3r::sla::Head *heavy_head = head_at(builder, heavy_pos);

    REQUIRE(light_head != nullptr);
    REQUIRE(heavy_head != nullptr);

    CHECK(light_head->r_back_mm == Approx(0.4));
    CHECK(heavy_head->r_back_mm == Approx(0.9));
}

TEST_CASE("BranchingSupportTree::Point presets size their own base", "[suptreetree]")
{
    SupportPoints pts = preset_points();
    Slic3r::sla::SupportableMesh sm = make_supportable_mesh(pts);

    Slic3r::sla::SupportTreeBuilder builder;
    Slic3r::sla::create_branching_tree(builder, sm);

    // The branching tree builds the base of a point from that point, not from
    // the global config: the light pillar keeps its 2 mm base of 0.5 mm, the
    // heavy one its 4 mm base of 1 mm.
    const Slic3r::sla::Pedestal *light = pedestal_at(builder, 8.);
    const Slic3r::sla::Pedestal *heavy = pedestal_at(builder, 32.);

    REQUIRE(light != nullptr);
    REQUIRE(heavy != nullptr);

    CHECK(light->height == Approx(0.5));
    CHECK(heavy->height == Approx(1.0));
    CHECK(light->r_bottom == Approx(1.0));
    CHECK(heavy->r_bottom == Approx(2.0));
    CHECK(light->r_top == Approx(0.4));
    CHECK(heavy->r_top == Approx(0.9));
}

TEST_CASE("BranchingSupportTree::Points without a preset keep the global base",
          "[suptreetree]")
{
    SupportPoints pts(2);
    pts[0].pos = Vec3f{8.f, 20.f, 0.f};
    pts[1].pos = Vec3f{32.f, 20.f, 0.f};
    for (SupportPoint &sp : pts)
        sp.head_front_radius = 0.2f;

    Slic3r::sla::SupportableMesh sm = make_supportable_mesh(pts);

    Slic3r::sla::SupportTreeBuilder builder;
    Slic3r::sla::create_branching_tree(builder, sm);

    REQUIRE(!builder.pedestals().empty());

    for (const Slic3r::sla::Pedestal &ped : builder.pedestals()) {
        CHECK(ped.r_bottom == Approx(sm.cfg.base_radius_mm));
        CHECK(ped.height == Approx(sm.cfg.base_height_mm));
    }
}

TEST_CASE("BranchingSupportTree::Point presets place their head with their own penetration",
          "[suptreetree]")
{
    using Slic3r::Biz::Algorithms::Execution::ex_seq;

    // The heavy point is used because its pillar is wider than the configured
    // one, so the head keeps its full width and only the penetration changes.
    SupportPoints deep_pts{heavy_point()};
    deep_pts[0].contact_depth = 1.2f;

    SupportPoints plain_pts{heavy_point()};

    Slic3r::sla::SupportableMesh deep_sm = make_supportable_mesh(deep_pts);
    Slic3r::sla::SupportableMesh plain_sm = make_supportable_mesh(plain_pts);

    std::optional<Slic3r::sla::Head> deep =
        Slic3r::sla::calculate_pinhead_placement(ex_seq, deep_sm, 0);
    std::optional<Slic3r::sla::Head> plain =
        Slic3r::sla::calculate_pinhead_placement(ex_seq, plain_sm, 0);

    REQUIRE(deep.has_value());
    REQUIRE(plain.has_value());

    CHECK(deep->penetration_mm == Approx(1.2));
    CHECK(plain->penetration_mm == Approx(plain_sm.cfg.head_penetration_mm));

    // The deeper the head sinks, the shorter it is, so its junction sits closer
    // to the surface of the model.
    CHECK(deep->junction_point().z() > plain->junction_point().z());
}

TEST_CASE("DefaultSupportTree::Point presets place their head with their own penetration",
          "[suptreetree]")
{
    SupportPoints deep_pts{heavy_point()};
    deep_pts[0].contact_depth = 1.2f;

    SupportPoints plain_pts{heavy_point()};

    Slic3r::sla::SupportableMesh deep_sm = make_supportable_mesh(deep_pts);
    Slic3r::sla::SupportableMesh plain_sm = make_supportable_mesh(plain_pts);

    Slic3r::sla::SupportTreeBuilder deep_builder;
    Slic3r::sla::create_default_tree(deep_builder, deep_sm);

    Slic3r::sla::SupportTreeBuilder plain_builder;
    Slic3r::sla::create_default_tree(plain_builder, plain_sm);

    const Slic3r::sla::Head *deep = head_at(deep_builder, heavy_pos);
    const Slic3r::sla::Head *plain = head_at(plain_builder, heavy_pos);

    REQUIRE(deep != nullptr);
    REQUIRE(plain != nullptr);

    CHECK(deep->penetration_mm == Approx(1.2));
    CHECK(plain->penetration_mm == Approx(plain_sm.cfg.head_penetration_mm));

    CHECK(deep->junction_point().z() > plain->junction_point().z());
}

TEST_CASE("DefaultSupportTree::A long thin preset pillar keeps its own radius",
          "[suptreetree]")
{
    // A gap long enough for the mini pillar path, which used to widen a thin
    // pillar back to the globally configured radius. A Light point asks for
    // 0.4 mm and has to get it, so the path must not run at all.
    SupportPoints pts(1);
    pts[0] = light_point();
    Slic3r::sla::SupportableMesh sm = make_supportable_mesh(pts, tall_elevation);

    Slic3r::sla::SupportTreeBuilder builder;
    Slic3r::sla::create_default_tree(builder, sm);

    const Slic3r::sla::Head *light = head_at(builder, light_pos);
    REQUIRE(light != nullptr);
    REQUIRE(light->pillar_id >= 0);

    // The widening path would have left the head without a pillar of its own
    // and put a wider one under a bridge below it.
    const Slic3r::sla::Pillar &pillar = builder.pillar(light->pillar_id);

    CHECK(pillar.r_start == Approx(0.4));
    CHECK(pillar.r_end == Approx(0.4));

    // The base of the point is still there, with the size it asked for.
    const Slic3r::sla::Pedestal *ped = pedestal_at(builder, 8.);
    REQUIRE(ped != nullptr);
    CHECK(ped->r_top == Approx(0.4));
    CHECK(ped->r_bottom == Approx(1.0));
    CHECK(ped->height == Approx(0.5));
}

TEST_CASE("BranchingSupportTree::A thin preset pillar keeps its own radius", "[suptreetree]")
{
    // The ground connection of a point with its own pillar diameter has to end
    // at that diameter and not at the radius of the node it continues to, which
    // is the globally configured one here. A Light point asks for 0.4 mm.
    SupportPoints pts(1);
    pts[0] = light_point();
    Slic3r::sla::SupportableMesh sm = make_supportable_mesh(pts);

    Slic3r::sla::SupportTreeBuilder builder;
    Slic3r::sla::create_branching_tree(builder, sm);

    const Slic3r::sla::Pillar *pillar = pillar_at(builder, 8.);
    REQUIRE(pillar != nullptr);

    CHECK(pillar->r_start == Approx(0.4));
    CHECK(pillar->r_end == Approx(0.4));

    const Slic3r::sla::Pedestal *ped = pedestal_at(builder, 8.);
    REQUIRE(ped != nullptr);
    CHECK(ped->r_top == Approx(0.4));
    CHECK(ped->r_bottom == Approx(1.0));
    CHECK(ped->height == Approx(0.5));
}
