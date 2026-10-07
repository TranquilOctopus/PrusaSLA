// M2.35: what a click of the support points tool is on. A click used to be raycast against the
// surface of the model alone, and the nearest point within two tip diameters of that hit was
// selected, which left a support under an overhang reachable only by looking at it from below the
// model, made a click on a marker that stands in front of the surface miss, and did nothing at all
// on the drawn support tree. These are the rules of the picking that replaced it: the drawn markers
// are picked on the screen, then the drawn tree of a point, and a click that hits neither falls back
// to the surface of the model. No scene, no camera and no gizmo: the markers and the pieces of the
// tree are handed in as they are on the screen.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <optional>
#include <vector>

#include "Slic3r/App/Plater/SlaSupportPointPick.hpp"

using Catch::Approx;
using Slic3r::App::Plater::sla_support_click_on_tree;
using Slic3r::App::Plater::sla_support_point_click_radius_px;
using Slic3r::App::Plater::sla_support_point_click_target;
using Slic3r::App::Plater::sla_support_point_marker_at;
using Slic3r::App::Plater::sla_support_point_pick_from_tree_hit;
using Slic3r::App::Plater::sla_support_tree_part_at;
using Slic3r::App::Plater::sla_support_tree_pick_slack_px;
using Slic3r::App::Plater::SlaSupportPointMarker;
using Slic3r::App::Plater::SlaSupportPointTarget;
using Slic3r::App::Plater::SlaSupportTreePart;
using Slic3r::Domain::Vec2d;
using Slic3r::Domain::Vec3d;

namespace {

// A marker where it is drawn, with how big it is drawn and how far it is from the camera. A drawn
// radius of 8 px gives a click radius of 12 px, so the cursor has to be within 12 px of the centre.
SlaSupportPointMarker make_marker(Vec2d screen_pos, double depth_mm, double drawn_radius_px = 8.)
{
    SlaSupportPointMarker marker;
    marker.screen_pos      = screen_pos;
    marker.drawn_radius_px = drawn_radius_px;
    marker.depth_mm        = depth_mm;
    return marker;
}

// The drawn piece of the support tree of one point: the segment the pillar runs along, from the head
// of the point (top, on the model) down to the plate (bottom).
SlaSupportTreePart
make_part(size_t point_index, Vec2d screen_start, Vec2d screen_end, double depth_mm)
{
    SlaSupportTreePart part;
    part.object.object_id   = 1;
    part.object.instance_id = 1;
    part.point_index        = point_index;
    part.screen_start       = screen_start;
    part.screen_end         = screen_end;
    part.drawn_radius_px    = 3.;
    part.depth_mm           = depth_mm;
    return part;
}

} // namespace

TEST_CASE("A click takes the marker nearest to the cursor", "[SlaSupportPointPick]")
{
    const std::vector<SlaSupportPointMarker> markers{
        make_marker({100., 100.}, 30.),
        make_marker({300., 300.}, 30.),
        make_marker({500., 500.}, 30.)
    };

    SECTION("The cursor is on one marker")
    {
        const std::optional<size_t> picked = sla_support_point_marker_at(markers, {105., 96.});
        REQUIRE(picked.has_value());
        CHECK(*picked == 0u);
    }

    SECTION("Two markers are under the cursor and the nearer one on the screen wins")
    {
        // Both within their click radius (12 px) of the cursor, the first one closer to it.
        const std::vector<SlaSupportPointMarker>
            overlapping{make_marker({100., 100.}, 30.), make_marker({112., 100.}, 30.)};
        const std::optional<size_t> picked = sla_support_point_marker_at(overlapping, {100., 100.});
        REQUIRE(picked.has_value());
        CHECK(*picked == 0u);

        const std::optional<size_t> other = sla_support_point_marker_at(overlapping, {112., 100.});
        REQUIRE(other.has_value());
        CHECK(*other == 1u);
    }

    SECTION("Of two markers drawn on the same spot the one nearer the camera wins")
    {
        // The marker of the point on the near side of the model is drawn over the one behind it, and
        // that is the point the user aims at.
        const std::vector<SlaSupportPointMarker>
            overlapping{make_marker({200., 200.}, 90.), make_marker({200., 200.}, 12.)};
        const std::optional<size_t> picked = sla_support_point_marker_at(overlapping, {200., 200.});
        REQUIRE(picked.has_value());
        CHECK(*picked == 1u);
    }

    SECTION("The cursor is on no marker, which is what tells the tool to use the surface")
    {
        CHECK_FALSE(sla_support_point_marker_at(markers, {140., 140.}).has_value());
    }
}

