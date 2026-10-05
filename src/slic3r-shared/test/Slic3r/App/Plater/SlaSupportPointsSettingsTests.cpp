// M2.33: the two groups of the support settings of the support points tool. One field set used to do
// two jobs at once (it set the value a clicked point takes AND wrote it on the selected points), so
// it was never clear what a changed value touched. "New supports" is what the next clicked point
// takes, "Selected supports (N)" is what the points of the selection carry. These are the rules the
// two groups route through, with the editing state they act on. No scene, no gizmo, no dialog.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <string>
#include <unordered_set>
#include <vector>

#include "Slic3r/App/Plater/SlaSupportPointsEditing.hpp"
#include "Slic3r/App/Plater/SlaSupportPointsSettings.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"

using Catch::Approx;
using Slic3r::App::Plater::selection_support_view;
using Slic3r::App::Plater::sla_new_support_preset_changed;
using Slic3r::App::Plater::sla_new_support_setting_changed;
using Slic3r::App::Plater::sla_new_support_values;
using Slic3r::App::Plater::sla_selected_support_preset_changed;
using Slic3r::App::Plater::sla_selected_support_setting_changed;
using Slic3r::App::Plater::sla_support_point_field_value;
using Slic3r::App::Plater::sla_support_preset;
using Slic3r::App::Plater::sla_support_preset_name;
using Slic3r::App::Plater::SlaSupportGeometry;
using Slic3r::App::Plater::SlaSupportPointField;
using Slic3r::App::Plater::SlaSupportPointsEditing;
using Slic3r::App::Plater::SlaSupportPreset;
using Slic3r::App::Plater::SlaSupportSelectionView;
using Slic3r::App::Plater::SupportBrace;
using Slic3r::App::Plater::SupportOnModel;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec3f;
using Slic3r::Domain::SLA::SupportPoint;
using Slic3r::Domain::SLA::SupportPointType;

namespace {

SupportPoint make_point(double tip_diameter_mm = 0.4)
{
    SupportPoint point;
    point.pos               = Vec3f{0.f, 0.f, 10.f};
    point.head_front_radius = static_cast<float>(tip_diameter_mm / 2.);
    point.type              = SupportPointType::manual_add;
    return point;
}

// An editing session with three points, none of them selected.
SlaSupportPointsEditing make_editing()
{
    SlaSupportPointsEditing editing;
    editing.points = {make_point(0.4), make_point(0.4), make_point(0.4)};
    return editing;
}

} // namespace

