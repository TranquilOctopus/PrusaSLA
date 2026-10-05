// M2.37: what the automatic support placement puts where. A pre-supported head or bust has a fat
// support in the neck, where the glue into the body hides it, and small ones over the detail, which
// has to print. The lowest island of the model is the part that prints first, so it is the heavy
// one; every other generated point takes the preset of the detail. Two settings in "Supports & raft"
// say so (support_auto_heavy_base, support_auto_detail_preset) and this file covers the rule they
// feed, over the generated points alone: no model, no scene, nothing sliced.
//
// The presets are the tip classes of the support rulebook since M7.8.1, so "heavy" here is the
// T0.4 class the support tool shows as "0.4" and "light" is T0.2, and a generated point gets the
// whole class (a ball contact sunk half its tip, the cone under it, a hexagonal stem and a prism
// base) rather than only the four sizes.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "Slic3r/App/Plater/SlaSupportAutoPresets.hpp"
#include "Slic3r/App/Plater/SlaSupportPointsSettings.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"

using Catch::Approx;
using Slic3r::App::Plater::auto_support_base_layers;
using Slic3r::App::Plater::is_sla_auto_support_base_point;
using Slic3r::App::Plater::sla_apply_auto_support_presets;
using Slic3r::App::Plater::sla_auto_detail_preset_name;
using Slic3r::App::Plater::sla_auto_heavy_base_preset_index;
using Slic3r::App::Plater::sla_support_preset;
using Slic3r::App::Plater::sla_support_preset_count;
using Slic3r::App::Plater::sla_support_preset_name;
using Slic3r::App::Plater::SlaAutoSupportChoice;
using Slic3r::App::Plater::SlaAutoSupportPresets;
using Slic3r::App::Plater::SlaSupportGeometry;
using Slic3r::App::Plater::SlaSupportPreset;
using Slic3r::Domain::ConfigItemDef;
using Slic3r::Domain::ConfigValue;
using Slic3r::Domain::EnumValueDef;
using Slic3r::Domain::EnumWrapper;
using Slic3r::Domain::Vec3f;
using Slic3r::Domain::sla::SupportAutoDetailPreset;
using Slic3r::Domain::SLA::SupportPoint;
using Slic3r::Domain::SLA::SupportPoints;
using Slic3r::Domain::SLA::SupportPointType;

