#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/SidebarSlaPrintSettings.hpp"

using namespace Slic3r::App::SidebarSlaPrintSettingsFormat;

TEST_CASE("SidebarSlaPrintSettingsFormat::format_number", "[sidebar_sla_print_settings]")
{
    SECTION("value")
    {
        REQUIRE(format_number(0.05) == "0.05");
        REQUIRE(format_number(0.025) == "0.025");
        REQUIRE(format_number(2.0) == "2");
        REQUIRE(format_number(2.5) == "2.5");
        REQUIRE(format_number(30.0) == "30");
    }
    SECTION("float noise is rounded away")
    {
        REQUIRE(format_number(0.050000000745058059) == "0.05");
    }
}

TEST_CASE("SidebarSlaPrintSettingsFormat::format_summary", "[sidebar_sla_print_settings]")
{
    SECTION("resin, layer height and exposures")
    {
        REQUIRE(
            format_summary("Generic Resin", 0.05, 2.5, 30.0, "mm", "s")
            == "Generic Resin · 0.05 mm · 2.5 s / 30 s"
        );
    }
    SECTION("missing values are shown as a dash")
    {
        REQUIRE(
            format_summary("Generic Resin", std::nullopt, std::nullopt, 30.0, "mm", "s")
            == "Generic Resin · — · — / 30 s"
        );
    }
    SECTION("missing resin name is shown as a dash")
    {
        REQUIRE(format_summary({}, 0.05, 2.5, 30.0, "mm", "s") == "— · 0.05 mm · 2.5 s / 30 s");
    }
    SECTION("localized units are used")
    {
        REQUIRE(
            format_summary("Harz", 0.05, 2.5, 30.0, "Millimeter", "Sekunde")
            == "Harz · 0.05 Millimeter · 2.5 Sekunde / 30 Sekunde"
        );
    }
}
