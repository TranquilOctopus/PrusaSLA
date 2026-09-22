#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include "Slic3r/Domain/ConfigDefsSLA.hpp"

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
        CHECK(def->tooltip.find("controls the raft height") != std::string::npos);
    }

    // Check pad_enable (now "Use raft")
    {
        const ConfigItemDef* def = find_def("pad_enable");
        REQUIRE(def != nullptr);
        CHECK(def->category == ConfigItemDef::Category::Print_Pad);
        CHECK(def->option_group == ConfigItemDef::OptionGroup::Print_Pad_Pad);
        CHECK(def->label == "Use raft");
        CHECK(def->tooltip.find("automatically enabled") != std::string::npos);
    }

    // Check pad_wall_thickness (now "Raft wall thickness")
    {
        const ConfigItemDef* def = find_def("pad_wall_thickness");
        REQUIRE(def != nullptr);
        CHECK(def->category == ConfigItemDef::Category::Print_Pad);
        CHECK(def->option_group == ConfigItemDef::OptionGroup::Print_Pad_Pad);
        CHECK(def->label == "Raft wall thickness");
        CHECK(def->tooltip.find("set by the selected raft type") != std::string::npos);
    }

    // Check pad_wall_height (now "Raft height")
    {
        const ConfigItemDef* def = find_def("pad_wall_height");
        REQUIRE(def != nullptr);
        CHECK(def->category == ConfigItemDef::Category::Print_Pad);
        CHECK(def->option_group == ConfigItemDef::OptionGroup::Print_Pad_Pad);
        CHECK(def->label == "Raft height");
        CHECK(def->tooltip.find("set by the selected raft type") != std::string::npos);
    }

    // Check pad_brim_size (now "Raft expansion")
    {
        const ConfigItemDef* def = find_def("pad_brim_size");
        REQUIRE(def != nullptr);
        CHECK(def->category == ConfigItemDef::Category::Print_Pad);
        CHECK(def->option_group == ConfigItemDef::OptionGroup::Print_Pad_Pad);
        CHECK(def->label == "Raft expansion");
        CHECK(def->tooltip.find("set by the selected raft type") != std::string::npos);
        CHECK(def->tooltip.find("Skate uses half") != std::string::npos);
    }

    // Check pad_wall_slope (now "Raft slope")
    {
        const ConfigItemDef* def = find_def("pad_wall_slope");
        REQUIRE(def != nullptr);
        CHECK(def->category == ConfigItemDef::Category::Print_Pad);
        CHECK(def->option_group == ConfigItemDef::OptionGroup::Print_Pad_Pad);
        CHECK(def->label == "Raft slope");
        CHECK(def->tooltip.find("set by the selected raft type") != std::string::npos);
        CHECK(def->tooltip.find("Skate uses 70 degrees") != std::string::npos);
    }

    // Check pad_object_gap (now "Raft gap to object")
    {
        const ConfigItemDef* def = find_def("pad_object_gap");
        REQUIRE(def != nullptr);
        CHECK(def->category == ConfigItemDef::Category::Print_Pad);
        CHECK(def->option_group == ConfigItemDef::OptionGroup::Print_Pad_Pad);
        CHECK(def->label == "Raft gap to object");
        CHECK(def->tooltip.find("set by the selected raft type") != std::string::npos);
    }

    // Check pad_around_object (now "Raft around object")
    {
        const ConfigItemDef* def = find_def("pad_around_object");
        REQUIRE(def != nullptr);
        CHECK(def->category == ConfigItemDef::Category::Print_Pad);
        CHECK(def->option_group == ConfigItemDef::OptionGroup::Print_Pad_Pad);
        CHECK(def->label == "Raft around object");
        CHECK(def->tooltip.find("enabled automatically for Around Object and Skate") != std::string::npos);
    }

    // Check pad_around_object_everywhere (now "Raft around object everywhere")
    {
        const ConfigItemDef* def = find_def("pad_around_object_everywhere");
        REQUIRE(def != nullptr);
        CHECK(def->category == ConfigItemDef::Category::Print_Pad);
        CHECK(def->option_group == ConfigItemDef::OptionGroup::Print_Pad_Pad);
        CHECK(def->label == "Raft around object everywhere");
        CHECK(def->tooltip.find("overrides elevation-based logic") != std::string::npos);
    }

    // Verify pad settings NOT driven by raft_type keep "Pad" terminology
    {
        const ConfigItemDef* def = find_def("pad_max_merge_distance");
        REQUIRE(def != nullptr);
        CHECK(def->label == "Max merge distance");
        CHECK(def->tooltip.find("pads") != std::string::npos);
    }

    {
        const ConfigItemDef* def = find_def("pad_object_connector_stride");
        REQUIRE(def != nullptr);
        CHECK(def->label == "Pad object connector stride");
    }

    {
        const ConfigItemDef* def = find_def("pad_object_connector_width");
        REQUIRE(def != nullptr);
        CHECK(def->label == "Pad object connector width");
    }

    {
        const ConfigItemDef* def = find_def("pad_object_connector_penetration");
        REQUIRE(def != nullptr);
        CHECK(def->label == "Pad object connector penetration");
    }
}