namespace {

// The print the miniature is sliced at, and the bottom of its own mesh, which is where the object
// frame of the points starts: the generator samples from the first layer up.
constexpr double layer_height_mm = 0.05;
constexpr double lowest_z_mm     = 0.;

SupportPoint point_of(SupportPointType type, double z_mm)
{
    SupportPoint point;
    point.pos               = Vec3f{1.f, 2.f, static_cast<float>(z_mm)};
    point.head_front_radius = 0.2f;
    point.type              = type;
    return point;
}

SupportPoint island_point(double z_mm)
{
    return point_of(SupportPointType::island, z_mm);
}

SupportPoint slope_point(double z_mm)
{
    return point_of(SupportPointType::slope, z_mm);
}

// A support the user placed by hand, which is what the tool writes on a click.
SupportPoint user_point(double z_mm)
{
    return point_of(SupportPointType::manual_add, z_mm);
}

/// The indices of @p points below, named so a test says which point it means.
enum GeneratedIndex
{
    base_first = 0,
    base_second,
    base_slope,
    detail_first,
    detail_second
};

/// What one generation of a miniature gives back: two layers of island points on the base that is
/// glued into something else, a slope point on the same layer (the extra support of an overhang,
/// which is not the base), and two island points over the detail.
SupportPoints a_generated_set()
{
    return {
        island_point(0.5 * layer_height_mm),
        island_point(1.5 * layer_height_mm),
        slope_point(0.5 * layer_height_mm),
        island_point(40.),
        island_point(40.5)
    };
}

/// The same generation with the roles the generator puts on a model (M7.8.2), which is what decides
/// the class of a point since M7.8.8, plus the two kinds of point that keep the band rule of M2.37
/// (no role) and a support of the user (never touched).
enum RoleIndex
{
    anchor_point = 0,
    island_point_above,
    small_island_point,
    overhang_point,
    fragile_point,
    unknown_point_on_the_base,
    unknown_point_over_the_detail,
    point_of_the_user
};

SupportPoints a_generated_set_with_roles()
{
    SupportPoint anchor = island_point(0.5 * layer_height_mm);
    anchor.role         = SupportPoint::Role::Anchor;

    SupportPoint island = island_point(40.);
    island.role         = SupportPoint::Role::Island;

    SupportPoint small_island = island_point(41.);
    small_island.role         = SupportPoint::Role::SmallIsland;

    SupportPoint overhang = slope_point(12.);
    overhang.role         = SupportPoint::Role::Overhang;

    SupportPoint fragile = island_point(25.);
    fragile.role         = SupportPoint::Role::Fragile;

    // Two points from before the roles existed: the one on the base is the band of M2.37, the one
    // over the detail is everything else.
    const SupportPoint unknown_on_the_base = island_point(1.5 * layer_height_mm);
    const SupportPoint unknown_over_detail = island_point(40.5);

    // A support of the user, with sizes of its own and an anchor role: a generation never resizes a
    // support of theirs, whatever its role says.
    SupportPoint placed      = user_point(0.5 * layer_height_mm);
    placed.role              = SupportPoint::Role::Anchor;
    placed.pillar_diameter   = 1.2f;
    placed.base_diameter     = 3.f;
    placed.base_height       = 0.7f;
    placed.head_front_radius = 0.3f;
    placed.tip_shape         = SupportPoint::TipShape::Ball;

    return {
        anchor,
        island,
        small_island,
        overhang,
        fragile,
        unknown_on_the_base,
        unknown_over_detail,
        placed
    };
}

/// The two presets of the settings at their config definition values, which is what a print preset
/// that does not carry the keys gets: the T0.4 class on the base and the T0.2 one on the detail.
SlaAutoSupportPresets the_presets()
{
    return SlaAutoSupportPresets{sla_support_preset("heavy"), sla_support_preset("light")};
}

/// The same, with all five classes filled the way the tool fills them: as the print preset of the
/// printer carries them, which for a preset without the keys is what the config definitions ship
/// (M7.8.8).
SlaAutoSupportPresets the_presets_with_every_class()
{
    SlaAutoSupportPresets presets;
    for (std::size_t index = 0; index < presets.classes.size(); ++index) {
        const std::string name = sla_support_preset_name(static_cast<int>(index));
        presets.classes[index] = sla_support_preset(name);
    }
    presets.base   = presets.classes[std::size_t(sla_auto_heavy_base_preset_index)];
    presets.detail = presets.class_of(sla_auto_detail_preset_name(SlaAutoSupportChoice{}.detail));
    return presets;
}

/// Everything a class puts on a point, not only its four sizes: the tip is the class itself, the
/// contact sinks half of it (R3.1), and the stem, the foot and the cross-section are the geometry
/// every class shares (R3.2 to R3.4). One function writes all of it
/// (apply_sla_support_preset()), so this is the whole contract of a class on a generated point.
void check_class_of(const SupportPoint& point, const SlaSupportPreset& preset)
{
    const SlaSupportGeometry& geometry = preset.geometry;

    // The four sizes the "Supports & raft" settings carry, the tip among them.
    CHECK(2. * point.head_front_radius == Approx(geometry.tip_diameter_mm));
    CHECK(point.pillar_diameter == Approx(preset.stem_diameter_mm));
    CHECK(point.base_diameter == Approx(preset.base_diameter_mm));
    CHECK(point.base_height == Approx(preset.base_height_mm));

    // The rest of the class, which a preset button has landed whole since M7.8.1.
    CHECK(point.tip_shape == geometry.tip_shape);
    CHECK(point.tip_length == Approx(geometry.tip_length_mm));
    CHECK(2. * point.knot_radius == Approx(geometry.knot_diameter_mm));
    CHECK(point.stem_sides == geometry.stem_sides);
    CHECK(point.stem_taper == Approx(geometry.stem_taper));
    CHECK(point.base_shape == geometry.base_shape);

    // Half the tip of the class that landed on the point, not of the class of the other points.
    CHECK(point.contact_depth == Approx(0.5 * geometry.tip_diameter_mm));
}

/// Two bundles are the same class when they carry the same geometry and the same three sizes, which
/// is what a class of the tool is (M7.8.1): the four settings and the rest of R3.
void check_same_class(const SlaSupportPreset& actual, const SlaSupportPreset& expected)
{
    CHECK(actual.geometry == expected.geometry);
    CHECK(actual.stem_diameter_mm == Approx(expected.stem_diameter_mm));
    CHECK(actual.base_diameter_mm == Approx(expected.base_diameter_mm));
    CHECK(actual.base_height_mm == Approx(expected.base_height_mm));
}

const ConfigItemDef* find_def(const std::string& name)
{
    const auto& defs = Slic3r::Domain::get_defs_sla();
    for (const auto& def : defs.defs()) {
        if (def.name == name)
            return &def;
    }
    return nullptr;
}

/// The choices a combobox key offers, in the order of their values: what it stores (the ids the
/// presets of the support tool are named by) beside what it shows (the tip size, since M7.8.1).
std::vector<EnumValueDef> enum_options_of(const ConfigValue& value)
{
    std::vector<EnumValueDef> options;
    value.visit([&options](const EnumWrapper& wrapper) { options = wrapper.def(); });
    return options;
}

} // namespace

