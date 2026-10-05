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

#include <string>
#include <utility>
#include <vector>

#include "Slic3r/App/Plater/SlaSupportAutoPresets.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"

using Catch::Approx;
using Slic3r::App::Plater::auto_support_base_layers;
using Slic3r::App::Plater::is_sla_auto_support_base_point;
using Slic3r::App::Plater::sla_apply_auto_support_presets;
using Slic3r::App::Plater::sla_auto_detail_preset_name;
using Slic3r::App::Plater::sla_support_preset;
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

/// The two presets of the settings at their config definition values, which is what a print preset
/// that does not carry the keys gets: the T0.4 class on the base and the T0.2 one on the detail.
SlaAutoSupportPresets the_presets()
{
    return SlaAutoSupportPresets{sla_support_preset("heavy"), sla_support_preset("light")};
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
