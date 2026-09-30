#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/Algorithms/AABBMesh.hpp"
#include "Slic3r/Biz/Sla/DrainHoleSuggestion.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"
#include "Slic3r/Domain/Types.hpp"

#include <cmath>
#include <numbers>
#include <vector>

using Slic3r::Biz::Sla::DrainHoleCandidate;
using Slic3r::Biz::Sla::DrainHoleSuggestion;
using Slic3r::Biz::Sla::DrainHoleSuggestionOptions;
using Slic3r::Biz::Sla::accepts_drain_hole_suggestion;
using Slic3r::Biz::Sla::suggest_drain_hole;
using Slic3r::Biz::Sla::suggest_nearest_drain_hole;
using Slic3r::Biz::Slicing::Sla::SlaIssue;
using Slic3r::Domain::Index3;
using Slic3r::Domain::Transform3d;
using Slic3r::Domain::TriangleMesh;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec3f;
using Slic3r::AABBMesh;

namespace {

/// Append a triangle, keeping the winding of the mesh a right handed one.
void add_triangle(TriangleMesh& mesh, const Vec3f& a, const Vec3f& b, const Vec3f& c)
{
    const int base = static_cast<int>(mesh.its.vertices.size());
    mesh.its.vertices.emplace_back(a);
    mesh.its.vertices.emplace_back(b);
    mesh.its.vertices.emplace_back(c);
    mesh.its.indices.emplace_back(Index3{base, base + 1, base + 2});
}

/// A closed box, wound so that its facet normals point out of it.
void add_box(TriangleMesh& mesh, const Vec3f& low, const Vec3f& high)
{
    const Vec3f v0{low.x(), low.y(), low.z()};
    const Vec3f v1{high.x(), low.y(), low.z()};
    const Vec3f v2{high.x(), high.y(), low.z()};
    const Vec3f v3{low.x(), high.y(), low.z()};
    const Vec3f v4{low.x(), low.y(), high.z()};
    const Vec3f v5{high.x(), low.y(), high.z()};
    const Vec3f v6{high.x(), high.y(), high.z()};
    const Vec3f v7{low.x(), high.y(), high.z()};

    add_triangle(mesh, v0, v3, v2); // bottom, -Z
    add_triangle(mesh, v0, v2, v1);
    add_triangle(mesh, v4, v5, v6); // top, +Z
    add_triangle(mesh, v4, v6, v7);
    add_triangle(mesh, v0, v1, v5); // front, -Y
    add_triangle(mesh, v0, v5, v4);
    add_triangle(mesh, v3, v7, v6); // back, +Y
    add_triangle(mesh, v3, v6, v2);
    add_triangle(mesh, v0, v4, v7); // left, -X
    add_triangle(mesh, v0, v7, v3);
    add_triangle(mesh, v1, v2, v6); // right, +X
    add_triangle(mesh, v1, v6, v5);
}

/// A box with a closed cavity inside it, which is what a hollow print without a drain hole
/// slices to: the inner box is wound the other way round, so its normals point into the cavity.
TriangleMesh make_hollow_cube(double size, double wall)
{
    TriangleMesh mesh;
    add_box(mesh, Vec3f::Zero(), Vec3f(size, size, size));
    add_box(mesh, Vec3f(wall, wall, wall), Vec3f(size - wall, size - wall, size - wall));
    // Only the second box is wound the other way round, and it is the one appended last.
    for (size_t i = 12; i < mesh.its.indices.size(); ++i) {
        std::swap(mesh.its.indices[i][1], mesh.its.indices[i][2]);
    }
    return mesh;
}

/// A cup standing on the plate with its opening down: a tube of @p wall_thickness closed by a roof
/// of @p roof_thickness, the shape a suction cup detector finds in the layers of a print.
TriangleMesh make_upside_down_cup(double radius, double wall_thickness, double height, double roof_thickness,
                                 size_t steps = 32)
{
    const double inner_radius = radius - wall_thickness;
    const double roof_z       = height - roof_thickness;

    TriangleMesh mesh;
    for (size_t i = 0; i < steps; ++i) {
        const double a          = 2. * std::numbers::pi * double(i) / double(steps);
        const double b          = 2. * std::numbers::pi * double(i + 1) / double(steps);
        const Vec3f  outer_a{float(radius * std::cos(a)), float(radius * std::sin(a)), 0.f};
        const Vec3f  outer_b{float(radius * std::cos(b)), float(radius * std::sin(b)), 0.f};
        const Vec3f  outer_a_top = outer_a + Vec3f(0.f, 0.f, float(height));
        const Vec3f  outer_b_top = outer_b + Vec3f(0.f, 0.f, float(height));
        const Vec3f  inner_a{float(inner_radius * std::cos(a)), float(inner_radius * std::sin(a)), 0.f};
        const Vec3f  inner_b{float(inner_radius * std::cos(b)), float(inner_radius * std::sin(b)), 0.f};
        const Vec3f  inner_a_roof = inner_a + Vec3f(0.f, 0.f, float(roof_z));
        const Vec3f  inner_b_roof = inner_b + Vec3f(0.f, 0.f, float(roof_z));

        // Outer wall, normals out of the cup.
        add_triangle(mesh, outer_a, outer_b, outer_b_top);
        add_triangle(mesh, outer_a, outer_b_top, outer_a_top);
        // Inner wall, normals into the cup.
        add_triangle(mesh, inner_a, inner_b_roof, inner_b);
        add_triangle(mesh, inner_a, inner_a_roof, inner_b_roof);
        // The underside of the roof, which is where the air of the cup has to get out.
        add_triangle(mesh, Vec3f(0.f, 0.f, float(roof_z)), inner_b_roof, inner_a_roof);
        // The top of the cup.
        add_triangle(mesh, Vec3f(0.f, 0.f, float(height)), outer_a_top, outer_b_top);
        // The rim of the opening, on the plate.
        add_triangle(mesh, inner_a, inner_b, outer_b);
        add_triangle(mesh, inner_a, outer_b, outer_a);
    }
    return mesh;
}

/// A point off the axis of the cup, on purpose: a ray that goes exactly through the centre of a
/// fan runs along the edge the fan triangles share, and a hit on an edge is a degenerate case for
/// any raycaster. The cup has a hole of its own in the middle of its roof.
const Vec3d in_the_cup{0.4, 0.3, 2.};

} // namespace