TEST_CASE(
    "The two settings of the automatic placement sit with the support point settings",
    "[Config][SLA][Supports][SlaSupportAutoPresets]"
)
{
    const ConfigItemDef* density = find_def("support_points_density_relative");
    REQUIRE(density != nullptr);

    for (const std::string& key : {"support_auto_heavy_base", "support_auto_detail_preset"}) {
        INFO("key " << key);
        const ConfigItemDef* def = find_def(key);
        REQUIRE(def != nullptr);
        CHECK(def->category == ConfigItemDef::Category::Print_Supports);
        CHECK(def->option_group == density->option_group);
        CHECK(def->row_group == "Support points");
        CHECK_FALSE(def->tooltip.empty());
    }

    // The defaults are the behaviour of a miniature printer: the heavy base on and Light for the
    // detail, so a print preset that never sets the keys prints the way the file describes.
    const ConfigItemDef* heavy_base = find_def("support_auto_heavy_base");
    const ConfigItemDef* detail     = find_def("support_auto_detail_preset");
    REQUIRE(heavy_base != nullptr);
    REQUIRE(detail != nullptr);

    CHECK(heavy_base->gui_type == ConfigItemDef::GUIType::checkbox);
    CHECK(heavy_base->init_fn().get<bool>() == true);
    CHECK(detail->gui_type == ConfigItemDef::GUIType::combobox);
    CHECK(detail->init_fn().get<SupportAutoDetailPreset>() == SupportAutoDetailPreset::Light);

    // The choices of the detail are named by the size of their contact, like the preset rows of the
    // same page since M7.8.1, but the values keep the ids the setting has had since M2.37, so a
    // project or a print preset that stores "medium" keeps meaning the same class.
    const std::vector<EnumValueDef> options = enum_options_of(detail->init_fn());
    REQUIRE(options.size() == 3);
    CHECK(options[0].enum_value == static_cast<int>(SupportAutoDetailPreset::Mini));
    CHECK(options[0].str_serialized == "mini");
    CHECK(options[0].str_ui == "0.1");
    CHECK(options[1].enum_value == static_cast<int>(SupportAutoDetailPreset::Light));
    CHECK(options[1].str_serialized == "light");
    CHECK(options[1].str_ui == "0.2");
    CHECK(options[2].enum_value == static_cast<int>(SupportAutoDetailPreset::Medium));
    CHECK(options[2].str_serialized == "medium");
    CHECK(options[2].str_ui == "0.3");
}

