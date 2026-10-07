#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "Slic3r/Domain/ConfigBoxesSLA.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/SLA/RaftPreset.hpp"

using namespace Catch::Matchers;
using Slic3r::Domain::ConfigItemDef;
using Slic3r::Domain::SLAConfigLocation;

TEST_CASE("SLA Raft settings have correct category and option group", "[Config][SLA][Raft]")
{
    const auto& defs = Slic3r::Domain::get_defs_sla();

    // Helper to find a config item by name
    auto find_def = [&defs](const std::string& name) -> const ConfigItemDef* {
        for (const auto& def : defs.defs()) {
            if (def.name == name) {
                return &def;
            }
        }
        return nullptr;
    };

    // Check raft_type setting
    {
        const ConfigItemDef* def = find_def("raft_type");
        REQUIRE(def != nullptr);
        CHECK(def->category == ConfigItemDef::Category::Print_Pad);
        CHECK(def->option_group == ConfigItemDef::OptionGroup::Print_Pad_Pad);
        CHECK(def->label == "Raft type");
        CHECK(def->gui_type == ConfigItemDef::GUIType::combobox);
        // The raft type turns the raft on or off and decides whether it goes around the object.
        CHECK(def->tooltip.find("The shape of the raft under the object") != std::string::npos);
        CHECK(def->tooltip.find("None, the default, prints no raft") != std::string::npos);
        // Skate is around object with half the expansion and a 70 degree wall.
        CHECK(def->tooltip.find("half the expansion and a 70 degree wall slope") != std::string::npos);
        // The type picks the knobs, so it is shown above them.
        CHECK(def->tooltip.find("shows only those") != std::string::npos);
        // None is the default (M7.8.4b), so a new config gets no raft by default.
        REQUIRE(def->init_fn != nullptr);
        CHECK(def->init_fn().get<Slic3r::Domain::sla::RaftType>()
              == Slic3r::Domain::sla::RaftType::None);
        CHECK(def->tooltip.find("would form a suction cup") != std::string::npos);
        // The entries are in the order of their enumerators, which the definitions require
        // (check_enum_def asserts that an enum list is sorted), so Auto stays the first row even
        // though None is the default.
        const Slic3r::Domain::EnumValueDefs& enum_values =
            def->init_fn().get<Slic3r::Domain::EnumWrapper>().def();
        REQUIRE(!enum_values.empty());
        CHECK(std::is_sorted(enum_values.begin(), enum_values.end()));
        // What a preset or a project stores is the name of the type and not its number, so the four
        // shapes from before Auto keep their names and every project that names one of them reads
        // what it always read.
        const std::vector<std::string> stored_names{"auto", "none", "full", "around_object", "skate"};
        REQUIRE(enum_values.size() == stored_names.size());
        for (size_t i = 0; i < stored_names.size(); ++i) {
            INFO("raft type " << i);
            CHECK(enum_values[i].str_serialized == stored_names[i]);
        }
        for (const Slic3r::Domain::sla::RaftType type :
             {Slic3r::Domain::sla::RaftType::None,
              Slic3r::Domain::sla::RaftType::Full,
              Slic3r::Domain::sla::RaftType::AroundObject,
              Slic3r::Domain::sla::RaftType::Skate,
              Slic3r::Domain::sla::RaftType::Auto}) {
            INFO("raft type " << static_cast<int>(type));
            CHECK(Slic3r::Domain::SLA::raft_type_name(type) != std::string());
        }
        // raft_type is the first row of the Raft group, the knobs it drives come after it.
        CHECK(def->order == 0);
    }

    // pad_enable ("Use raft") is replaced by raft_type, so it is hidden, not deleted.
    {
        const ConfigItemDef* def = find_def("pad_enable");
        REQUIRE(def != nullptr);
        CHECK(def->category == ConfigItemDef::Category::Hidden);
        CHECK(def->option_group == ConfigItemDef::OptionGroup::Print_Pad_Pad);
        CHECK(def->label == "Use raft");
        CHECK(def->tooltip.find("The raft type decides this") != std::string::npos);
    }

    // Check pad_wall_thickness (now "Raft wall thickness")
    {
        const ConfigItemDef* def = find_def("pad_wall_thickness");
        REQUIRE(def != nullptr);
        CHECK(def->category == ConfigItemDef::Category::Print_Pad);
        CHECK(def->option_group == ConfigItemDef::OptionGroup::Print_Pad_Pad);
        CHECK(def->label == "Raft wall thickness");
        CHECK(def->tooltip == "The thickness of the raft walls.");
    }

    // Check pad_wall_height (now "Raft height")
    {
        const ConfigItemDef* def = find_def("pad_wall_height");
        REQUIRE(def != nullptr);
        CHECK(def->category == ConfigItemDef::Category::Print_Pad);
        CHECK(def->option_group == ConfigItemDef::OptionGroup::Print_Pad_Pad);
        CHECK(def->label == "Raft height");
        // The height is the cavity the object sits in, not a general raft height.
        CHECK(def->tooltip.find("cavity") != std::string::npos);
    }

    // Check pad_brim_size (now "Raft expansion")
    {
        const ConfigItemDef* def = find_def("pad_brim_size");
        REQUIRE(def != nullptr);
        CHECK(def->category == ConfigItemDef::Category::Print_Pad);
        CHECK(def->option_group == ConfigItemDef::OptionGroup::Print_Pad_Pad);
        CHECK(def->label == "Raft expansion");
        CHECK(def->tooltip.find("How far the raft reaches around the object") != std::string::npos);
    }

    // Check pad_wall_slope (now "Raft slope")
    {
        const ConfigItemDef* def = find_def("pad_wall_slope");
        REQUIRE(def != nullptr);
        CHECK(def->category == ConfigItemDef::Category::Print_Pad);
        CHECK(def->option_group == ConfigItemDef::OptionGroup::Print_Pad_Pad);
        CHECK(def->label == "Raft slope");
        CHECK(def->tooltip.find("build plate") != std::string::npos);
    }

    // Check raft_floor_thickness, the slab on the build plate the raft stands on
    {
        const ConfigItemDef* def = find_def("raft_floor_thickness");
        REQUIRE(def != nullptr);
        CHECK(def->category == ConfigItemDef::Category::Print_Pad);
        CHECK(def->option_group == ConfigItemDef::OptionGroup::Print_Pad_Pad);
        CHECK(def->label == "Raft floor thickness");
        CHECK(def->gui_type == ConfigItemDef::GUIType::textfield);
        CHECK(def->tooltip.find("slab on the build plate") != std::string::npos);
        CHECK(def->tooltip.find("Zero keeps the floor as thick as the walls") != std::string::npos);
        // It is off by default, so a raft without a floor of its own is the raft of today.
        REQUIRE(def->init_fn != nullptr);
        CHECK(def->init_fn().get<double>() == Catch::Approx(0.));
        // It belongs with the wall thickness it is a second value of.
        const ConfigItemDef* wall = find_def("pad_wall_thickness");
        const ConfigItemDef* brim = find_def("pad_brim_size");
        REQUIRE(wall != nullptr);
        REQUIRE(brim != nullptr);
        CHECK(wall->order < def->order);
        CHECK(def->order < brim->order);
    }

    // Check raft_edge_taper, the bevel on the top edge of the raft
    {
        const ConfigItemDef* def = find_def("raft_edge_taper");
        REQUIRE(def != nullptr);
        CHECK(def->category == ConfigItemDef::Category::Print_Pad);
        CHECK(def->option_group == ConfigItemDef::OptionGroup::Print_Pad_Pad);
        CHECK(def->label == "Raft edge taper");
        CHECK(def->tooltip.find("pried off the build plate") != std::string::npos);
        // It shapes the same wall as the raft slope, so it goes right after it.
        const ConfigItemDef* slope = find_def("pad_wall_slope");
        const ConfigItemDef* merge = find_def("pad_max_merge_distance");
        REQUIRE(slope != nullptr);
        REQUIRE(merge != nullptr);
        CHECK(slope->order < def->order);
        CHECK(def->order < merge->order);
    }

    // Check raft_infill and the three knobs that shape the pattern it cuts out of the raft
    {
        const ConfigItemDef* def = find_def("raft_infill");
        REQUIRE(def != nullptr);
        CHECK(def->category == ConfigItemDef::Category::Print_Pad);
        CHECK(def->option_group == ConfigItemDef::OptionGroup::Print_Pad_Pad);
        CHECK(def->label == "Raft infill");
        CHECK(def->gui_type == ConfigItemDef::GUIType::combobox);
        CHECK(def->tooltip.find("never rests on a hole") != std::string::npos);

        // The pattern and its three knobs come after the edge taper, which is the last of the knobs
        // that shape the raft itself.
        const ConfigItemDef* taper = find_def("raft_edge_taper");
        REQUIRE(taper != nullptr);
        CHECK(taper->order < def->order);

        for (const std::string& key : {"raft_infill_spacing",
                                       "raft_infill_wall",
                                       "raft_infill_skin"}) {
            INFO("setting " << key);
            const ConfigItemDef* knob = find_def(key);
            REQUIRE(knob != nullptr);
            CHECK(knob->category == ConfigItemDef::Category::Print_Pad);
            CHECK(knob->option_group == ConfigItemDef::OptionGroup::Print_Pad_Pad);
            CHECK(knob->gui_type == ConfigItemDef::GUIType::textfield);
            CHECK(knob->units.size() == 1);
        }

        const ConfigItemDef* spacing = find_def("raft_infill_spacing");
        const ConfigItemDef* wall    = find_def("raft_infill_wall");
        const ConfigItemDef* skin    = find_def("raft_infill_skin");
        REQUIRE(spacing != nullptr);
        REQUIRE(wall != nullptr);
        REQUIRE(skin != nullptr);

        // The cell size and the material between two cells may not be zero: without a cell the
        // raft would be filled back up to solid and without material between two cells it would
        // be full of holes.
        CHECK(spacing->min.value() > 0.);
        CHECK(wall->min.value() > 0.);

        // The knobs read in the order the raft needs them: the cells, the material between them
        // and the skin that is left under the top face.
        CHECK(def->order < spacing->order);
        CHECK(spacing->order < wall->order);
        CHECK(wall->order < skin->order);
    }

    // Check raft_interface_thickness and raft_interface_exposure, the band at the top of the raft
    {
        const ConfigItemDef* def = find_def("raft_interface_thickness");
        REQUIRE(def != nullptr);
        CHECK(def->category == ConfigItemDef::Category::Print_Pad);
        CHECK(def->option_group == ConfigItemDef::OptionGroup::Print_Pad_Pad);
        CHECK(def->label == "Raft interface thickness");
        CHECK(def->gui_type == ConfigItemDef::GUIType::textfield);
        CHECK(def->units.size() == 1);
        // It is off by default, so a print without it is the print of today.
        REQUIRE(def->init_fn != nullptr);
        CHECK(def->init_fn().get<double>() == Catch::Approx(0.));
        // The formats with one exposure for the whole print cannot apply it, and the setting says so.
        CHECK(def->tooltip.find("no per-layer exposure") != std::string::npos);
        // A layer that is a bottom layer is exposed at the bottom exposure, not the interface one.
        CHECK(def->tooltip.find("bottom layer is exposed at the bottom exposure") != std::string::npos);

        const ConfigItemDef* exposure = find_def("raft_interface_exposure");
        REQUIRE(exposure != nullptr);
        CHECK(exposure->category == ConfigItemDef::Category::Print_Pad);
        CHECK(exposure->option_group == ConfigItemDef::OptionGroup::Print_Pad_Pad);
        CHECK(exposure->label == "Raft interface exposure");
        CHECK(exposure->gui_type == ConfigItemDef::GUIType::textfield);
        CHECK(exposure->tooltip.find("single exposure for the whole print") != std::string::npos);

        // They come with the raft itself, after the pattern cut into it.
        const ConfigItemDef* skin = find_def("raft_infill_skin");
        const ConfigItemDef* merge = find_def("pad_max_merge_distance");
        REQUIRE(skin != nullptr);
        REQUIRE(merge != nullptr);
        CHECK(skin->order < def->order);
        CHECK(def->order < exposure->order);
        CHECK(exposure->order < merge->order);
    }

    // Check pad_object_gap (now "Raft gap to object")
    {
        const ConfigItemDef* def = find_def("pad_object_gap");
        REQUIRE(def != nullptr);
        CHECK(def->category == ConfigItemDef::Category::Print_Pad);
        CHECK(def->option_group == ConfigItemDef::OptionGroup::Print_Pad_Pad);
        CHECK(def->label == "Raft gap to object");
        CHECK(def->tooltip.find("gap left between the object bottom and the raft") != std::string::npos);
    }

    // pad_around_object ("Raft around object") is replaced by raft_type, so it is hidden.
    {
        const ConfigItemDef* def = find_def("pad_around_object");
        REQUIRE(def != nullptr);
        CHECK(def->category == ConfigItemDef::Category::Hidden);
        CHECK(def->option_group == ConfigItemDef::OptionGroup::Print_Pad_Pad);
        CHECK(def->label == "Raft around object");
        CHECK(def->tooltip.find("The raft type decides this") != std::string::npos);
        CHECK(def->tooltip.find("Around object and Skate") != std::string::npos);
    }

    // Check pad_around_object_everywhere (now "Raft around object everywhere")
    {
        const ConfigItemDef* def = find_def("pad_around_object_everywhere");
        REQUIRE(def != nullptr);
        CHECK(def->category == ConfigItemDef::Category::Print_Pad);
        CHECK(def->option_group == ConfigItemDef::OptionGroup::Print_Pad_Pad);
        CHECK(def->label == "Raft around object everywhere");
        CHECK(def->tooltip.find("follow the object everywhere") != std::string::npos);
    }

    // Check pad_max_merge_distance, which the raft type passes through
    {
        const ConfigItemDef* def = find_def("pad_max_merge_distance");
        REQUIRE(def != nullptr);
        CHECK(def->category == ConfigItemDef::Category::Print_Pad);
        CHECK(def->option_group == ConfigItemDef::OptionGroup::Print_Pad_Pad);
        CHECK(def->label == "Max merge distance");
        // The tooltip talks about rafts, not about the FFF pad wording.
        CHECK(def->tooltip.find("smaller rafts") != std::string::npos);
        CHECK(def->tooltip.find("pads") == std::string::npos);
    }

    {
        const ConfigItemDef* def = find_def("pad_object_connector_stride");
        REQUIRE(def != nullptr);
        CHECK(def->label == "Raft object connector stride");
        CHECK(def->tooltip.find("tie the object to the raft") != std::string::npos);
    }

    {
        const ConfigItemDef* def = find_def("pad_object_connector_width");
        REQUIRE(def != nullptr);
        CHECK(def->label == "Raft object connector width");
        CHECK(def->tooltip.find("tie the object to the raft") != std::string::npos);
    }

    {
        const ConfigItemDef* def = find_def("pad_object_connector_penetration");
        REQUIRE(def != nullptr);
        CHECK(def->label == "Raft object connector penetration");
        CHECK(def->tooltip.find("tie the object to the raft") != std::string::npos);
    }

    // The page and the group the raft settings live in are called Raft, not Pad.
    CHECK(ConfigItemDef::translate_category(
              ConfigItemDef::Category::Print_Pad,
              Slic3r::Domain::PrinterTechnology::SLA
          ) == "Raft");
    CHECK(ConfigItemDef::translate_option_group(ConfigItemDef::OptionGroup::Print_Pad_Pad)
          == "Raft");
}