TEST_CASE("DrainHoleSuggestion - a cup is answered with a hole through its roof", "[DrainHoleSuggestion]")
{
    const TriangleMesh cup    = make_upside_down_cup(10., 4., 20., 5.);
    const AABBMesh     aabb{cup};

    // The issue sits at the bottom of the cup, the roof is 5 mm below its top.
    const auto suggestion = suggest_drain_hole(
        aabb, Transform3d::Identity(), SlaIssue::Kind::Cup, Vec3d{0.4, 0.3, 2.});

    REQUIRE(suggestion.has_value());
    CHECK(suggestion->position_mm.x() == Catch::Approx(0.4));
    CHECK(suggestion->position_mm.y() == Catch::Approx(0.3));
    CHECK(suggestion->position_mm.z() == Catch::Approx(15.));
    // Cut upwards, into the roof, not out of the top of the cup.
    CHECK(suggestion->normal.x() == Catch::Approx(0.).margin(1e-6));
    CHECK(suggestion->normal.y() == Catch::Approx(0.).margin(1e-6));
    CHECK(suggestion->normal.z() == Catch::Approx(1.));
}

TEST_CASE("DrainHoleSuggestion - trapped resin is answered with a hole through its floor",
          "[DrainHoleSuggestion]")
{
    const TriangleMesh hollow = make_hollow_cube(20., 4.);
    const AABBMesh     aabb{hollow};

    // The cavity of the hollow cube starts at z = 4, the resin in it sits on that floor.
    const auto suggestion = suggest_drain_hole(
        aabb, Transform3d::Identity(), SlaIssue::Kind::TrappedResin, Vec3d{10.5, 9.5, 4.});

    REQUIRE(suggestion.has_value());
    CHECK(suggestion->position_mm.x() == Catch::Approx(10.5));
    CHECK(suggestion->position_mm.y() == Catch::Approx(9.5));
    CHECK(suggestion->position_mm.z() == Catch::Approx(4.));
    // Cut downwards, into the floor, so the resin runs out towards the plate.
    CHECK(suggestion->normal.z() == Catch::Approx(-1.));
}

