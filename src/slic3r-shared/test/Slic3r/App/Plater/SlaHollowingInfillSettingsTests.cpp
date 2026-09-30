// The hollowing infill lattice in the UI (M2.29b). The pattern of the lattice decides which of the
// three keys the hollowing tool shows and which ones the object settings panel offers, and the
// combobox of the tool and the hollowing_infill key have to name the same patterns in the same
// order, or the tool would write a pattern the user did not pick.

#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/SlaHollowingInfillSettings.hpp"
#include "Slic3r/Domain/ConfigBoxesSLA.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/SLA/HollowingLatticeSettings.hpp"

#include <optional>
#include <string>
#include <vector>

using Slic3r::Domain::ConfigItemDef;
using Slic3r::Domain::SLAConfigLocation;
using Slic3r::Domain::sla::HollowingInfillType;
using Slic3r::Domain::SLA::hollowing_infill_uses_setting;
using Slic3r::Domain::SLA::hollowing_infill_visible_settings;
using Slic3r::Domain::SLA::is_hollowing_infill_setting;

namespace {

const ConfigItemDef* find_def(const std::string& name)
{
    const auto& defs = Slic3r::Domain::get_defs_sla();
    for (const ConfigItemDef& def : defs.defs()) {
        if (def.name == name) {
            return &def;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("The infill pattern decides which of the hollowing infill keys are shown", "[SlaHollowing][Hollowing][Infill]")
{
    SECTION("the pattern itself is shown whichever pattern it is")
    {
        for (const HollowingInfillType infill : {HollowingInfillType::None,
                                                 HollowingInfillType::Grid,
                                                 HollowingInfillType::Cubic}) {
            INFO("pattern " << static_cast<int>(infill));
            CHECK(hollowing_infill_uses_setting(infill, "hollowing_infill"));
            // The pattern is picked first and the knobs it decides come after it.
            CHECK(hollowing_infill_visible_settings(infill).front() == "hollowing_infill");
        }
    }

    SECTION("an empty cavity reads neither knob of a lattice")
    {
        // None is the cavity of today: a spacing and a strut would be read by nothing.
        CHECK_FALSE(hollowing_infill_uses_setting(HollowingInfillType::None, "hollowing_infill_spacing"));
        CHECK_FALSE(hollowing_infill_uses_setting(HollowingInfillType::None, "hollowing_infill_strut"));
        CHECK(hollowing_infill_visible_settings(HollowingInfillType::None).size() == 1);
    }

    SECTION("a patterned cavity reads both knobs")
    {
        CHECK(hollowing_infill_uses_setting(HollowingInfillType::Grid, "hollowing_infill_spacing"));
        CHECK(hollowing_infill_uses_setting(HollowingInfillType::Grid, "hollowing_infill_strut"));
        CHECK(hollowing_infill_uses_setting(HollowingInfillType::Cubic, "hollowing_infill_spacing"));
        CHECK(hollowing_infill_uses_setting(HollowingInfillType::Cubic, "hollowing_infill_strut"));
        CHECK(hollowing_infill_visible_settings(HollowingInfillType::Grid).size() == 3);
        CHECK(hollowing_infill_visible_settings(HollowingInfillType::Cubic).size() == 3);
    }

    SECTION("only the three keys of the lattice are hollowing infill settings")
    {
        for (const std::string& key : {"hollowing_infill",
                                       "hollowing_infill_spacing",
                                       "hollowing_infill_strut"}) {
            INFO("key " << key);
            CHECK(is_hollowing_infill_setting(key));
        }

        // The hollowing keys of the tool itself are shown whatever the pattern is.
        for (const std::string& key : {"hollowing_enable",
                                       "hollowing_min_thickness",
                                       "hollowing_quality",
                                       "hollowing_closing_distance"}) {
            INFO("key " << key);
            CHECK_FALSE(is_hollowing_infill_setting(key));
        }
    }
}

TEST_CASE("The lattice keys are object settings of the hollowing group", "[SlaHollowing][Hollowing][Infill]")
{
    // The three keys are overridable per object, which is what puts them in the object settings of
    // the hollowing tool, and they sit in the group that shows the wall thickness.
    const ConfigItemDef* thickness = find_def("hollowing_min_thickness");
    REQUIRE(thickness != nullptr);

    for (const std::string& key : {"hollowing_infill",
                                   "hollowing_infill_spacing",
                                   "hollowing_infill_strut"}) {
        INFO("key " << key);
        const ConfigItemDef* def = find_def(key);
        REQUIRE(def != nullptr);
        CHECK(def->overrides_in.contains(SLAConfigLocation::Object));
        CHECK(def->category == ConfigItemDef::Category::Print_Hollowing);
        CHECK(def->option_group == thickness->option_group);
    }
}

TEST_CASE("The combobox of the hollowing tool and the hollowing_infill key name the same patterns", "[SlaHollowing][Hollowing][Infill]")
{
    using Slic3r::App::hollowing_infill_index;
    using Slic3r::App::hollowing_infill_of_index;
    using Slic3r::App::hollowing_infill_patterns;

    SECTION("every pattern has an index and every index a pattern")
    {
        for (size_t index = 0; index < hollowing_infill_patterns().size(); ++index) {
            INFO("index " << index);
            const std::optional<HollowingInfillType> infill = hollowing_infill_of_index(static_cast<int>(index));
            REQUIRE(infill.has_value());
            CHECK(hollowing_infill_index(*infill) == static_cast<int>(index));
        }
    }

    SECTION("no pattern comes first, so a combobox that has no index yet is the plain cavity")
    {
        const std::optional<HollowingInfillType> infill = hollowing_infill_of_index(0);
        REQUIRE(infill.has_value());
        CHECK(*infill == HollowingInfillType::None);
    }

    SECTION("an index that is not a pattern is no pattern at all")
    {
        CHECK_FALSE(hollowing_infill_of_index(-1).has_value());
        CHECK_FALSE(hollowing_infill_of_index(3).has_value());
    }

    SECTION("a value that is not a pattern shows the plain cavity")
    {
        CHECK(hollowing_infill_index(static_cast<HollowingInfillType>(42)) == 0);
    }
}