TEST_CASE(
    "The heavy supports go on the lowest island of the model and the detail takes the preset",
    "[SlaSupportAutoPresets]"
)
{
    const SlaAutoSupportChoice choice; // the defaults: heavy base on, Light for the detail
    const SlaSupportPreset heavy = sla_support_preset("heavy");
    const SlaSupportPreset light = sla_support_preset("light");

    SECTION(
        "The island points of the base take the heavy class and everything else the detail one"
    )
    {
        SupportPoints points = a_generated_set();
        sla_apply_auto_support_presets(points, lowest_z_mm, layer_height_mm, the_presets(), choice);

        check_class_of(points[base_first], heavy);
        check_class_of(points[base_second], heavy);
        // A slope point on the base layer is the extra support of an overhang, not the part the
        // model stands on, so it is not where the thick support belongs.
        check_class_of(points[base_slope], light);
        check_class_of(points[detail_first], light);
        check_class_of(points[detail_second], light);
    }

    SECTION("The detail preset of the settings is what the other points take")
    {
        SupportPoints points = a_generated_set();
        SlaAutoSupportChoice mini;
        mini.detail = SupportAutoDetailPreset::Mini;
        const SlaAutoSupportPresets presets{the_presets().base, sla_support_preset("mini")};

        sla_apply_auto_support_presets(points, lowest_z_mm, layer_height_mm, presets, mini);

        check_class_of(points[base_first], sla_support_preset("heavy"));
        check_class_of(points[detail_first], sla_support_preset("mini"));
        check_class_of(points[detail_second], sla_support_preset("mini"));
    }

    SECTION("A class lands whole on a generated point and nothing else of the point is touched")
    {
        SupportPoints points = a_generated_set();

        points[detail_first].on_model = SupportPoint::OnModel::Forbid;

        const Vec3f position        = points[detail_first].pos;
        const SupportPointType type = points[detail_first].type;

        sla_apply_auto_support_presets(points, lowest_z_mm, layer_height_mm, the_presets(), choice);

        // Where the point is, what it is and how it rests on the model are not values of a class, so
        // a generation never moves a point or takes away its own will to rest on the model.
        CHECK(points[detail_first].pos == position);
        CHECK(points[detail_first].type == type);
        CHECK(points[detail_first].on_model == SupportPoint::OnModel::Forbid);

        // The rest of it is the class: since M7.8.1 a preset is the whole of R3, not four sizes, so
        // the ball contact and the hexagonal stem of the T0.2 class replace what the point carried
        // (a cone and a round stem here). This is the same write a preset button does, through the
        // one function apply_sla_support_preset().
        check_class_of(points[detail_first], light);
        CHECK(points[detail_first].tip_shape == SupportPoint::TipShape::Ball);
        CHECK(points[detail_first].stem_sides == 6);
    }
}

TEST_CASE(
    "The heavy base of the model is the island within two layers of its lowest point",
    "[SlaSupportAutoPresets]"
)
{
    CHECK(auto_support_base_layers == Approx(2.));

    CHECK(is_sla_auto_support_base_point(
        island_point(0.5 * layer_height_mm),
        lowest_z_mm,
        layer_height_mm
    ));
    CHECK(is_sla_auto_support_base_point(
        island_point(2. * layer_height_mm),
        lowest_z_mm,
        layer_height_mm
    ));
    CHECK_FALSE(is_sla_auto_support_base_point(
        island_point(3. * layer_height_mm),
        lowest_z_mm,
        layer_height_mm
    ));

    // A support that does not hold the model up is never the base, however low it stands.
    CHECK_FALSE(is_sla_auto_support_base_point(slope_point(0.), lowest_z_mm, layer_height_mm));
    CHECK_FALSE(is_sla_auto_support_base_point(user_point(0.), lowest_z_mm, layer_height_mm));

    // Without a layer height there is no band and no base, rather than every point of the model.
    CHECK_FALSE(is_sla_auto_support_base_point(island_point(0.), lowest_z_mm, 0.));
}