TEST_CASE(
    "The New supports group changes what a clicked point takes and nothing else",
    "[SlaSupportPointsSettings]"
)
{
    SECTION("A value of the group lands on the next point and on no point that is there")
    {
        SlaSupportPointsEditing editing = make_editing();
        editing.select_point(1);

        sla_new_support_setting_changed(editing, SlaSupportPointField::TipDiameter, 0.8);
        sla_new_support_setting_changed(editing, SlaSupportPointField::StemDiameter, 1.5);
        sla_new_support_setting_changed(editing, SlaSupportPointField::StemSides, 6);
        sla_new_support_setting_changed(
            editing,
            SlaSupportPointField::TipShape,
            sla_support_point_field_value(SupportPoint::TipShape::Ball)
        );
        sla_new_support_setting_changed(
            editing,
            SlaSupportPointField::SupportOnModel,
            sla_support_point_field_value(SupportOnModel::Allow)
        );
        sla_new_support_setting_changed(
            editing,
            SlaSupportPointField::Bracing,
            sla_support_point_field_value(SupportBrace::Off)
        );

        // The point that is selected is left exactly as it was: the "Selected supports" group is
        // where a point that is there gets changed.
        for (const SupportPoint& point : editing.points) {
            CHECK(point.head_front_radius == Approx(0.2));
            CHECK(point.pillar_diameter == Approx(0.f));
            CHECK(point.stem_sides == 0);
            CHECK(point.tip_shape == SupportPoint::TipShape::Default);
            CHECK(point.on_model == SupportPoint::OnModel::Inherit);
            CHECK(point.brace == SupportPoint::Brace::Inherit);
        }

        editing.add_point(Vec3d{1., 2., 3.});

        REQUIRE(editing.points.size() == 4u);
        const SupportPoint& placed = editing.points.back();
        CHECK(placed.type == SupportPointType::manual_add);
        CHECK(placed.head_front_radius == Approx(0.4));
        CHECK(placed.pillar_diameter == Approx(1.5));
        CHECK(placed.stem_sides == 6);
        CHECK(placed.tip_shape == SupportPoint::TipShape::Ball);
        CHECK(placed.on_model == SupportPoint::OnModel::Allow);
        CHECK(placed.brace == SupportPoint::Brace::Off);
    }

    SECTION("One value at a time leaves the other values of a clicked point alone")
    {
        SlaSupportPointsEditing editing;
        editing.support_geometry.tip_diameter_mm = 0.4;
        editing.support_geometry.tip_length_mm   = 2.5;
        editing.support_geometry.stem_taper      = 0.3;

        sla_new_support_setting_changed(
            editing,
            SlaSupportPointField::TipShape,
            sla_support_point_field_value(SupportPoint::TipShape::Cone)
        );

        CHECK(editing.support_geometry.tip_diameter_mm == Approx(0.4));
        CHECK(editing.support_geometry.tip_length_mm == Approx(2.5));
        CHECK(editing.support_geometry.stem_taper == Approx(0.3));

        editing.add_point(Vec3d{0., 0., 0.});
        REQUIRE(editing.points.size() == 1u);
        CHECK(editing.points[0].tip_shape == SupportPoint::TipShape::Cone);
        CHECK(editing.points[0].tip_length == Approx(2.5f));
        CHECK(editing.points[0].stem_taper == Approx(0.3f));
    }

    SECTION("A preset of the group is what the next point takes")
    {
        SlaSupportPointsEditing editing = make_editing();
        editing.select_point(0);

        sla_new_support_preset_changed(editing, sla_support_preset("heavy"));

        // The point that is selected is still the point it was.
        CHECK(editing.points[0].head_front_radius == Approx(0.2));
        CHECK(editing.points[0].pillar_diameter == Approx(0.f));

        editing.add_point(Vec3d{0., 0., 0.});
        REQUIRE(editing.points.size() == 4u);
        const SlaSupportPreset heavy = sla_support_preset("heavy");
        CHECK(editing.points[3].head_front_radius == Approx(heavy.tip_diameter_mm / 2.));
        CHECK(editing.points[3].pillar_diameter == Approx(heavy.stem_diameter_mm));
        CHECK(editing.points[3].base_diameter == Approx(heavy.base_diameter_mm));
        CHECK(editing.points[3].base_height == Approx(heavy.base_height_mm));
    }

    SECTION("A preset of the group clears the follow the global setting switches")
    {
        SlaSupportPointsEditing editing    = make_editing();
        editing.pillar_diameter_mm         = 2.;
        editing.pillar_diameter_use_global = true;

        sla_new_support_preset_changed(editing, sla_support_preset("light"));

        CHECK(editing.pillar_diameter_use_global == false);
        CHECK(editing.pillar_diameter_mm == Approx(sla_support_preset("light").stem_diameter_mm));
    }
}

