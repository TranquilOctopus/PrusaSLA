#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/SidebarSlaSummary.hpp"

#include <limits>
#include <optional>

using namespace Slic3r::App::SidebarSlaSummaryFormat;

TEST_CASE("SidebarSlaSummaryFormat::format_resin_ml", "[sidebar_sla_summary]")
{
    SECTION("value")
    {
        REQUIRE(format_resin_ml(84.2) == "84.2 ml");
        REQUIRE(format_resin_ml(0.0) == "0.0 ml");
        REQUIRE(format_resin_ml(100.0) == "100.0 ml");
    }
    SECTION("nullopt")
    {
        REQUIRE(format_resin_ml(std::nullopt) == "—");
    }
}

TEST_CASE("SidebarSlaSummaryFormat::format_cost", "[sidebar_sla_summary]")
{
    SECTION("value")
    {
        REQUIRE(format_cost(6.10) == "6.10");
        REQUIRE(format_cost(0.0) == "0.00");
        REQUIRE(format_cost(123.456) == "123.46");
    }
    SECTION("nullopt")
    {
        REQUIRE(format_cost(std::nullopt) == "—");
    }
}

TEST_CASE("SidebarSlaSummaryFormat::format_bottles", "[sidebar_sla_summary]")
{
    SECTION("value")
    {
        REQUIRE(format_bottles(0.08) == "0.08");
        REQUIRE(format_bottles(0.0) == "0.00");
        REQUIRE(format_bottles(1.5) == "1.50");
    }
    SECTION("nullopt")
    {
        REQUIRE(format_bottles(std::nullopt) == "—");
    }
}

TEST_CASE("SidebarSlaSummaryFormat::format_layers", "[sidebar_sla_summary]")
{
    SECTION("value")
    {
        REQUIRE(format_layers(1024) == "1024");
        REQUIRE(format_layers(0) == "0");
        REQUIRE(format_layers(1) == "1");
    }
    SECTION("nullopt")
    {
        REQUIRE(format_layers(std::nullopt) == "—");
    }
}

TEST_CASE("SidebarSlaSummaryFormat::format_print_time", "[sidebar_sla_summary]")
{
    SECTION("value")
    {
        // Seconds are dropped, the minutes of a print that is 5 h 3 min and 59 s in are 3.
        REQUIRE(format_print_time(0.0) == "0:00");
        REQUIRE(format_print_time(59.0) == "0:00");
        REQUIRE(format_print_time(60.0) == "0:01");
        REQUIRE(format_print_time(183.0) == "0:03");
        REQUIRE(format_print_time(600.0) == "0:10");
        // The minutes always carry two digits, the hours do not have to.
        REQUIRE(format_print_time(5. * 3600. + 65.) == "5:01");
        REQUIRE(format_print_time(9.5 * 3600.) == "9:30");
        REQUIRE(format_print_time(36. * 3600.) == "36:00");
    }
    SECTION("a time that is not a time is the dash")
    {
        REQUIRE(format_print_time(-1.0) == "—");
        REQUIRE(format_print_time(std::numeric_limits<double>::quiet_NaN()) == "—");
        REQUIRE(format_print_time(std::numeric_limits<double>::infinity()) == "—");
    }
    SECTION("nullopt")
    {
        REQUIRE(format_print_time(std::nullopt) == "—");
    }
}