TEST_CASE("The raft type decides which raft settings are shown", "[Config][SLA][Raft]")
{
    using Slic3r::Domain::sla::RaftType;
    using Slic3r::Domain::SLA::raft_type_uses_setting;
    using Slic3r::Domain::SLA::raft_type_visible_settings;

    SECTION("the type itself is always shown")
    {
        for (const RaftType type : {RaftType::Auto,
                                    RaftType::None,
                                    RaftType::Full,
                                    RaftType::AroundObject,
                                    RaftType::Skate}) {
            INFO("raft type " << static_cast<int>(type));
            CHECK(raft_type_uses_setting(type, "raft_type"));
            // The first row of the group is the type, then the knobs it drives.
            CHECK(raft_type_visible_settings(type).front() == "raft_type");
        }
    }

    SECTION("None prints no raft, so no knob applies")
    {
        for (const std::string& key : {"pad_wall_height",
                                       "pad_wall_thickness",
                                       "raft_floor_thickness",
                                       "pad_brim_size",
                                       "pad_wall_slope",
                                       "raft_edge_taper",
                                       "raft_infill",
                                       "raft_infill_spacing",
                                       "raft_infill_wall",
                                       "raft_infill_skin",
                                       "raft_interface_thickness",
                                       "raft_interface_exposure",
                                       "pad_max_merge_distance",
                                       "pad_object_gap",
                                       "pad_around_object_everywhere",
                                       "pad_object_connector_stride",
                                       "pad_object_connector_width",
                                       "pad_object_connector_penetration"}) {
            INFO("setting " << key);
            CHECK_FALSE(raft_type_uses_setting(RaftType::None, key));
        }
        CHECK(raft_type_visible_settings(RaftType::None).size() == 1);
    }

    SECTION("a full plate raft never embeds the object")
    {
        for (const std::string& key : {"pad_wall_height",
                                       "pad_wall_thickness",
                                       "raft_floor_thickness",
                                       "pad_brim_size",
                                       "pad_wall_slope",
                                       "raft_edge_taper",
                                       "raft_infill",
                                       "raft_infill_spacing",
                                       "raft_infill_wall",
                                       "raft_infill_skin",
                                       "raft_interface_thickness",
                                       "raft_interface_exposure",
                                       "pad_max_merge_distance"}) {
            INFO("setting " << key);
            CHECK(raft_type_uses_setting(RaftType::Full, key));
        }

        for (const std::string& key : {"pad_object_gap",
                                       "pad_around_object_everywhere",
                                       "pad_object_connector_stride",
                                       "pad_object_connector_width",
                                       "pad_object_connector_penetration"}) {
            INFO("setting " << key);
            CHECK_FALSE(raft_type_uses_setting(RaftType::Full, key));
        }
    }

    SECTION("an around object raft reads everything the user set")
    {
        for (const std::string& key : {"pad_wall_height",
                                       "pad_wall_thickness",
                                       "raft_floor_thickness",
                                       "pad_brim_size",
                                       "pad_wall_slope",
                                       "raft_edge_taper",
                                       "raft_infill",
                                       "raft_infill_spacing",
                                       "raft_infill_wall",
                                       "raft_infill_skin",
                                       "raft_interface_thickness",
                                       "raft_interface_exposure",
                                       "pad_max_merge_distance",
                                       "pad_object_gap",
                                       "pad_around_object_everywhere",
                                       "pad_object_connector_stride",
                                       "pad_object_connector_width",
                                       "pad_object_connector_penetration"}) {
            INFO("setting " << key);
            CHECK(raft_type_uses_setting(RaftType::AroundObject, key));
        }
    }

    SECTION("Skate replaces the expansion and the wall slope, so those two are hidden")
    {
        for (const std::string& key : {"pad_wall_height",
                                       "pad_wall_thickness",
                                       "raft_floor_thickness",
                                       "raft_edge_taper",
                                       "raft_infill",
                                       "raft_infill_spacing",
                                       "raft_infill_wall",
                                       "raft_infill_skin",
                                       "raft_interface_thickness",
                                       "raft_interface_exposure",
                                       "pad_max_merge_distance",
                                       "pad_object_gap",
                                       "pad_around_object_everywhere",
                                       "pad_object_connector_stride",
                                       "pad_object_connector_width",
                                       "pad_object_connector_penetration"}) {
            INFO("setting " << key);
            CHECK(raft_type_uses_setting(RaftType::Skate, key));
        }

        // SKATE_BRIM_FACTOR and SKATE_SLOPE_DEG in RaftPreset.cpp override these two.
        CHECK_FALSE(raft_type_uses_setting(RaftType::Skate, "pad_brim_size"));
        CHECK_FALSE(raft_type_uses_setting(RaftType::Skate, "pad_wall_slope"));
    }

    SECTION("Auto prints either nothing or a raft around the object, so it reads every knob")
    {
        // What the rule (M7.8.4) resolves to is one of the shapes above, never Auto itself, so the
        // rows shown are the union of theirs: everything Around object reads, which is also what a
        // full plate raft reads and a part that gets no raft at all ignores.
        for (const std::string& key : {"pad_wall_height",
                                       "pad_wall_thickness",
                                       "raft_floor_thickness",
                                       "pad_brim_size",
                                       "pad_wall_slope",
                                       "raft_edge_taper",
                                       "raft_infill",
                                       "raft_infill_spacing",
                                       "raft_infill_wall",
                                       "raft_infill_skin",
                                       "raft_interface_thickness",
                                       "raft_interface_exposure",
                                       "pad_max_merge_distance",
                                       "pad_object_gap",
                                       "pad_around_object_everywhere",
                                       "pad_object_connector_stride",
                                       "pad_object_connector_width",
                                       "pad_object_connector_penetration"}) {
            INFO("setting " << key);
            CHECK(raft_type_uses_setting(RaftType::Auto, key));
        }
        CHECK(raft_type_visible_settings(RaftType::Auto)
              == raft_type_visible_settings(RaftType::AroundObject));
    }

    SECTION("a setting that is not a raft knob is not filtered out")
    {
        CHECK_FALSE(raft_type_uses_setting(RaftType::None, "layer_height"));
        CHECK_FALSE(raft_type_uses_setting(RaftType::Skate, "support_pillar_diameter"));
        // The UI filter asks this first, so such a setting is never hidden.
        CHECK_FALSE(Slic3r::Domain::SLA::is_raft_setting("layer_height"));
        CHECK_FALSE(Slic3r::Domain::SLA::is_raft_setting("support_pillar_diameter"));
        CHECK(Slic3r::Domain::SLA::is_raft_setting("raft_type"));
        CHECK(Slic3r::Domain::SLA::is_raft_setting("pad_object_gap"));
        // The three knobs that shape the pattern are raft settings, so the filter looks at them,
        // even though a solid raft hides them.
        CHECK(Slic3r::Domain::SLA::is_raft_setting("raft_infill_spacing"));
        CHECK(Slic3r::Domain::SLA::is_raft_setting("raft_infill_wall"));
        CHECK(Slic3r::Domain::SLA::is_raft_setting("raft_infill_skin"));
    }
}