TEST_CASE(
    "The Selected supports group changes the selection and nothing else",
    "[SlaSupportPointsSettings]"
)
{
    SECTION("A value of the group lands on the selected points only")
    {
        SlaSupportPointsEditing editing = make_editing();
        editing.select_point(0);
        editing.select_point(2, true);

        sla_selected_support_setting_changed(editing, SlaSupportPointField::TipDiameter, 0.8);
        sla_selected_support_setting_changed(editing, SlaSupportPointField::StemDiameter, 1.5);

        CHECK(editing.points[0].head_front_radius == Approx(0.4));
        CHECK(editing.points[1].head_front_radius == Approx(0.2));
        CHECK(editing.points[2].head_front_radius == Approx(0.4));
        CHECK(editing.points[0].pillar_diameter == Approx(1.5));
        CHECK(editing.points[1].pillar_diameter == Approx(0.f));
        CHECK(editing.points[2].pillar_diameter == Approx(1.5));
    }

    SECTION("A value of the group does not change what a clicked point takes")
    {
        SlaSupportPointsEditing editing          = make_editing();
        editing.support_geometry.tip_diameter_mm = 0.4;
        editing.pillar_diameter_mm               = 0.8;
        editing.select_point(0);

        sla_selected_support_setting_changed(editing, SlaSupportPointField::TipDiameter, 1.2);
        sla_selected_support_setting_changed(editing, SlaSupportPointField::StemDiameter, 3.);

        CHECK(editing.support_geometry.tip_diameter_mm == Approx(0.4));
        CHECK(editing.pillar_diameter_mm == Approx(0.8));

        editing.add_point(Vec3d{0., 0., 0.});
        REQUIRE(editing.points.size() == 4u);
        CHECK(editing.points[3].head_front_radius == Approx(0.2));
    }

    SECTION("Nothing selected changes nothing")
    {
        SlaSupportPointsEditing editing = make_editing();

        sla_selected_support_setting_changed(editing, SlaSupportPointField::TipDiameter, 0.8);

        for (const SupportPoint& point : editing.points) {
            CHECK(point.head_front_radius == Approx(0.2));
        }
    }

    SECTION("One value at a time leaves the other values of the selection alone")
    {
        SlaSupportPointsEditing editing = make_editing();
        editing.select_point(1);

        sla_selected_support_setting_changed(
            editing,
            SlaSupportPointField::TipShape,
            sla_support_point_field_value(SupportPoint::TipShape::Cone)
        );

        CHECK(editing.points[1].tip_shape == SupportPoint::TipShape::Cone);
        CHECK(editing.points[1].head_front_radius == Approx(0.2));
        CHECK(editing.points[1].tip_length == Approx(0.f));
    }

    SECTION("A size of the group follows the global setting and leaves it again")
    {
        SlaSupportPointsEditing editing = make_editing();
        editing.pillar_diameter_mm      = 0.8;
        editing.select_point(0);
        sla_selected_support_setting_changed(editing, SlaSupportPointField::StemDiameter, 1.5);
        REQUIRE(editing.points[0].pillar_diameter == Approx(1.5));

        // Back to the global setting of Supports & raft: a size of zero is what says that on a point.
        sla_selected_support_setting_changed(
            editing,
            SlaSupportPointField::FollowGlobalStemDiameter,
            1.
        );
        CHECK(editing.points[0].pillar_diameter == Approx(0.f));

        // And out of it again, at the value the tool shows for the size.
        sla_selected_support_setting_changed(
            editing,
            SlaSupportPointField::FollowGlobalStemDiameter,
            0.
        );
        CHECK(editing.points[0].pillar_diameter == Approx(0.8));
    }

    SECTION("The support on model switch of the group lands on the selection")
    {
        SlaSupportPointsEditing editing = make_editing();
        editing.select_point(1);

        sla_selected_support_setting_changed(
            editing,
            SlaSupportPointField::SupportOnModel,
            sla_support_point_field_value(SupportOnModel::Forbid)
        );

        CHECK(editing.points[0].on_model == SupportPoint::OnModel::Inherit);
        CHECK(editing.points[1].on_model == SupportPoint::OnModel::Forbid);
    }

    SECTION("The bracing switch of the group lands on the selection")
    {
        SlaSupportPointsEditing editing = make_editing();
        editing.select_point(1);

        sla_selected_support_setting_changed(
            editing,
            SlaSupportPointField::Bracing,
            sla_support_point_field_value(SupportBrace::Off)
        );

        CHECK(editing.points[0].brace == SupportPoint::Brace::Inherit);
        CHECK(editing.points[1].brace == SupportPoint::Brace::Off);

        // What a clicked point takes is left alone by the other group (M2.38).
        CHECK(editing.new_support_brace == SupportBrace::Inherit);
    }

    SECTION("A preset of the group lands on the selection only")
    {
        SlaSupportPointsEditing editing          = make_editing();
        editing.support_geometry.tip_diameter_mm = 0.4;
        editing.pillar_diameter_mm               = 0.8;
        editing.select_point(2);

        sla_selected_support_preset_changed(editing, sla_support_preset("medium"));

        const SlaSupportPreset medium = sla_support_preset("medium");
        CHECK(editing.points[0].head_front_radius == Approx(0.2));
        CHECK(editing.points[1].head_front_radius == Approx(0.2));
        CHECK(editing.points[2].head_front_radius == Approx(medium.tip_diameter_mm / 2.));
        CHECK(editing.points[2].pillar_diameter == Approx(medium.stem_diameter_mm));
        CHECK(editing.points[2].base_diameter == Approx(medium.base_diameter_mm));
        CHECK(editing.points[2].base_height == Approx(medium.base_height_mm));

        // What a clicked point takes is left alone by the preset of the selection.
        CHECK(editing.support_geometry.tip_diameter_mm == Approx(0.4));
        CHECK(editing.pillar_diameter_mm == Approx(0.8));
    }
}