TEST_CASE("The click radius comes from the size the marker is drawn with", "[SlaSupportPointPick]")
{
    SECTION("A marker drawn small still keeps a click target a person can hit")
    {
        // A point far away is drawn as a pixel or two, but a click that lands near it has to take it.
        CHECK(sla_support_point_click_radius_px(0.5) == Approx(6.));
        CHECK(sla_support_point_click_radius_px(2.) == Approx(6.));
    }

    SECTION("A marker drawn large is grabbed a little wider than it is drawn")
    {
        CHECK(sla_support_point_click_radius_px(8.) == Approx(12.));
        CHECK(sla_support_point_click_radius_px(10.) == Approx(15.));
    }

    SECTION("A click never takes a point that is far away on the screen")
    {
        CHECK(sla_support_point_click_radius_px(400.) == Approx(24.));
    }

    SECTION("The size a glyph is drawn with is what a click follows, whatever the tip diameter is")
    {
        // The old rule was two tip diameters in mm, so a click followed the size of the support in
        // the model and not the size of what is on the screen.
        const std::vector<SlaSupportPointMarker> markers{make_marker({100., 100.}, 30.)};
        const double click_radius =
            sla_support_point_click_radius_px(markers.front().drawn_radius_px);

        const std::optional<size_t> inside =
            sla_support_point_marker_at(markers, {100. + click_radius - 1., 100.});
        const std::optional<size_t> outside =
            sla_support_point_marker_at(markers, {100. + click_radius + 1., 100.});

        REQUIRE(inside.has_value());
        CHECK(*inside == 0u);
        CHECK_FALSE(outside.has_value());
    }
}

TEST_CASE(
    "A click on the drawn support tree takes the support it belongs to",
    "[SlaSupportPointPick]"
)
{
    const std::vector<SlaSupportTreePart> parts{
        make_part(0, {100., 100.}, {100., 400.}, 30.),
        make_part(1, {300., 120.}, {300., 420.}, 30.),
        make_part(2, {500., 150.}, {500., 450.}, 30.)
    };

    SECTION("A click on a pillar maps back to the point index of that pillar")
    {
        // Middle of the pillar of the first point, well away from the model surface above it.
        const std::optional<SlaSupportTreePart> picked =
            sla_support_tree_part_at(parts, {100., 250.});
        REQUIRE(picked.has_value());
        CHECK(picked->point_index == 0u);
        CHECK(picked->object.object_id == 1u);

        const std::optional<SlaSupportTreePart> second =
            sla_support_tree_part_at(parts, {305., 250.});
        REQUIRE(second.has_value());
        CHECK(second->point_index == 1u);
    }

    SECTION("A click on the head of a support takes it as well")
    {
        const std::optional<SlaSupportTreePart> picked =
            sla_support_tree_part_at(parts, {498., 160.});
        REQUIRE(picked.has_value());
        CHECK(picked->point_index == 2u);
    }

    SECTION("A click on the foot of a support takes it as well")
    {
        const std::optional<SlaSupportTreePart> picked =
            sla_support_tree_part_at(parts, {302., 415.});
        REQUIRE(picked.has_value());
        CHECK(picked->point_index == 1u);
    }

    SECTION("A thin pillar is still a click target")
    {
        // A pillar thinner than the slack around it stays clickable where a marker of the same size
        // would not.
        const std::optional<SlaSupportTreePart> picked =
            sla_support_tree_part_at(parts, {100. + sla_support_tree_pick_slack_px, 250.});
        REQUIRE(picked.has_value());
        CHECK(picked->point_index == 0u);
    }

    SECTION("The cursor is on no piece of the drawn tree")
    {
        CHECK_FALSE(sla_support_tree_part_at(parts, {200., 250.}).has_value());
    }

    SECTION("Of two pieces of the tree on the same spot the one nearer the camera wins")
    {
        // Two models whose supports stand in front of each other on the screen.
        SlaSupportTreePart near_model = make_part(7, {200., 100.}, {200., 400.}, 10.);
        near_model.object.object_id   = 3;
        SlaSupportTreePart far_model  = make_part(2, {200., 100.}, {200., 400.}, 80.);
        far_model.object.object_id    = 2;

        const std::optional<SlaSupportTreePart> picked =
            sla_support_tree_part_at({far_model, near_model}, {200., 250.});
        REQUIRE(picked.has_value());
        CHECK(picked->point_index == 7u);
        CHECK(picked->object.object_id == 3u);
    }
}