TEST_CASE(
    "The heavy base switched off leaves the automatic placement flat",
    "[SlaSupportAutoPresets]"
)
{
    const SlaSupportPreset light = sla_support_preset("light");

    SupportPoints points = a_generated_set();
    SlaAutoSupportChoice choice;
    choice.heavy_base = false;

    sla_apply_auto_support_presets(points, lowest_z_mm, layer_height_mm, the_presets(), choice);

    // Every generated point, the base of the model included, carries the same detail class, which is
    // the one geometry every generated point had before the two settings existed.
    for (const SupportPoint& point : points) {
        INFO("point z " << point.pos.z());
        check_class_of(point, light);
    }
}

TEST_CASE(
    "The automatic placement never resizes a support the user placed",
    "[SlaSupportAutoPresets]"
)
{
    const SlaSupportPreset heavy = sla_support_preset("heavy");

    // A support of the user that stands on the lowest layer of the model, with sizes and a contact
    // of its own, and a generated point next to it.
    SupportPoint placed          = user_point(0.5 * layer_height_mm);
    placed.pillar_diameter       = 1.2f;
    placed.base_diameter         = 3.f;
    placed.base_height           = 0.7f;
    placed.head_front_radius     = 0.3f;
    placed.tip_shape             = SupportPoint::TipShape::Ball;
    placed.stem_sides            = 4;
    const SupportPoint as_placed = placed;

    SupportPoints points{placed, island_point(0.5 * layer_height_mm)};

    sla_apply_auto_support_presets(
        points,
        lowest_z_mm,
        layer_height_mm,
        the_presets(),
        SlaAutoSupportChoice{}
    );

    // Not one of its values changes, and it is not the base even where the base is.
    CHECK(points[0] == as_placed);
    CHECK_FALSE(is_sla_auto_support_base_point(points[0], lowest_z_mm, layer_height_mm));
    // The generated point beside it takes the whole T0.4 class, tip and sizes together.
    check_class_of(points[1], heavy);
}