TEST_CASE("DrainHoleSuggestion - the hole has the size the drain hole tool starts with",
          "[DrainHoleSuggestion]")
{
    const TriangleMesh cup = make_upside_down_cup(10., 4., 20., 5.);
    const AABBMesh     aabb{cup};

    SECTION("the defaults of the tool")
    {
        const auto suggestion =
            suggest_drain_hole(aabb, Transform3d::Identity(), SlaIssue::Kind::Cup, in_the_cup);
        REQUIRE(suggestion.has_value());
        CHECK(suggestion->radius_mm == Catch::Approx(5.));
        CHECK(suggestion->height_mm == Catch::Approx(10.));
    }

    SECTION("the size the caller asks for")
    {
        const DrainHoleSuggestionOptions opts{.radius_mm = 2.5, .height_mm = 4.};
        const auto suggestion =
            suggest_drain_hole(aabb, Transform3d::Identity(), SlaIssue::Kind::Cup, in_the_cup, opts);
        REQUIRE(suggestion.has_value());
        CHECK(suggestion->radius_mm == Catch::Approx(2.5));
        CHECK(suggestion->height_mm == Catch::Approx(4.));
    }

    SECTION("the hole to store in the model object")
    {
        const auto suggestion =
            suggest_drain_hole(aabb, Transform3d::Identity(), SlaIssue::Kind::Cup, in_the_cup);
        REQUIRE(suggestion.has_value());
        const Slic3r::Domain::SLA::DrainHole hole = suggestion->to_drain_hole();
        CHECK(hole.pos.x() == Catch::Approx(0.4f));
        CHECK(hole.pos.y() == Catch::Approx(0.3f));
        CHECK(hole.pos.z() == Catch::Approx(15.f));
        CHECK(hole.normal.z() == Catch::Approx(1.f));
        CHECK(hole.radius == Catch::Approx(5.f));
        CHECK(hole.height == Catch::Approx(10.f));
        CHECK_FALSE(hole.failed);
    }
}

TEST_CASE("DrainHoleSuggestion - only a cavity can be answered", "[DrainHoleSuggestion]")
{
    const TriangleMesh cup = make_upside_down_cup(10., 4., 20., 5.);
    const AABBMesh     aabb{cup};

    CHECK(accepts_drain_hole_suggestion(SlaIssue::Kind::Cup));
    CHECK(accepts_drain_hole_suggestion(SlaIssue::Kind::TrappedResin));
    CHECK_FALSE(accepts_drain_hole_suggestion(SlaIssue::Kind::Island));
    CHECK_FALSE(accepts_drain_hole_suggestion(SlaIssue::Kind::Other));

    CHECK_FALSE(suggest_drain_hole(aabb, Transform3d::Identity(), SlaIssue::Kind::Island, in_the_cup)
                    .has_value());
    CHECK_FALSE(suggest_drain_hole(aabb, Transform3d::Identity(), SlaIssue::Kind::Other, in_the_cup)
                    .has_value());
}

TEST_CASE("DrainHoleSuggestion - no surface along the axis of the cavity means no suggestion",
          "[DrainHoleSuggestion]")
{
    const TriangleMesh cup = make_upside_down_cup(10., 4., 20., 5.);
    const AABBMesh     aabb{cup};

    SECTION("next to the model")
    {
        CHECK_FALSE(
            suggest_drain_hole(aabb, Transform3d::Identity(), SlaIssue::Kind::Cup, Vec3d{50., 50., 2.})
                .has_value());
    }
    SECTION("above the model")
    {
        CHECK_FALSE(
            suggest_drain_hole(aabb, Transform3d::Identity(), SlaIssue::Kind::Cup, Vec3d{0.4, 0.3, 40.})
                .has_value());
    }
}