TEST_CASE("The raft infill pattern decides the three knobs that shape it", "[Config][SLA][Raft]")
{
    using Slic3r::Domain::sla::RaftInfillType;
    using Slic3r::Domain::SLA::raft_infill_uses_setting;
    using Slic3r::Domain::SLA::raft_infill_visible_settings;

    const std::vector<std::string> knobs{"raft_infill_spacing",
                                         "raft_infill_wall",
                                         "raft_infill_skin"};

    SECTION("a solid raft reads none of them")
    {
        for (const std::string& key : knobs) {
            INFO("setting " << key);
            CHECK_FALSE(raft_infill_uses_setting(RaftInfillType::None, key));
        }
        CHECK(raft_infill_visible_settings(RaftInfillType::None).empty());
    }

    SECTION("grid and honeycomb read all three")
    {
        for (const RaftInfillType infill : {RaftInfillType::Grid, RaftInfillType::Honeycomb}) {
            INFO("raft infill " << static_cast<int>(infill));
            for (const std::string& key : knobs) {
                INFO("setting " << key);
                CHECK(raft_infill_uses_setting(infill, key));
            }
            // The pattern is picked where it is shown, so it is not one of its own knobs.
            CHECK_FALSE(raft_infill_uses_setting(infill, "raft_infill"));
            CHECK_FALSE(raft_infill_uses_setting(infill, "layer_height"));
        }
    }
}