TEST_CASE(
    "The class of a generated point is the class of its role, with the two settings on top",
    "[SlaSupportAutoPresets][SlaSupportRoles]"
)
{
    const SlaAutoSupportChoice choice; // the defaults: heavy base on, Light for the detail

    SECTION("Every role takes its own class and a point without one keeps the band rule of M2.37")
    {
        SupportPoints points = a_generated_set_with_roles();

        sla_apply_auto_support_presets(
            points,
            lowest_z_mm,
            layer_height_mm,
            the_presets_with_every_class(),
            choice
        );

        // R4.1: the anchor of the lowest island is the heavy class.
        check_class_of(points[anchor_point], sla_support_preset("heavy"));
        // R4.3, R4.6: an island, a small island and an overhang take the detail preset, which is the
        // T0.2 class by default.
        check_class_of(points[island_point_above], sla_support_preset("light"));
        check_class_of(points[small_island_point], sla_support_preset("light"));
        check_class_of(points[overhang_point], sla_support_preset("light"));
        // R4.4 and R4.5: a thin feature takes the minimum tip, whatever the settings ask for.
        check_class_of(points[fragile_point], sla_support_preset("mini"));
        // A point of a project written before the roles existed is still sized the way it was: the
        // band of the base heavy, everything else the detail preset.
        check_class_of(points[unknown_point_on_the_base], sla_support_preset("heavy"));
        check_class_of(points[unknown_point_over_the_detail], sla_support_preset("light"));
    }

    SECTION("The heavy base switched off gives the anchors the detail preset")
    {
        SupportPoints points = a_generated_set_with_roles();
        SlaAutoSupportChoice without_heavy_base;
        without_heavy_base.heavy_base = false;

        sla_apply_auto_support_presets(
            points,
            lowest_z_mm,
            layer_height_mm,
            the_presets_with_every_class(),
            without_heavy_base
        );

        // With nothing to tell the base of the model apart, an anchor is a light support like any
        // other generated point.
        check_class_of(points[anchor_point], sla_support_preset("light"));
        check_class_of(points[unknown_point_on_the_base], sla_support_preset("light"));
        // The other roles are where they were, and the fragile one is still the minimum tip.
        check_class_of(points[island_point_above], sla_support_preset("light"));
        check_class_of(points[fragile_point], sla_support_preset("mini"));
    }

    SECTION("The detail setting names the class of the islands and the overhangs")
    {
        SlaAutoSupportChoice mini;
        mini.detail = SupportAutoDetailPreset::Mini;
        SlaAutoSupportChoice medium;
        medium.detail = SupportAutoDetailPreset::Medium;

        SlaAutoSupportPresets presets = the_presets_with_every_class();

        SupportPoints mini_points   = a_generated_set_with_roles();
        sla_apply_auto_support_presets(mini_points, lowest_z_mm, layer_height_mm, presets, mini);
        // T0.1 everywhere the setting reaches, the anchor keeps its own class and the fragile point
        // is at the same size as the rest.
        check_class_of(mini_points[island_point_above], sla_support_preset("mini"));
        check_class_of(mini_points[small_island_point], sla_support_preset("mini"));
        check_class_of(mini_points[overhang_point], sla_support_preset("mini"));
        check_class_of(mini_points[anchor_point], sla_support_preset("heavy"));
        check_class_of(mini_points[fragile_point], sla_support_preset("mini"));

        SlaAutoSupportPresets medium_presets = presets;
        medium_presets.detail                = medium_presets.class_of("medium");
        SupportPoints medium_points          = a_generated_set_with_roles();
        sla_apply_auto_support_presets(
            medium_points,
            lowest_z_mm,
            layer_height_mm,
            medium_presets,
            medium
        );
        // T0.3, which is the medium tip of R4.3 an island keeps and the heaviest class the setting
        // offers, so nothing here is as thick as the anchor.
        check_class_of(medium_points[island_point_above], sla_support_preset("medium"));
        check_class_of(medium_points[small_island_point], sla_support_preset("medium"));
        check_class_of(medium_points[overhang_point], sla_support_preset("medium"));
        check_class_of(medium_points[anchor_point], sla_support_preset("heavy"));
        check_class_of(medium_points[fragile_point], sla_support_preset("mini"));
    }

    SECTION("A role decides on its own, whatever height the point stands at")
    {
        // The band of M2.37 is a height, the role is not: an anchor far above the base is still the
        // heavy class, and an overhang on the lowest layer of the model is not, since it is not what
        // holds the model up (R4.1, R4.6).
        SupportPoint high_anchor          = slope_point(40.);
        high_anchor.role                  = SupportPoint::Role::Anchor;
        SupportPoint overhang_on_the_base = slope_point(0.5 * layer_height_mm);
        overhang_on_the_base.role         = SupportPoint::Role::Overhang;

        SupportPoints points{high_anchor, overhang_on_the_base};

        sla_apply_auto_support_presets(
            points,
            lowest_z_mm,
            layer_height_mm,
            the_presets_with_every_class(),
            choice
        );

        check_class_of(points[0], sla_support_preset("heavy"));
        check_class_of(points[1], sla_support_preset("light"));
    }

    SECTION("A support of the user is never resized, whatever role it carries")
    {
        SupportPoints points         = a_generated_set_with_roles();
        const SupportPoint as_placed = points[point_of_the_user];

        sla_apply_auto_support_presets(
            points,
            lowest_z_mm,
            layer_height_mm,
            the_presets_with_every_class(),
            choice
        );

        CHECK(points[point_of_the_user] == as_placed);
    }
}