TEST_CASE(
    "The Selected supports group shows what the selection carries and says Mixed where it cannot",
    "[SlaSupportPointsSettings]"
)
{
    SECTION("Nothing selected shows no values and a count of zero, which hides the group")
    {
        SlaSupportPointsEditing editing = make_editing();

        const SlaSupportSelectionView view = selection_support_view(editing);

        CHECK(view.count == 0u);
        CHECK_FALSE(view.geometry.has_value());
        CHECK_FALSE(view.sizes.has_value());
        CHECK_FALSE(view.follow_global.stem_diameter);
    }

    SECTION("One selected point shows what it carries")
    {
        SlaSupportPointsEditing editing = make_editing();
        editing.select_point(1);

        const SlaSupportSelectionView view = selection_support_view(editing);

        REQUIRE(view.count == 1u);
        REQUIRE(view.geometry.has_value());
        REQUIRE(view.sizes.has_value());
        CHECK(view.geometry->tip_diameter_mm == Approx(0.4));
        CHECK(view.sizes->stem_diameter_mm == Approx(0.f));
        // A size of zero on the point means it follows the global setting.
        CHECK(view.follow_global.stem_diameter);
        CHECK(view.follow_global.base_diameter);
        CHECK(view.follow_global.base_height);
    }

    SECTION("Points that disagree on a geometry value show no geometry at all")
    {
        SlaSupportPointsEditing editing = make_editing();
        // The change lands on the points that are selected when it is made, so the second point
        // joins the selection afterwards: selected before the change, it would have been given the
        // same value and the two would agree.
        editing.select_point(0);
        sla_selected_support_setting_changed(editing, SlaSupportPointField::TipDiameter, 0.8);
        editing.select_point(1, true);

        const SlaSupportSelectionView view = selection_support_view(editing);

        REQUIRE(view.count == 2u);
        CHECK_FALSE(view.geometry.has_value());
        CHECK_FALSE(view.sizes.has_value());
    }

    SECTION("Points that disagree only on a size show the geometry and no size")
    {
        SlaSupportPointsEditing editing = make_editing();
        // Point 0 alone is given the stem diameter, and point 1 joins the selection with the zero
        // it was made with, so the two disagree on that size and on nothing else.
        editing.select_point(0);
        sla_selected_support_setting_changed(editing, SlaSupportPointField::StemDiameter, 1.5);
        editing.select_point(1, true);

        const SlaSupportSelectionView view = selection_support_view(editing);

        REQUIRE(view.geometry.has_value());
        CHECK(view.geometry->tip_diameter_mm == Approx(0.4));
        CHECK_FALSE(view.sizes.has_value());
        CHECK(view.follow_global.stem_diameter == false);
    }

    SECTION("A selection that agrees shows the value they share")
    {
        SlaSupportPointsEditing editing = make_editing();
        editing.select_point(0);
        editing.select_point(2, true);

        const SlaSupportSelectionView view = selection_support_view(editing);

        REQUIRE(view.sizes.has_value());
        CHECK(view.sizes->tip_diameter_mm == Approx(0.4));
        CHECK(view.sizes->base_diameter_mm == Approx(0.f));
        CHECK(view.on_model == SupportOnModel::Inherit);
        CHECK(view.brace == SupportBrace::Inherit);
    }
}