TEST_CASE("The raft type and the raft infill pattern together decide the raft rows",
          "[Config][SLA][Raft]")
{
    using Slic3r::Domain::sla::RaftInfillType;
    using Slic3r::Domain::sla::RaftType;
    using Slic3r::Domain::SLA::raft_uses_setting;
    using Slic3r::Domain::SLA::raft_visible_settings;

    const std::vector<RaftType> types{
        RaftType::Auto, RaftType::None, RaftType::Full, RaftType::AroundObject, RaftType::Skate
    };
    const std::vector<RaftInfillType> infills{
        RaftInfillType::None, RaftInfillType::Grid, RaftInfillType::Honeycomb
    };
    const std::vector<std::string> infill_knobs{
        "raft_infill_spacing", "raft_infill_wall", "raft_infill_skin"
    };

    for (const RaftType type : types) {
        for (const RaftInfillType infill : infills) {
            INFO("raft type " << static_cast<int>(type) << ", raft infill "
                             << static_cast<int>(infill));

            // The dropdowns are always there: without raft_type there is no raft to change and
            // without raft_infill there is no way to cut a pattern.
            CHECK(raft_uses_setting(type, infill, "raft_type"));
            CHECK(raft_visible_settings(type, infill).front() == "raft_type");

            // A solid raft prints no raft at all, so the edge taper and the whole infill block,
            // the pattern and its three knobs, change nothing.
            const bool raft_printed = type != RaftType::None;
            CHECK(raft_uses_setting(type, infill, "raft_edge_taper") == raft_printed);
            CHECK(raft_uses_setting(type, infill, "raft_infill") == raft_printed);

            // A printed raft with no pattern cuts no cells, so the three knobs are hidden. Grid
            // and honeycomb cut cells, so all three are shown.
            const bool pattern_cut = raft_printed && infill != RaftInfillType::None;
            for (const std::string& key : infill_knobs) {
                INFO("setting " << key);
                CHECK(raft_uses_setting(type, infill, key) == pattern_cut);
            }

            // The knobs that shape the raft itself follow the raft type, not the pattern.
            CHECK(raft_uses_setting(type, infill, "pad_wall_height") == raft_printed);
            CHECK(raft_uses_setting(type, infill, "pad_wall_thickness") == raft_printed);
            CHECK(raft_uses_setting(type, infill, "pad_max_merge_distance") == raft_printed);
            // Skate brings its own expansion and slope (SKATE_BRIM_FACTOR, SKATE_SLOPE_DEG), so
            // those two are read by every printed raft that is not a skate one. A raft None prints
            // no raft at all, so it reads neither, like every other raft knob above.
            const bool skate            = type == RaftType::Skate;
            const bool user_shaped_raft = raft_printed && !skate;
            CHECK(raft_uses_setting(type, infill, "pad_brim_size") == user_shaped_raft);
            CHECK(raft_uses_setting(type, infill, "pad_wall_slope") == user_shaped_raft);

            // Only a raft around the object reads the object gap and the connectors. Auto can
            // resolve to one of those (M7.8.4), so it reads them too.
            const bool around_object = type == RaftType::AroundObject || type == RaftType::Skate
                                       || type == RaftType::Auto;
            CHECK(raft_uses_setting(type, infill, "pad_object_gap") == around_object);
            CHECK(raft_uses_setting(type, infill, "pad_around_object_everywhere") == around_object);
            CHECK(raft_uses_setting(type, infill, "pad_object_connector_stride") == around_object);
            CHECK(raft_uses_setting(type, infill, "pad_object_connector_width") == around_object);
            CHECK(
                raft_uses_setting(type, infill, "pad_object_connector_penetration")
                == around_object
            );

            // A setting that is not a raft knob is never filtered.
            CHECK_FALSE(raft_uses_setting(type, infill, "layer_height"));
            CHECK_FALSE(raft_uses_setting(type, infill, "support_pillar_diameter"));

            // A raft None leaves the two dropdowns and nothing else.
            if (!raft_printed) {
                CHECK(raft_visible_settings(type, infill).size() == 1);
            }
        }
    }
}

