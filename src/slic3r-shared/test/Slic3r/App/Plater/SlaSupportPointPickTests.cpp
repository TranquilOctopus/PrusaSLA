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
using Slic3r::App::Plater::sla_support_point_click_radius_px;
using Slic3r::App::Plater::sla_support_point_click_target;
using Slic3r::App::Plater::sla_support_point_marker_at;
using Slic3r::App::Plater::sla_support_tree_part_at;
using Slic3r::App::Plater::sla_support_tree_pick_slack_px;
using Slic3r::App::Plater::SlaSupportPointMarker;
using Slic3r::App::Plater::SlaSupportPointTarget;
using Slic3r::App::Plater::SlaSupportTreePart;
using Slic3r::Domain::Vec2d;

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
