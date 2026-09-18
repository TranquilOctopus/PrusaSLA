#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/Scene/IGizmo.hpp"
#include "Slic3r/App/Plater/ToolGizmosUiInfo.hpp"
#include "Slic3r/Domain/PrinterTechnology.hpp"

using Slic3r::App::Plater::tool_name;
using Slic3r::App::Plater::tool_shortcut;
using Slic3r::App::Plater::tool_command_name;
using Slic3r::App::Plater::tool_icon;
using Slic3r::App::Plater::tool_key_code;
using Slic3r::App::Scene::ToolType;
using Slic3r::Domain::PrinterTechnology;

TEST_CASE("SlaSupportPoints and SlaHollow tool types exist in ToolType enum", "[ToolType]") {
    // Verify the new enum values exist
    REQUIRE(static_cast<int>(ToolType::SlaSupportPoints) > 0);
    REQUIRE(static_cast<int>(ToolType::SlaHollow) > 0);
    REQUIRE(ToolType::SlaSupportPoints != ToolType::SlaHollow);
}

TEST_CASE("SlaSupportPoints and SlaHollow have UI info entries", "[ToolGizmosUiInfo]") {
    // Verify tool_name returns non-empty strings
    REQUIRE(!tool_name(ToolType::SlaSupportPoints).empty());
    REQUIRE(!tool_name(ToolType::SlaHollow).empty());

    // Verify tool_shortcut returns non-empty strings
    REQUIRE(!tool_shortcut(ToolType::SlaSupportPoints).empty());
    REQUIRE(!tool_shortcut(ToolType::SlaHollow).empty());

    // Verify tool_command_name returns non-null strings
    REQUIRE(tool_command_name(ToolType::SlaSupportPoints) != nullptr);
    REQUIRE(tool_command_name(ToolType::SlaHollow) != nullptr);

    // Verify tool_icon returns valid icons (not Icon::None)
    REQUIRE(tool_icon(ToolType::SlaSupportPoints) != Slic3r::App::Render::Icon::None);
    REQUIRE(tool_icon(ToolType::SlaHollow) != Slic3r::App::Render::Icon::None);

    // Verify tool_key_code returns valid key codes
    REQUIRE(tool_key_code(ToolType::SlaSupportPoints) != Slic3r::App::Platform::KeyCode::None);
    REQUIRE(tool_key_code(ToolType::SlaHollow) != Slic3r::App::Platform::KeyCode::None);
}

TEST_CASE("SlaSupportPoints and SlaHollow have correct names", "[ToolGizmosUiInfo]") {
    REQUIRE(tool_name(ToolType::SlaSupportPoints) == "SLA Support Points");
    REQUIRE(tool_name(ToolType::SlaHollow) == "SLA Hollow");
}

TEST_CASE("SlaSupportPoints and SlaHollow have correct command names", "[ToolGizmosUiInfo]") {
    REQUIRE(std::string(tool_command_name(ToolType::SlaSupportPoints)) == "sla-support-points-gizmo");
    REQUIRE(std::string(tool_command_name(ToolType::SlaHollow)) == "sla-hollow-gizmo");
}

TEST_CASE("SlaSupportPoints and SlaHollow have correct shortcuts", "[ToolGizmosUiInfo]") {
    REQUIRE(tool_shortcut(ToolType::SlaSupportPoints) == "P");
    REQUIRE(tool_shortcut(ToolType::SlaHollow) == "H");
}

TEST_CASE("SlaSupportPoints and SlaHollow have correct icons", "[ToolGizmosUiInfo]") {
    REQUIRE(tool_icon(ToolType::SlaSupportPoints) == Slic3r::App::Render::Icon::Support);
    REQUIRE(tool_icon(ToolType::SlaHollow) == Slic3r::App::Render::Icon::Hollowing);
}

TEST_CASE("SlaSupportPoints and SlaHollow have correct key codes", "[ToolGizmosUiInfo]") {
    REQUIRE(tool_key_code(ToolType::SlaSupportPoints) == Slic3r::App::Platform::KeyCode::P);
    REQUIRE(tool_key_code(ToolType::SlaHollow) == Slic3r::App::Platform::KeyCode::H);
}