TEST_CASE("The raft infill defaults are the named tuning constants", "[Config][SLA][Raft]")
{
    using Slic3r::Domain::sla::RaftInfillType;
    using Slic3r::Domain::SLA::RAFT_INFILL_SKIN_MM;
    using Slic3r::Domain::SLA::RAFT_INFILL_SPACING_MM;
    using Slic3r::Domain::SLA::RAFT_INFILL_WALL_MM;
    using Slic3r::Domain::SLA::RaftInfill;

    // The tuning pass M2.14b2b asks for: the cell size and the wall are named constants in
    // RaftPreset.hpp, so the values there and the ones ConfigDefsSLA.cpp puts into a built config
    // cannot drift apart. 2 mm cells with 0.4 mm of material between them and half a millimetre
    // of skin under the top face is the starting point, still to be validated against Lychee and
    // Chitubox.
    const Slic3r::Domain::SLAPrintSettings settings;
    CHECK(settings.items.find("raft_infill")->get<RaftInfillType>() == RaftInfillType::None);
    CHECK(settings.items.find("raft_infill_spacing")->get<double>() == RAFT_INFILL_SPACING_MM);
    CHECK(settings.items.find("raft_infill_wall")->get<double>() == RAFT_INFILL_WALL_MM);
    CHECK(settings.items.find("raft_infill_skin")->get<double>() == RAFT_INFILL_SKIN_MM);

    // The values the engine falls back to when a config has no raft_infill at all are the same.
    const RaftInfill fallback;
    CHECK(fallback.type == RaftInfillType::None);
    CHECK(fallback.spacing_mm == RAFT_INFILL_SPACING_MM);
    CHECK(fallback.wall_mm == RAFT_INFILL_WALL_MM);
    CHECK(fallback.skin_mm == RAFT_INFILL_SKIN_MM);
}

