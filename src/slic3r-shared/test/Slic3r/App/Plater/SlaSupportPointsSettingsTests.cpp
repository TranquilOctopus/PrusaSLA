// M2.33: the two groups of the support settings of the support points tool. One field set used to do
// two jobs at once (it set the value a clicked point takes AND wrote it on the selected points), so
// it was never clear what a changed value touched. "New supports" is what the next clicked point
// takes, "Selected supports (N)" is what the points of the selection carry. These are the rules the
// two groups route through, with the editing state they act on. No scene, no gizmo, no dialog.
// Since M7.8.1 a preset button is one of the tip classes of the support rulebook (R3) and lands as a
// whole class: the sizes, the ball contact sunk half the tip and the geometry around it.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Slic3r/App/Plater/SlaSupportPointsEditing.hpp"
#include "Slic3r/App/Plater/SlaSupportPointsSettings.hpp"
#include "Slic3r/Domain/ConfigBoxesSLA.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"

using Catch::Approx;
using Slic3r::App::Plater::selection_support_view;
using Slic3r::App::Plater::sla_new_support_preset_changed;
using Slic3r::App::Plater::sla_new_support_setting_changed;
using Slic3r::App::Plater::sla_new_support_values;
using Slic3r::App::Plater::sla_selected_support_preset_changed;
using Slic3r::App::Plater::sla_selected_support_setting_changed;
using Slic3r::App::Plater::sla_support_contact_depth;
using Slic3r::App::Plater::sla_support_point_field_value;
using Slic3r::App::Plater::sla_support_preset;
using Slic3r::App::Plater::sla_support_preset_count;
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
        CHECK(editing.points[3].head_front_radius == Approx(heavy.geometry.tip_diameter_mm / 2.));
        CHECK(editing.points[3].pillar_diameter == Approx(heavy.stem_diameter_mm));
        CHECK(editing.points[3].base_diameter == Approx(heavy.base_diameter_mm));
        CHECK(editing.points[3].base_height == Approx(heavy.base_height_mm));
    }

    SECTION("A preset of the group gives the next point the whole class of its tip")
    {
        SlaSupportPointsEditing editing           = make_editing();
        editing.support_geometry.knot_diameter_mm = 1.5;

        sla_new_support_preset_changed(editing, sla_support_preset("mini"));
        editing.add_point(Vec3d{0., 0., 0.});

        // The class is not only its tip: a ball contact sunk half the tip into the model (R3.1, R3.2
        // and the contact depth with them), the cone under it, a hexagonal stem (R3.3) and a prism
        // foot (R3.4). A preset is one bundle of values, so the knot of before goes with it.
        REQUIRE(editing.points.size() == 4u);
        const SupportPoint& placed = editing.points.back();
        CHECK(placed.head_front_radius == Approx(0.05f));
        CHECK(placed.tip_shape == SupportPoint::TipShape::Ball);
        CHECK(placed.contact_depth == Approx(0.05f));
        CHECK(placed.tip_length == Approx(0.5f));
        CHECK(placed.knot_radius == Approx(0.f));
        CHECK(placed.stem_sides == 6);
        CHECK(placed.base_shape == SupportPoint::BaseShape::Cylinder);
        CHECK(placed.pillar_diameter == Approx(1.f));
        CHECK(placed.base_diameter == Approx(6.f));
        CHECK(placed.base_height == Approx(0.3f));
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
        CHECK(editing.points[2].head_front_radius == Approx(medium.geometry.tip_diameter_mm / 2.));
        CHECK(editing.points[2].pillar_diameter == Approx(medium.stem_diameter_mm));
        CHECK(editing.points[2].base_diameter == Approx(medium.base_diameter_mm));
        CHECK(editing.points[2].base_height == Approx(medium.base_height_mm));
        // The geometry of the class lands with the sizes: a ball contact sunk half its own diameter
        // into the model, a hexagonal stem and a prism foot (rulebook R3).
        CHECK(editing.points[2].tip_shape == SupportPoint::TipShape::Ball);
        CHECK(editing.points[2].contact_depth == Approx(0.15f));
        CHECK(editing.points[2].stem_sides == 6);
        CHECK(editing.points[2].base_shape == SupportPoint::BaseShape::Cylinder);
        // The points that are not selected keep the geometry they carried.
        CHECK(editing.points[0].tip_shape == SupportPoint::TipShape::Default);
        CHECK(editing.points[0].stem_sides == 0);

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
    "The support presets are the tip classes of the support rulebook",
    "[SlaSupportPointsSettings]"
)
{
    // The four ids the presets have had since M2.18 are the first four classes of the rulebook (R3)
    // and "xheavy" is the fifth. No preset is renamed or removed, so a print preset that carries
    // the keys of one of them still loads.
    CHECK(sla_support_preset_name(0) == "mini");
    CHECK(sla_support_preset_name(1) == "light");
    CHECK(sla_support_preset_name(2) == "medium");
    CHECK(sla_support_preset_name(3) == "heavy");
    CHECK(sla_support_preset_name(4) == "xheavy");
    CHECK(sla_support_preset_name(-1) == "mini");
    CHECK(sla_support_preset_name(5) == "xheavy");
    CHECK(sla_support_preset_count == 5);

    // A support is named by the size of its contact, so the tip of a class is the class itself. The
    // rulebook has five of them and none between 0.4 and 0.6.
    CHECK(sla_support_preset("mini").geometry.tip_diameter_mm == Approx(0.1));
    CHECK(sla_support_preset("light").geometry.tip_diameter_mm == Approx(0.2));
    CHECK(sla_support_preset("medium").geometry.tip_diameter_mm == Approx(0.3));
    CHECK(sla_support_preset("heavy").geometry.tip_diameter_mm == Approx(0.4));
    CHECK(sla_support_preset("xheavy").geometry.tip_diameter_mm == Approx(0.6));

    // Every class carries the geometry R3 gives every support: a ball contact (R3.1), the cone
    // under it (R3.2), a hexagonal stem of one diameter (R3.3) and the one prism base of 6 mm by
    // 0.3 mm (R3.4). Only the tip and what it is filled with differ between the classes.
    for (const std::string& name : {"mini", "light", "medium", "heavy", "xheavy"}) {
        INFO("preset " << name);
        const SlaSupportPreset preset = sla_support_preset(name);
        CHECK(preset.geometry.tip_shape == SupportPoint::TipShape::Ball);
        CHECK(preset.geometry.tip_length_mm == Approx(0.5));
        CHECK(preset.geometry.knot_diameter_mm == Approx(0.));
        CHECK(preset.geometry.stem_sides == 6);
        CHECK(preset.geometry.stem_taper == Approx(0.));
        CHECK(preset.geometry.base_shape == SupportPoint::BaseShape::Cylinder);
        CHECK(preset.stem_diameter_mm == Approx(1.0));
        CHECK(preset.base_diameter_mm == Approx(6.0));
        CHECK(preset.base_height_mm == Approx(0.3));
    }

    // An unknown name falls back to the last class rather than to nothing, the way the tool has
    // always done for a preset the config box does not carry.
    CHECK(sla_support_preset("something else").geometry.tip_diameter_mm == Approx(0.6));
}

