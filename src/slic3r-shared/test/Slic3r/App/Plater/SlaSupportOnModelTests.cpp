// M2.26: the per-point "may this support end on the model" switch. Lychee lets the user mark a
// single support as allowed (or forbidden) to be anchored on the model instead of on the plate,
// while support_buildplate_only and support_max_weight_on_model only decide that for a whole
// object. The switch is a tri-state on the support point (SLA::SupportPoint) and the support tool
// writes it on the points that are selected, next to the per-point geometry controls of M2.16c.
// Nothing here is about the tree the switch produces: that is sla_support_on_model_tests.cpp in
// tests/sla_print.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <unordered_set>
#include <vector>

#include "Slic3r/App/Plater/SlaSupportOnModel.hpp"
#include "Slic3r/App/Plater/SlaSupportPointsEditing.hpp"
#include "Slic3r/App/Plater/SlaSupportPreviewService.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"

using Slic3r::App::Plater::SlaSupportPointsEditing;
using Slic3r::App::Plater::SupportOnModel;
using Slic3r::App::Plater::hash_support_points;
using Slic3r::App::Plater::selection_support_on_model;
using Slic3r::Domain::SLA::SupportPoint;
using Slic3r::Domain::SLA::SupportPoints;

namespace {

SupportPoint make_point()
{
    SupportPoint point;
    point.pos               = Slic3r::Domain::Vec3f{0.f, 0.f, 10.f};
    point.head_front_radius = 0.2f;
    point.type              = Slic3r::Domain::SLA::SupportPointType::manual_add;
    return point;
}

SupportPoint make_point_with(SupportOnModel on_model)
{
    SupportPoint point = make_point();
    point.on_model     = on_model;
    return point;
}

} // namespace

TEST_CASE("A support point carries the switch it is given", "[SlaSupportOnModel]")
{
    // The switch has three states and its default is the one that keeps today's trees: the point
    // follows the object, i.e. its support_buildplate_only.
    SupportPoint inherit;
    CHECK(inherit.on_model == SupportOnModel::Inherit);

    SupportPoint allow = make_point();
    allow.on_model      = SupportOnModel::Allow;
    SupportPoint forbid = make_point();
    forbid.on_model     = SupportOnModel::Forbid;

    // Two points that only differ in the switch are two different points, because the tree they
    // get is built differently.
    CHECK_FALSE(allow == inherit);
    CHECK_FALSE(allow == forbid);
    CHECK(forbid != inherit);
    CHECK(allow == make_point_with(SupportOnModel::Allow));
}

TEST_CASE("The switch of a selection is only shown when the points agree", "[SlaSupportOnModel]")
{
    SupportPoints points{make_point(), make_point(), make_point()};
    points[1].on_model = SupportOnModel::Forbid;
    points[2].on_model = SupportOnModel::Allow;

    SECTION("nothing selected shows no value")
    {
        CHECK_FALSE(selection_support_on_model(points, {}).has_value());
    }

    SECTION("one point shows its own state")
    {
        const std::optional<SupportOnModel> shown = selection_support_on_model(points, {1});
        REQUIRE(shown.has_value());
        CHECK(*shown == SupportOnModel::Forbid);
    }

    SECTION("points that disagree show no value at all")
    {
        CHECK_FALSE(selection_support_on_model(points, {0, 1}).has_value());
        CHECK_FALSE(selection_support_on_model(points, {0, 1, 2}).has_value());
    }

    SECTION("points that agree show the state they share")
    {
        // Both points carry it: a switch only one of them has is the disagreement above.
        SupportPoints agreeing{make_point_with(SupportOnModel::Allow),
                               make_point_with(SupportOnModel::Allow)};
        const std::optional<SupportOnModel> shared = selection_support_on_model(agreeing, {0, 1});
        REQUIRE(shared.has_value());
        CHECK(*shared == SupportOnModel::Allow);
    }

    SECTION("an index that is not a point shows no value, as for the geometry fields")
    {
        CHECK_FALSE(selection_support_on_model(points, {7}).has_value());
    }
}

TEST_CASE("The switch goes on the selected points only", "[SlaSupportOnModel]")
{
    SlaSupportPointsEditing editor;
    editor.points.push_back(make_point());
    editor.points.push_back(make_point());
    editor.points.push_back(make_point());
    editor.select_point(0);
    editor.select_point(2, true);

    // The point that is not selected, with a switch of its own, has to keep it.
    editor.points[1].on_model = SupportOnModel::Allow;

    for (const SupportOnModel on_model : {SupportOnModel::Allow,
                                          SupportOnModel::Forbid,
                                          SupportOnModel::Inherit}) {
        INFO("switch " << static_cast<int>(on_model));
        editor.apply_support_on_model_to_selected(on_model);
        CHECK(editor.points[0].on_model == on_model);
        CHECK(editor.points[2].on_model == on_model);
    }

    CHECK(editor.points[1].on_model == SupportOnModel::Allow);

    // What the points carry is what the control reads back.
    const std::optional<SupportOnModel> shown = editor.selected_support_on_model();
    REQUIRE(shown.has_value());
    CHECK(*shown == SupportOnModel::Inherit);
}

TEST_CASE("The switch of one point does not touch the others", "[SlaSupportOnModel]")
{
    SlaSupportPointsEditing editor;
    editor.points.push_back(make_point());
    editor.points[0].on_model = SupportOnModel::Allow;
    editor.select_point(0);

    editor.apply_support_on_model_to_selected(SupportOnModel::Forbid);

    CHECK(editor.points[0].on_model == SupportOnModel::Forbid);
    CHECK(editor.points.size() == 1);
}

TEST_CASE("A point placed by hand follows the object", "[SlaSupportOnModel]")
{
    SlaSupportPointsEditing editor;

    editor.add_point(Slic3r::Domain::Vec3d{1., 2., 3.});

    // The support tool has no setting for the switch, so a new point takes the default, which is
    // to be an ordinary support whose object decides.
    REQUIRE(editor.points.size() == 1);
    CHECK(editor.points[0].on_model == SupportOnModel::Inherit);
}

TEST_CASE("A change of the per-point switch refreshes the live preview",
          "[SlaSupportOnModel][SlaSupportPreview]")
{
    const std::uint64_t base = hash_support_points(SupportPoints{make_point()});

    // The tree of a point that ends on the model is another tree than the one that reaches the
    // plate, so the key of the preview has to change with it.
    for (const SupportOnModel on_model : {SupportOnModel::Allow, SupportOnModel::Forbid}) {
        INFO("switch " << static_cast<int>(on_model));
        SupportPoint point = make_point();
        point.on_model     = on_model;
        CHECK(hash_support_points(SupportPoints{point}) != base);
    }

    // Two points in the same order but with the switch of the first one changed are two previews.
    SupportPoints two{make_point(), make_point()};
    const std::uint64_t both_plain = hash_support_points(SupportPoints{make_point(), make_point()});
    two[0].on_model = SupportOnModel::Allow;
    CHECK(hash_support_points(two) != both_plain);
}