TEST_CASE("The New supports group shows what a clicked point takes", "[SlaSupportPointsSettings]")
{
    SlaSupportPointsEditing editing;
    editing.support_geometry.tip_diameter_mm = 0.45;
    editing.support_geometry.stem_sides      = 6;
    editing.pillar_diameter_mm               = 1.2;
    editing.base_diameter_mm                 = 3.;
    editing.base_height_mm                   = 0.7;
    editing.pillar_diameter_use_global       = false;

    const auto values = sla_new_support_values(editing);

    CHECK(values.geometry.tip_diameter_mm == Approx(0.45));
    CHECK(values.geometry.stem_sides == 6);
    CHECK(values.sizes.stem_diameter_mm == Approx(1.2));
    CHECK(values.sizes.base_diameter_mm == Approx(3.));
    CHECK(values.sizes.base_height_mm == Approx(0.7));
    CHECK(values.follow_global.stem_diameter == false);
    CHECK(values.follow_global.base_diameter == true);
    CHECK(values.on_model == SupportOnModel::Inherit);
    CHECK(values.brace == SupportBrace::Inherit);
}

TEST_CASE(
    "The four support presets keep the values of the config definitions",
    "[SlaSupportPointsSettings]"
)
{
    CHECK(sla_support_preset_name(0) == "mini");
    CHECK(sla_support_preset_name(1) == "light");
    CHECK(sla_support_preset_name(2) == "medium");
    CHECK(sla_support_preset_name(3) == "heavy");

    const SlaSupportPreset mini   = sla_support_preset("mini");
    const SlaSupportPreset light  = sla_support_preset("light");
    const SlaSupportPreset medium = sla_support_preset("medium");
    const SlaSupportPreset heavy  = sla_support_preset("heavy");

    CHECK(mini.tip_diameter_mm == Approx(0.2));
    CHECK(light.tip_diameter_mm == Approx(0.30));
    CHECK(medium.tip_diameter_mm == Approx(0.45));
    CHECK(heavy.tip_diameter_mm == Approx(0.60));

    CHECK(mini.stem_diameter_mm == Approx(0.5));
    CHECK(light.stem_diameter_mm == Approx(0.8));
    CHECK(medium.stem_diameter_mm == Approx(1.2));
    CHECK(heavy.stem_diameter_mm == Approx(1.8));

    CHECK(mini.base_diameter_mm == Approx(1.4));
    CHECK(light.base_diameter_mm == Approx(2.0));
    CHECK(medium.base_diameter_mm == Approx(3.0));
    CHECK(heavy.base_diameter_mm == Approx(4.0));

    CHECK(mini.base_height_mm == Approx(0.4));
    CHECK(light.base_height_mm == Approx(0.5));
    CHECK(medium.base_height_mm == Approx(0.7));
    CHECK(heavy.base_height_mm == Approx(1.0));

    // An unknown name falls back to the heaviest preset rather than to nothing, the way the tool has
    // always done for a preset the config box does not carry.
    CHECK(sla_support_preset("something else").tip_diameter_mm == Approx(heavy.tip_diameter_mm));
}