TEST_CASE(
    "A support contact sinks half its own diameter into the model",
    "[SlaSupportPointsSettings]"
)
{
    // R3.1, and the one number of it: every tip shape reaches as deep as the default pinhead, so the
    // depth follows the tip rather than the shape.
    CHECK(sla_support_contact_depth(0.1) == Approx(0.05));
    CHECK(sla_support_contact_depth(0.2) == Approx(0.1));
    CHECK(sla_support_contact_depth(0.3) == Approx(0.15));
    CHECK(sla_support_contact_depth(0.4) == Approx(0.2));
    CHECK(sla_support_contact_depth(0.6) == Approx(0.3));
    CHECK(sla_support_contact_depth(0.) == Approx(0.));
}

TEST_CASE(
    "A print preset of before the tip classes keeps the preset sizes it stored",
    "[SlaSupportPointsSettings][Config][SLA][Supports]"
)
{
    Slic3r::Domain::SLAPrintSettings print_settings;

    // The keys a print profile of before M7.8.1 stores, with the values it stored. The classes are
    // the rulebook's now, but the keys are the ones of then: an old preset keeps its sizes.
    const std::vector<std::pair<std::string, double>> stored{
        {"support_preset_mini_head_diameter", 0.2},
        {"support_preset_mini_pillar_diameter", 0.5},
        {"support_preset_mini_base_diameter", 1.4},
        {"support_preset_mini_base_height", 0.4},
        {"support_preset_light_head_diameter", 0.30},
        {"support_preset_light_pillar_diameter", 0.8},
        {"support_preset_light_base_diameter", 2.0},
        {"support_preset_light_base_height", 0.5},
        {"support_preset_medium_head_diameter", 0.45},
        {"support_preset_medium_pillar_diameter", 1.2},
        {"support_preset_medium_base_diameter", 3.0},
        {"support_preset_medium_base_height", 0.7},
        {"support_preset_heavy_head_diameter", 0.60},
        {"support_preset_heavy_pillar_diameter", 1.8},
        {"support_preset_heavy_base_diameter", 4.0},
        {"support_preset_heavy_base_height", 1.0},
    };
    for (const auto& [key, value] : stored) {
        INFO("key " << key);
        Slic3r::Domain::ConfigItem* item = print_settings.items.find(key);
        REQUIRE(item != nullptr);
        item->set(value);
        CHECK(item->get<double>() == Approx(value));
    }

    // The fifth class has keys of its own, so a profile can carry the sizes of it as well.
    for (const std::string& suffix :
         {"head_diameter", "pillar_diameter", "base_diameter", "base_height"})
    {
        INFO("key support_preset_xheavy_" << suffix);
        CHECK(print_settings.items.find("support_preset_xheavy_" + suffix) != nullptr);
    }

    // A print preset that carries none of them falls back to the rulebook classes, not to the sizes
    // the keys had before.
    CHECK(sla_support_preset("mini").geometry.tip_diameter_mm == Approx(0.1));
    CHECK(sla_support_preset("heavy").stem_diameter_mm == Approx(1.0));
    CHECK(sla_support_preset("heavy").base_diameter_mm == Approx(6.0));
}