TEST_CASE("Every shown raft setting is a real setting in the Raft group", "[Config][SLA][Raft]")
{
    using Slic3r::Domain::sla::RaftInfillType;
    using Slic3r::Domain::sla::RaftType;
    using Slic3r::Domain::SLA::raft_visible_settings;

    const auto& defs = Slic3r::Domain::get_defs_sla();
    auto find_def = [&defs](const std::string& name) -> const ConfigItemDef* {
        for (const auto& def : defs.defs()) {
            if (def.name == name)
                return &def;
        }
        return nullptr;
    };

    for (const RaftType type : {RaftType::Auto,
                                RaftType::None,
                                RaftType::Full,
                                RaftType::AroundObject,
                                RaftType::Skate}) {
        for (const RaftInfillType infill :
             {RaftInfillType::None, RaftInfillType::Grid, RaftInfillType::Honeycomb}) {
            INFO("raft type " << static_cast<int>(type) << ", raft infill "
                             << static_cast<int>(infill));
            for (const std::string& key : raft_visible_settings(type, infill)) {
                INFO("setting " << key);
                const ConfigItemDef* def = find_def(key);
                REQUIRE(def != nullptr);
                // Nothing the raft type and the pattern can show may be hidden or live outside
                // the Raft group, or the knob would silently disappear for good.
                CHECK(def->category == ConfigItemDef::Category::Print_Pad);
                CHECK(def->option_group == ConfigItemDef::OptionGroup::Print_Pad_Pad);
            }
        }
    }
}

