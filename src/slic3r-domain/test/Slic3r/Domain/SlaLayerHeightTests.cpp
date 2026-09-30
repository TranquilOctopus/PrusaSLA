#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/SlaLayerHeight.hpp"

#include <memory>
#include <string>

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

TEST_CASE("The raft interface is the top of the raft", "[SlaLayerHeight][Raft]")
{
    using Slic3r::Domain::raft_interface_band;

    SECTION("an interface of no thickness is no band at all")
    {
        const auto band = raft_interface_band(2., 0., 0.05);
        REQUIRE(band.empty());
        REQUIRE(band.count() == 0);
        REQUIRE(band.last_layer == -1);
    }

    SECTION("the band is the last layers of the raft")
    {
        // A 2 mm raft at 0.05 mm is 40 layers, an interface of 0.5 mm is the top 10 of them.
        const auto band = raft_interface_band(2., 0.5, 0.05);
        REQUIRE(band.count() == 10);
        REQUIRE(band.first_layer == 30);
        REQUIRE(band.last_layer == 39);
        REQUIRE(band.contains(30));
        REQUIRE(band.contains(39));
        REQUIRE_FALSE(band.contains(29));
        REQUIRE_FALSE(band.contains(40));
    }

    SECTION("a raft that ends on a layer boundary is not one layer taller")
    {
        REQUIRE(raft_interface_band(2., 0.5, 0.1).last_layer == 19);
        REQUIRE(raft_interface_band(1.5, 0.3, 0.05).first_layer == 24);
        // A raft a hair over a layer boundary is a whole layer taller.
        REQUIRE(raft_interface_band(2.01, 0.5, 0.05).last_layer == 40);
    }

    SECTION("an interface thicker than the raft is the whole raft")
    {
        const auto band = raft_interface_band(2., 5., 0.05);
        REQUIRE(band.count() == 40);
        REQUIRE(band.first_layer == 0);
        REQUIRE(band.last_layer == 39);
    }

    SECTION("a positive thickness thinner than a layer is still one layer")
    {
        const auto band = raft_interface_band(2., 0.02, 0.05);
        REQUIRE(band.count() == 1);
        REQUIRE(band.last_layer == 39);
    }

    SECTION("no raft, no layer or no interface means no band")
    {
        REQUIRE(raft_interface_band(0., 0.5, 0.05).empty());
        REQUIRE(raft_interface_band(2., 0.5, 0.).empty());
        REQUIRE(raft_interface_band(-1., 0.5, 0.05).empty());
    }

    SECTION("only the band is exposed with the interface exposure")
    {
        auto band = raft_interface_band(2., 0.5, 0.05);
        band.exposure_s = 12.;
        REQUIRE(band.layer_exposure_s(30, 6.) == Catch::Approx(12.));
        REQUIRE(band.layer_exposure_s(39, 6.) == Catch::Approx(12.));
        REQUIRE(band.layer_exposure_s(29, 6.) == Catch::Approx(6.));
        REQUIRE(band.layer_exposure_s(40, 6.) == Catch::Approx(6.));
        // The print time the formats write counts the difference the band makes.
        REQUIRE(band.print_time_delta_s(6.) == Catch::Approx(60.));
    }

    SECTION("an interface exposure of zero exposes the band like the rest of the print")
    {
        auto band = raft_interface_band(2., 0.5, 0.05);
        REQUIRE(band.layer_exposure_s(30, 6.) == Catch::Approx(6.));
        REQUIRE(band.print_time_delta_s(6.) == Catch::Approx(0.));
    }
}

TEST_CASE("The raft interface of a config is its band of layers", "[SlaLayerHeight][Raft]")
{
    using Slic3r::Domain::sla_raft_interface;

    SECTION("a config with the interface at its defaults has no interface")
    {
        ConfigPackSLA config_pack;
        const ConfigView config_view = make_view(config_pack);
        const auto band = sla_raft_interface(config_view, 100);
        REQUIRE(band.empty());
    }

    SECTION("the interface is the top of the raft the config describes")
    {
        ConfigPackSLA config_pack;
        // The raft is its wall thickness plus the height of its cavity, here 2 + 1 mm.
        config_pack.sla_print_settings.items.opt("pad_wall_thickness").set(2.);
        config_pack.sla_print_settings.items.opt("pad_wall_height").set(1.);
        config_pack.sla_print_settings.items.opt("layer_height").set(0.05);
        config_pack.sla_print_settings.items.opt("faded_layers").set(0);
        config_pack.sla_print_settings.items.opt("raft_interface_thickness").set(1.5);
        config_pack.sla_print_settings.items.opt("raft_interface_exposure").set(15.);
        const ConfigView config_view = make_view(config_pack);

        // 3 mm of raft at 0.05 mm is 60 layers, the top 30 of which are the interface. The engine
        // fades the exposure over the transition layers plus the first one, so the first layer
        // alone is a bottom layer here, and the band is well above it.
        const auto band = sla_raft_interface(config_view, 100);
        REQUIRE(band.count() == 30);
        REQUIRE(band.first_layer == 30);
        REQUIRE(band.last_layer == 59);
        REQUIRE(band.exposure_s == Catch::Approx(15.));
        REQUIRE(band.layer_exposure_s(30, 6.) == Catch::Approx(15.));
        REQUIRE(band.layer_exposure_s(59, 6.) == Catch::Approx(15.));
        REQUIRE(band.layer_exposure_s(0, 6.) == Catch::Approx(6.));
        REQUIRE(band.layer_exposure_s(60, 6.) == Catch::Approx(6.));
    }

    SECTION("a band that reaches nowhere but the bottom layers is no band")
    {
        ConfigPackSLA config_pack;
        config_pack.sla_print_settings.items.opt("pad_wall_thickness").set(0.2);
        config_pack.sla_print_settings.items.opt("layer_height").set(0.05);
        config_pack.sla_print_settings.items.opt("faded_layers").set(10);
        config_pack.sla_print_settings.items.opt("raft_interface_thickness").set(0.2);
        config_pack.sla_print_settings.items.opt("raft_interface_exposure").set(15.);
        const ConfigView config_view = make_view(config_pack);

        // 0.2 mm of raft is 4 layers and the burn-in is 11, so every interface layer is a bottom
        // layer and the bottom exposure wins over the interface one.
        REQUIRE(sla_raft_interface(config_view, 100).empty());
    }

    SECTION("a print shorter than the band is cut to the layers it has")
    {
        ConfigPackSLA config_pack;
        config_pack.sla_print_settings.items.opt("pad_wall_thickness").set(2.);
        config_pack.sla_print_settings.items.opt("layer_height").set(0.05);
        config_pack.sla_print_settings.items.opt("faded_layers").set(0);
        config_pack.sla_print_settings.items.opt("raft_interface_thickness").set(2.);
        config_pack.sla_print_settings.items.opt("raft_interface_exposure").set(15.);
        const ConfigView config_view = make_view(config_pack);

        const auto band = sla_raft_interface(config_view, 20);
        REQUIRE(band.count() == 19);
        REQUIRE(band.first_layer == 1);
        REQUIRE(band.last_layer == 19);
        REQUIRE_FALSE(band.contains(20));
    }
}