TEST_CASE(
    "What a click of the tool is on: a marker first, then the drawn tree",
    "[SlaSupportPointPick]"
)
{
    const std::vector<SlaSupportPointMarker> markers{make_marker({100., 100.}, 30.)};
    const std::vector<SlaSupportTreePart> parts{make_part(4, {100., 100.}, {100., 400.}, 30.)};

    SECTION("A click on the marker of a point takes that point")
    {
        const std::optional<SlaSupportPointTarget> target =
            sla_support_point_click_target(markers, parts, {100., 100.});
        REQUIRE(target.has_value());
        CHECK(target->index == 0u);
        // The marker is where a drag starts, the tree only selects.
        CHECK(target->from_marker);
    }

    SECTION("A click on the drawn tree of a point takes that point, and does not drag it")
    {
        // The cursor is on the pillar of the point 4, below the marker of the point 0.
        const std::optional<SlaSupportPointTarget> target =
            sla_support_point_click_target(markers, parts, {100., 250.});
        REQUIRE(target.has_value());
        CHECK(target->index == 4u);
        CHECK_FALSE(target->from_marker);
    }

    SECTION("The marker of a point wins over the drawn tree of another point")
    {
        // The cursor is on the marker of point 0, which stands in front of the pillar of point 4.
        const std::optional<SlaSupportPointTarget> target =
            sla_support_point_click_target(markers, parts, {104., 96.});
        REQUIRE(target.has_value());
        CHECK(target->index == 0u);
        CHECK(target->from_marker);
    }

    SECTION("A click that hits no marker and no drawn tree falls back to the surface of the model")
    {
        // Nothing at the cursor: the tool raycasts the model and adds a point on the surface it hits,
        // or clears the selection when the ray hits no model at all.
        const std::optional<SlaSupportPointTarget> target =
            sla_support_point_click_target(markers, parts, {600., 300.});
        CHECK_FALSE(target.has_value());
    }
}

// M2.39a: picking a support point from a hit on the real support tree mesh.
// The function takes the support point heads in world coordinates and a hit position on the tree mesh,
// and returns the index of the point whose head is nearest in 3D to the hit, preferring points whose
// head is ABOVE the hit (head_z >= hit_z - 0.5 mm). A click on a stem or trunk belongs to the head it carries.
TEST_CASE(
    "Picking a support point from a tree mesh hit (sla_support_point_pick_from_tree_hit)",
    "[SlaSupportPointPick][M2.39a]"
)
{
    SECTION("A lone vertical support: hit on the stem picks its head")
    {
        const std::vector<Vec3d> heads{{10., 10., 50.}}; // head at z=50
        const Vec3d hit{10., 10., 10.};                  // hit on stem at z=10
        const auto picked = sla_support_point_pick_from_tree_hit(heads, hit);
        REQUIRE(picked.has_value());
        CHECK(*picked == 0u);
    }

    SECTION("Two supports sharing a trunk: hit on shared trunk picks the head above the hit")
    {
        // Two supports whose stems merge into a shared trunk at z=20
        const std::vector<Vec3d> heads{{10., 10., 50.}, {12., 10., 45.}};
        const Vec3d hit{11., 10., 15.}; // hit on shared trunk below both heads
        const auto picked = sla_support_point_pick_from_tree_hit(heads, hit);
        REQUIRE(picked.has_value());
        // The hit is below both heads; nearest in 3D is point 0 (head at z=50, dist~35) vs point 1 (head at z=45, dist~30)
        // But both are above the hit (z >= 15 - 0.5 = 14.5), so nearest 3D distance wins -> point 1
        CHECK(*picked == 1u);
    }

    SECTION("A leaning branch: hit on the branch picks the head it connects to")
    {
        // A support whose stem leans: head at (15, 10, 40), base at (10, 10, 0)
        // Hit is on the leaning stem, closer to head than base
        const std::vector<Vec3d> heads{{15., 10., 40.}};
        const Vec3d hit{13., 10., 20.}; // on the leaning stem
        const auto picked = sla_support_point_pick_from_tree_hit(heads, hit);
        REQUIRE(picked.has_value());
        CHECK(*picked == 0u);
    }

    SECTION("Hit far from every head still picks the nearest one above the hit")
    {
        const std::vector<Vec3d> heads{
            {0., 0., 50.},   // point 0
            {100., 0., 40.}, // point 1
            {0., 100., 30.}  // point 2
        };
        // Hit is at (50, 50, 10) - equidistant from all in XY, but different Z
        const Vec3d hit{50., 50., 10.};
        const auto picked = sla_support_point_pick_from_tree_hit(heads, hit);
        REQUIRE(picked.has_value());
        // All heads are above the hit (z >= 9.5). Nearest in 3D:
        // point 0: dist^2 = 50^2 + 50^2 + 40^2 = 2500+2500+1600 = 6600
        // point 1: dist^2 = 50^2 + 50^2 + 30^2 = 2500+2500+900 = 5900
        // point 2: dist^2 = 50^2 + 50^2 + 20^2 = 2500+2500+400 = 5400 -> point 2 wins
        CHECK(*picked == 2u);
    }

    SECTION("Hit above all heads: prefers heads above hit, falls back to nearest")
    {
        const std::vector<Vec3d> heads{{0., 0., 10.}, {10., 0., 5.}};
        const Vec3d hit{0., 0., 20.}; // hit is ABOVE both heads
        const auto picked = sla_support_point_pick_from_tree_hit(heads, hit);
        REQUIRE(picked.has_value());
        // Neither head is above the hit (head_z >= 20 - 0.5 = 19.5 is false for both)
        // So both are "not above", nearest 3D distance wins -> point 0 (dist=10 vs 22.36)
        CHECK(*picked == 0u);
    }

    SECTION("Empty point list returns nullopt")
    {
        const std::vector<Vec3d> heads{};
        const Vec3d hit{0., 0., 0.};
        const auto picked = sla_support_point_pick_from_tree_hit(heads, hit);
        CHECK_FALSE(picked.has_value());
    }

    SECTION("Point exactly at hit threshold (head_z == hit_z - 0.5) counts as above")
    {
        const std::vector<Vec3d> heads{{0., 0., 9.5}}; // exactly at threshold
        const Vec3d hit{0., 0., 10.};
        const auto picked = sla_support_point_pick_from_tree_hit(heads, hit);
        REQUIRE(picked.has_value());
        CHECK(*picked == 0u);
    }

    SECTION("Point just below threshold (head_z == hit_z - 0.5 - epsilon) does not count as above")
    {
        const std::vector<Vec3d> heads{{0., 0., 9.49}}; // just below threshold
        const Vec3d hit{0., 0., 10.};
        const auto picked = sla_support_point_pick_from_tree_hit(heads, hit);
        REQUIRE(picked.has_value());
        // Not above, but only one point so it wins by distance
        CHECK(*picked == 0u);
    }

    SECTION("Multiple points, one above threshold wins over closer one below threshold")
    {
        // Point 0: head at z=9.4 (below threshold 9.5), very close in XY
        // Point 1: head at z=10.0 (above threshold), farther in XY
        const std::vector<Vec3d> heads{{0., 0., 9.4}, {5., 0., 10.0}};
        const Vec3d hit{0., 0., 10.};
        const auto picked = sla_support_point_pick_from_tree_hit(heads, hit);
        REQUIRE(picked.has_value());
        // Point 1 is above (z=10.0 >= 9.5), point 0 is not (z=9.4 < 9.5)
        // So point 1 wins even though it's farther in 3D
        CHECK(*picked == 1u);
    }
}