TEST_CASE("DrainHoleSuggestion - the hole is placed in the coordinates of the model, not of the plate",
          "[DrainHoleSuggestion]")
{
    const TriangleMesh cup = make_upside_down_cup(10., 4., 20., 5.);
    const AABBMesh     aabb{cup};

    // An instance scaled by two and moved onto the plate: the layers are in the frame of the
    // plate, the drain holes of the model object are in the frame of the mesh.
    Transform3d mesh_to_world = Transform3d::Identity();
    mesh_to_world.translate(Vec3d{100., 50., 3.});
    mesh_to_world.scale(2.);

    const Vec3d issue_in_the_cup{0.4, 0.3, 2.};
    const auto suggestion = suggest_drain_hole(aabb, mesh_to_world, SlaIssue::Kind::Cup, issue_in_the_cup);

    REQUIRE(suggestion.has_value());
    // The roof of the cup is at z = 15 in the mesh, which is z = 33 on the plate.
    CHECK(suggestion->position_mm.z() == Catch::Approx(15.));
    CHECK(suggestion->normal.z() == Catch::Approx(1.));
    const Vec3d on_the_plate = mesh_to_world * suggestion->position_mm;
    CHECK(on_the_plate.z() == Catch::Approx(33.));
    CHECK(on_the_plate.x() == Catch::Approx((mesh_to_world * issue_in_the_cup).x()));
}

TEST_CASE("DrainHoleSuggestion - the model nearest the cavity along its axis wins",
          "[DrainHoleSuggestion]")
{
    const TriangleMesh lower_cup = make_upside_down_cup(10., 4., 20., 5.);
    const TriangleMesh upper_cup = make_upside_down_cup(10., 4., 20., 5.);

    Transform3d upper_on_the_plate = Transform3d::Identity();
    upper_on_the_plate.translate(Vec3d{0., 0., 60.});

    const std::vector<DrainHoleCandidate> candidates{
        DrainHoleCandidate{Slic3r::Domain::ObjectID{2}, &upper_cup, upper_on_the_plate},
        DrainHoleCandidate{Slic3r::Domain::ObjectID{1}, &lower_cup, Transform3d::Identity()},
    };

    SECTION("the cavity of the model below the other one")
    {
        const auto suggestion = suggest_nearest_drain_hole(
            candidates, SlaIssue::Kind::Cup, Vec3d{0.4, 0.3, 2.});

        REQUIRE(suggestion.has_value());
        CHECK(suggestion->object_id == Slic3r::Domain::ObjectID{1});
        CHECK(suggestion->position_mm.z() == Catch::Approx(15.));
    }

    SECTION("the cavity of the model above the other one")
    {
        const auto suggestion = suggest_nearest_drain_hole(
            candidates, SlaIssue::Kind::Cup, Vec3d{0.4, 0.3, 62.});

        REQUIRE(suggestion.has_value());
        CHECK(suggestion->object_id == Slic3r::Domain::ObjectID{2});
        CHECK(suggestion->position_mm.z() == Catch::Approx(15.));
    }

    SECTION("no model on the axis of the cavity")
    {
        CHECK_FALSE(suggest_nearest_drain_hole(candidates, SlaIssue::Kind::Cup, Vec3d{90., 90., 2.})
                        .has_value());
    }

    SECTION("a candidate without a mesh is skipped")
    {
        const TriangleMesh no_geometry;
        const std::vector<DrainHoleCandidate> without_mesh{
            DrainHoleCandidate{Slic3r::Domain::ObjectID{3}, nullptr, Transform3d::Identity()},
            DrainHoleCandidate{Slic3r::Domain::ObjectID{4}, &no_geometry, Transform3d::Identity()},
        };
        CHECK_FALSE(suggest_nearest_drain_hole(without_mesh, SlaIssue::Kind::Cup, Vec3d{0.4, 0.3, 2.})
                        .has_value());
    }

    SECTION("an island is not answered even with a model right there")
    {
        CHECK_FALSE(
            suggest_nearest_drain_hole(candidates, SlaIssue::Kind::Island, Vec3d{0.4, 0.3, 2.})
                .has_value());
    }
}
