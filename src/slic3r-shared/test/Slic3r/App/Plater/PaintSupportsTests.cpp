// M2.30b: the FFF paint-on-supports tool is the SLA support paint tool as well. What the UI cannot
// decide on its own is in PaintSupportsRules: which technology the tool is offered for, what its two
// brushes are called, and which of its rows would need a slice and may therefore not be shown for
// SLA.
#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/Plater/PaintSupportsRules.hpp"
#include "Slic3r/App/Plater/ToolGizmosUiInfo.hpp"
#include "Slic3r/App/Scene/IGizmo.hpp"
#include "Slic3r/Domain/PrinterTechnology.hpp"

#include <string>

using Slic3r::App::Plater::PaintSupportsStrings;
using Slic3r::App::Plater::is_tool_visible_for_technology;
using Slic3r::App::Plater::paint_supports_automatic_painting_visible;
using Slic3r::App::Plater::paint_supports_strings;
using Slic3r::App::Plater::tool_name;
using Slic3r::App::Scene::ToolType;
using Slic3r::Domain::PrinterTechnology;

TEST_CASE("The support paint tool is offered for SLA and for FFF", "[PaintSupports]")
{
    SECTION("both technologies get the support paint tool")
    {
        REQUIRE(is_tool_visible_for_technology(ToolType::PaintOnSupportsGizmo, PrinterTechnology::SLA));
        REQUIRE(is_tool_visible_for_technology(ToolType::PaintOnSupportsGizmo, PrinterTechnology::FFF));
    }

    SECTION("the paint tools of the other features stay FFF only")
    {
        REQUIRE_FALSE(is_tool_visible_for_technology(ToolType::PaintOnSeamsGizmo, PrinterTechnology::SLA));
        REQUIRE_FALSE(is_tool_visible_for_technology(ToolType::PaintOnFuzzySkinGizmo, PrinterTechnology::SLA));
        REQUIRE_FALSE(is_tool_visible_for_technology(ToolType::MultiMaterialPaintingGizmo, PrinterTechnology::SLA));
        REQUIRE_FALSE(is_tool_visible_for_technology(ToolType::VariableLayerHeightGizmo, PrinterTechnology::SLA));

        REQUIRE(is_tool_visible_for_technology(ToolType::PaintOnSeamsGizmo, PrinterTechnology::FFF));
        REQUIRE(is_tool_visible_for_technology(ToolType::PaintOnFuzzySkinGizmo, PrinterTechnology::FFF));
        REQUIRE(is_tool_visible_for_technology(ToolType::MultiMaterialPaintingGizmo, PrinterTechnology::FFF));
        REQUIRE(is_tool_visible_for_technology(ToolType::VariableLayerHeightGizmo, PrinterTechnology::FFF));
    }

    SECTION("the tool keeps its name, so the toolbar item and the L shortcut are the same one")
    {
        REQUIRE(!tool_name(ToolType::PaintOnSupportsGizmo).empty());
    }
}

TEST_CASE("The brushes of the SLA paint tool are named after the supports", "[PaintSupports]")
{
    const PaintSupportsStrings fff = paint_supports_strings(PrinterTechnology::FFF);
    const PaintSupportsStrings sla = paint_supports_strings(PrinterTechnology::SLA);

    SECTION("the FFF wording is the wording of the FFF tool")
    {
        REQUIRE(fff.paint == "Paint");
        REQUIRE(fff.block == "Block");
        REQUIRE(fff.remove == "Remove");
        // Nothing to add to the FFF brushes, so no hint is shown there.
        REQUIRE(fff.block_hint.empty());
    }

    SECTION("the SLA wording says what is painted")
    {
        REQUIRE(sla.paint == "Paint supports");
        REQUIRE(sla.block == "Block supports");
        REQUIRE_FALSE(sla.remove.empty());
        REQUIRE_FALSE(sla.block_hint.empty());
    }

    SECTION("the block brush of the SLA tool states the island rule")
    {
        // A blocked region takes the automatic points of its overhang away, an island keeps its point
        // (the rule of the generator, tested in tests/sla_print/sla_support_facet_paint_tests.cpp).
        // The user has to read that where the brush is named.
        REQUIRE(sla.block_hint.find("island") != std::string::npos);
        REQUIRE(sla.block_hint.find("Auto support") != std::string::npos);
    }
}

TEST_CASE("The automatic painting row is FFF only, it needs a slice", "[PaintSupports]")
{
    // The button paints the spots of the FFF support search, which is a slice of the bed up to that
    // step. Only the Slice button may slice an SLA print.
    REQUIRE(paint_supports_automatic_painting_visible(PrinterTechnology::FFF));
    REQUIRE_FALSE(paint_supports_automatic_painting_visible(PrinterTechnology::SLA));
}