// M2.39a: deciding whether a click is on the support tree vs the model surface.
// The function compares the camera-to-hit distances and returns true only when the model
// was not hit, or when the tree hit is nearer by more than epsilon (default 0.01 mm).
TEST_CASE(
    "Click on tree vs model surface (sla_support_click_on_tree)",
    "[SlaSupportPointPick][M2.39a]"
)
{
    SECTION("No tree hit -> false")
    {
        CHECK_FALSE(sla_support_click_on_tree(std::nullopt, 100.));
        CHECK_FALSE(sla_support_click_on_tree(std::nullopt, std::nullopt));
    }

    SECTION("Tree hit, no model hit -> true")
    {
        CHECK(sla_support_click_on_tree(50., std::nullopt));
    }

    SECTION("Tree hit nearer than model by more than epsilon -> true")
    {
        // Tree at 50mm, model at 100mm, epsilon 0.01 -> tree wins
        CHECK(sla_support_click_on_tree(50., 100.));
        CHECK(sla_support_click_on_tree(50., 50.02)); // 0.02 > 0.01
    }

    SECTION("Tree hit nearer but within epsilon -> false (model wins)")
    {
        // Tree at 50mm, model at 50.005mm, epsilon 0.01 -> model wins (difference 0.005 < 0.01)
        CHECK_FALSE(sla_support_click_on_tree(50., 50.005));
        // Tree at 50mm, model at 50.01mm, epsilon 0.01 -> model wins (difference 0.01 not > 0.01)
        CHECK_FALSE(sla_support_click_on_tree(50., 50.01));
    }

    SECTION("Model hit nearer -> false")
    {
        CHECK_FALSE(sla_support_click_on_tree(100., 50.));
    }

    SECTION("Custom epsilon")
    {
        // With epsilon 0.1, tree at 50 needs model at > 50.1 to win
        CHECK(sla_support_click_on_tree(50., 50.11, 0.1));
        CHECK_FALSE(sla_support_click_on_tree(50., 50.1, 0.1));
        CHECK_FALSE(sla_support_click_on_tree(50., 50.05, 0.1));
    }
}