TEST_CASE(
    "The automatic placement carries the five tip classes and answers with the one a name asks for",
    "[SlaSupportAutoPresets]"
)
{
    const SlaAutoSupportPresets presets = the_presets_with_every_class();

    // Every class of the tool is in the bundle, as the print preset of the printer carries it, so a
    // role can pick any of the five (M7.8.8).
    for (int index = 0; index < sla_support_preset_count; ++index) {
        const std::string name = sla_support_preset_name(index);
        INFO("class " << name);
        check_same_class(presets.class_of(name), sla_support_preset(name));
    }

    // The two classes of M2.37 are two of the five: the base is the heavy one (T0.4, button 3) and
    // the detail is whatever support_auto_detail_preset names.
    check_same_class(presets.base, sla_support_preset("heavy"));
    CHECK(
        presets.classes[std::size_t(sla_auto_heavy_base_preset_index)].geometry.tip_diameter_mm
        == Approx(0.4)
    );
    check_same_class(
        presets.detail,
        sla_support_preset(sla_auto_detail_preset_name(SlaAutoSupportChoice{}.detail))
    );

    // A name the tool has no button for is the largest class, as it is in sla_support_preset().
    check_same_class(presets.class_of("no-such-class"), sla_support_preset("xheavy"));
}

TEST_CASE(
    "The detail setting names the preset buttons of the support tool",
    "[SlaSupportAutoPresets]"
)
{
    CHECK(sla_auto_detail_preset_name(SupportAutoDetailPreset::Mini) == "mini");
    CHECK(sla_auto_detail_preset_name(SupportAutoDetailPreset::Light) == "light");
    CHECK(sla_auto_detail_preset_name(SupportAutoDetailPreset::Medium) == "medium");

    // Every name it gives is a preset of the tool, so a generation cannot ask for a preset that does
    // not exist.
    const SlaSupportPreset heavy = sla_support_preset("heavy");
    for (const SupportAutoDetailPreset preset :
         {SupportAutoDetailPreset::Mini,
          SupportAutoDetailPreset::Light,
          SupportAutoDetailPreset::Medium})
    {
        INFO("preset " << static_cast<int>(preset));
        const SlaSupportPreset values = sla_support_preset(sla_auto_detail_preset_name(preset));
        CHECK(values.geometry.tip_diameter_mm > 0.);
        // A detail class is smaller than the base class. Since M7.8.1 the classes differ by the size
        // of their contact only - they share the 1.0 mm stem, the 6 x 0.3 mm foot and the rest of
        // R3 - so the stem diameter is the same for every class and the tip says which one it is.
        CHECK(values.geometry.tip_diameter_mm < heavy.geometry.tip_diameter_mm);
    }

    // The three names are the first three classes of the tool, in the order of the keys: Mini is
    // T0.1, Light is T0.2 and Medium is T0.3, while the class the base of the model takes is T0.4.
    const std::pair<SupportAutoDetailPreset, double> classes[]{
        {SupportAutoDetailPreset::Mini, 0.1},
        {SupportAutoDetailPreset::Light, 0.2},
        {SupportAutoDetailPreset::Medium, 0.3}
    };
    for (const auto& [preset, tip_mm] : classes) {
        INFO("preset " << static_cast<int>(preset));
        const SlaSupportPreset values = sla_support_preset(sla_auto_detail_preset_name(preset));
        CHECK(values.geometry.tip_diameter_mm == Approx(tip_mm));
    }
    CHECK(sla_support_preset("heavy").geometry.tip_diameter_mm == Approx(0.4));
}
