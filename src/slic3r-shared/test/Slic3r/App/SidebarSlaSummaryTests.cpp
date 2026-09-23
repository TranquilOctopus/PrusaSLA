#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/SidebarSlaSummary.hpp"

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