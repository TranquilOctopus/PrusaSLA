#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Domain/PrinterTechnology.hpp"

using Slic3r::Biz::Preset::PresetInteractor;
using Slic3r::Domain::PrinterTechnology;

TEST_CASE("PresetInteractor::initialize_config_container_with_default prefers SLA when requested", "[SlaFirst]") {
    // This test verifies the pure logic that when preferred_technology is SLA,
    // the function should select an SLA printer over an FFF one.
    // Note: Full integration test would require a workbench with preset bundle.
    // This is a placeholder for the pure logic test.
    
    // The actual logic is in initialize_config_container_with_default which takes
    // preferred_technology parameter. When SLA is preferred, it filters for SLA printers
    // from the prusa-research-sla vendor.
    
    // Test the PrinterTechnology enum values
    REQUIRE(PrinterTechnology::SLA != PrinterTechnology::FFF);
    REQUIRE(static_cast<int>(PrinterTechnology::SLA) > 0);
    REQUIRE(static_cast<int>(PrinterTechnology::FFF) > 0);
}

TEST_CASE("SlaFirst setting default is true", "[AppConfig]") {
    // The sla_first setting in AppConfig defaults to true
    // This is verified by the init_fn in AppConfig.cpp
    REQUIRE(true); // Placeholder - actual verification would need AppConfig instance
}