TEST_CASE("The Raft group is shown in the order the raft type lists it", "[Config][SLA][Raft]")
{
    using Slic3r::Domain::sla::RaftInfillType;
    using Slic3r::Domain::sla::RaftType;
    using Slic3r::Domain::SLA::raft_visible_settings;

    const auto& defs = Slic3r::Domain::get_defs_sla();
    auto find_def = [&defs](const std::string& name) -> const ConfigItemDef* {
        for (const auto& def : defs.defs()) {
            if (def.name == name)
                return &def;
        }
        return nullptr;
    };

    // The dialog sorts the rows of a group by ConfigItemDef::order, so the order values have to
    // agree with raft_visible_settings, which is the order the raft type was designed for. A
    // pattern that is cut gives the longest list, so all three patterns are checked.
    for (const RaftInfillType infill :
         {RaftInfillType::None, RaftInfillType::Grid, RaftInfillType::Honeycomb}) {
        const std::vector<std::string>& settings =
            raft_visible_settings(RaftType::AroundObject, infill);
        for (size_t i = 1; i < settings.size(); ++i) {
            INFO("raft infill " << static_cast<int>(infill) << ", after " << settings.at(i - 1)
                                << " comes " << settings.at(i));
            const ConfigItemDef* previous = find_def(settings.at(i - 1));
            const ConfigItemDef* current  = find_def(settings.at(i));
            REQUIRE(previous != nullptr);
            REQUIRE(current != nullptr);
            CHECK(previous->order < current->order);
        }
    }
}

