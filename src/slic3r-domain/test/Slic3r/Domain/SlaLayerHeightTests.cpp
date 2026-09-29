#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/SlaLayerHeight.hpp"

using Slic3r::Domain::ConfigPackSLA;
using Slic3r::Domain::ConfigView;
using Slic3r::Domain::FullConfigSLA;
using Slic3r::Domain::Preset::HwPrinterConfig;
using Slic3r::Domain::PrinterTechnology;

namespace {

ConfigView make_view(const ConfigPackSLA& config_pack)
{
    const HwPrinterConfig hw_config{.technology = PrinterTechnology::SLA};
    auto full_config = std::make_shared<const FullConfigSLA>(config_pack, hw_config);
    ConfigView config_view{full_config, {}};
    // Until finalize() runs, values() is empty and every lookup misses.
    config_view.finalize();
    return config_view;
}

} // namespace

TEST_CASE("sla_effective_layer_height falls back to the print layer height", "[SlaLayerHeight]")
{
    SECTION("a resin without a layer height")
    {
        ConfigPackSLA config_pack;
        config_pack.sla_print_settings.items.opt("layer_height").set(0.05);
        // The unset resin value is 0.
        const ConfigView config_view = make_view(config_pack);
        REQUIRE(Slic3r::Domain::sla_effective_layer_height(config_view) == Catch::Approx(0.05));
    }

    SECTION("a resin with its own layer height wins")
    {
        ConfigPackSLA config_pack;
        config_pack.sla_print_settings.items.opt("layer_height").set(0.05);
        config_pack.sla_material_settings.items.opt("resin_layer_height").set(0.03);
        const ConfigView config_view = make_view(config_pack);
        REQUIRE(Slic3r::Domain::sla_effective_layer_height(config_view) == Catch::Approx(0.03));
    }
}

TEST_CASE("sla_effective_faded_layers falls back to the print faded layers", "[SlaLayerHeight]")
{
    SECTION("a resin without a transition layer count")
    {
        ConfigPackSLA config_pack;
        config_pack.sla_print_settings.items.opt("faded_layers").set(8);
        // The unset resin value is -1.
        const ConfigView config_view = make_view(config_pack);
        REQUIRE(Slic3r::Domain::sla_effective_faded_layers(config_view) == 8);
    }

    SECTION("a resin with its own transition layer count wins")
    {
        ConfigPackSLA config_pack;
        config_pack.sla_print_settings.items.opt("faded_layers").set(8);
        config_pack.sla_material_settings.items.opt("resin_faded_layers").set(0);
        const ConfigView config_view = make_view(config_pack);
        REQUIRE(Slic3r::Domain::sla_effective_faded_layers(config_view) == 0);
    }
}