TEST_CASE("SLA overridable settings have known option groups", "[Config][SLA][Override]")
{
    const auto& defs = Slic3r::Domain::get_defs_sla();

    for (const auto& def : defs.defs()) {
        // Only check settings that can be overridden and are not hidden
        if (!def.overrides_in.empty() &&
            def.category != ConfigItemDef::Category::Hidden) {
            INFO("Setting: " << def.name);
            CHECK(def.option_group != ConfigItemDef::OptionGroup::Unknown);
        }
    }
}

TEST_CASE("SLA settings that nothing reads are hidden, not deleted", "[Config][SLA][Hidden]")
{
    const auto& defs = Slic3r::Domain::get_defs_sla();
    auto find_def    = [&defs](const std::string& name) -> const ConfigItemDef*
    {
        for (const auto& def : defs.defs()) {
            if (def.name == name)
                return &def;
        }
        return nullptr;
    };

    // The evidence is in doc/sla-fork/sla-settings-evidence.md: the engine derives the peel time
    // from ExposureProfile and from the tilt times, and nothing clamps the exposure with the
    // min/max bounds, so none of these is read on an SLA path. Hidden, not deleted: old presets
    // and .3mf projects still carry the values.
    for (const std::string& key :
         {"min_exposure_time",
          "max_exposure_time",
          "min_initial_exposure_time",
          "max_initial_exposure_time",
          "material_source_note"})
    {
        INFO("setting " << key);
        const ConfigItemDef* def = find_def(key);
        REQUIRE(def != nullptr);
        CHECK(def->category == ConfigItemDef::Category::Hidden);
    }
}

TEST_CASE("SLA settings next to the hidden ones stay visible", "[Config][SLA][Hidden]")
{
    const auto& defs = Slic3r::Domain::get_defs_sla();
    auto find_def    = [&defs](const std::string& name) -> const ConfigItemDef*
    {
        for (const auto& def : defs.defs()) {
            if (def.name == name)
                return &def;
        }
        return nullptr;
    };

    // The layer-separation knobs were hidden by M1.13d3 because no exporter looked them up. They
    // are not: the .pwmx/.pm5 and .goo writers take the lift distance, the lift and retract speeds,
    // the waits around the moves and the light PWM from them, the bottom_* ones for the bottom
    // layers, so a user on a non-Prusa printer sets them. Hiding them again would take away a real
    // setting, so they are listed with the other settings the exporters read.
    for (const std::string& key :
         {"lift_height",
          "lift_height_2",
          "lift_speed",
          "lift_speed_2",
          "retract_speed",
          "retract_speed_2",
          "wait_before_lift",
          "wait_after_lift",
          "wait_after_retract",
          "light_pwm",
          "bottom_lift_height",
          "bottom_lift_height_2",
          "bottom_lift_speed",
          "bottom_lift_speed_2",
          "bottom_retract_speed",
          "bottom_retract_speed_2",
          "bottom_wait_before_lift",
          "bottom_wait_after_lift",
          "bottom_wait_after_retract",
          "bottom_light_pwm",
          "bottom_layer_count",
          "exposure_time",
          "initial_exposure_time",
          "faded_layers",
          "delay_before_exposure",
          "delay_after_exposure"})
    {
        INFO("setting " << key);
        const ConfigItemDef* def = find_def(key);
        REQUIRE(def != nullptr);
        CHECK(def->category != ConfigItemDef::Category::Hidden);
    }
}
