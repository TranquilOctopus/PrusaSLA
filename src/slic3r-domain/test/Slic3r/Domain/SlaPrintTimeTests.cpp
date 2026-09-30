#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/SLA/PrintTime.hpp"

#include <memory>
#include <string>
#include <vector>

using Slic3r::Domain::ConfigPackSLA;
using Slic3r::Domain::ConfigView;
using Slic3r::Domain::FullConfigSLA;
using Slic3r::Domain::Preset::HwPrinterConfig;
using Slic3r::Domain::PrinterTechnology;
using Slic3r::Domain::SlaLayerTime;
using Slic3r::Domain::SlaPrintTimeEstimate;
using Slic3r::Domain::sla_estimate_print_time;

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

// A three layer print: one burn-in layer, one transition layer, one normal layer. With no
// transition layers of its own the burn-in is that first layer alone and the fade has run out by
// it, so the second layer is already at the normal exposure. The exposures and the whole
// separation are material settings, the layer height and the transition layer count are print
// settings, as in ConfigDefsSLA.cpp. Filled in place, so the test never has to copy a config pack.
static void fill_three_layer_config(ConfigPackSLA& config_pack)
{
    auto& print    = config_pack.sla_print_settings.items;
    auto& material = config_pack.sla_material_settings.items;

    material.opt("exposure_time").set(6.);
    material.opt("initial_exposure_time").set(20.);
    material.opt("bottom_layer_count").set(1);
    print.opt("faded_layers").set(0);
    print.opt("layer_height").set(0.05);

    material.opt("wait_before_lift").set(3.);
    material.opt("wait_after_lift").set(0.4);
    material.opt("wait_after_retract").set(0.6);
    material.opt("bottom_wait_before_lift").set(4.);
    material.opt("bottom_wait_after_lift").set(0.7);
    material.opt("bottom_wait_after_retract").set(0.8);

    material.opt("lift_height").set(6.);
    material.opt("lift_speed").set(2.);
    material.opt("retract_speed").set(3.);
    material.opt("bottom_lift_height").set(9.);
    material.opt("bottom_lift_speed").set(1.75);
    material.opt("bottom_retract_speed").set(2.75);
}

TEST_CASE("An MSLA layer spends its time on the exposure, the waits and the separation", "[SlaPrintTime]")
{
    ConfigPackSLA config_pack;
    fill_three_layer_config(config_pack);

    const SlaPrintTimeEstimate estimate = sla_estimate_print_time(make_view(config_pack), 3);
    REQUIRE(estimate.valid);
    REQUIRE(estimate.layers.size() == 3);

    SECTION("the burn-in layer is exposed at the bottom exposure and moves on its own values")
    {
        const SlaLayerTime& layer = estimate.layers[0];
        REQUIRE(layer.exposure_s == Catch::Approx(20.));
        REQUIRE(layer.light_off_s == Catch::Approx(5.5)); // 4 + 0.7 + 0.8
        // 9 mm up at 1.75 mm/s and the same 9 mm back at 2.75 mm/s, with no second stage set.
        REQUIRE(layer.motion_s == Catch::Approx(9. / 1.75 + 9. / 2.75));
        REQUIRE(layer.total_s() == Catch::Approx(20. + 5.5 + 9. / 1.75 + 9. / 2.75));
    }

    SECTION("a normal layer is exposed at the normal exposure and moves on its own values")
    {
        const SlaLayerTime& layer = estimate.layers[2];
        REQUIRE(layer.exposure_s == Catch::Approx(6.));
        REQUIRE(layer.light_off_s == Catch::Approx(4.)); // 3 + 0.4 + 0.6
        REQUIRE(layer.motion_s == Catch::Approx(6. / 2. + 6. / 3.));
    }

    SECTION("the transition layers fade the exposure down to the normal one")
    {
        ConfigPackSLA ramp;
        fill_three_layer_config(ramp);
        ramp.sla_print_settings.items.opt("faded_layers").set(3);
        const SlaPrintTimeEstimate estimate_ramp = sla_estimate_print_time(make_view(ramp), 6);
        REQUIRE(estimate_ramp.valid);
        // The ramp is the 14 s between the two exposures spread over the transition layers plus
        // the first one, and it never goes below the normal exposure.
        REQUIRE(estimate_ramp.layers[1].exposure_s == Catch::Approx(20. - 14. / 4.));
        REQUIRE(estimate_ramp.layers[2].exposure_s == Catch::Approx(20. - 2. * 14. / 4.));
        REQUIRE(estimate_ramp.layers[3].exposure_s == Catch::Approx(20. - 3. * 14. / 4.));
        REQUIRE(estimate_ramp.layers[4].exposure_s == Catch::Approx(6.));
        REQUIRE(estimate_ramp.layers[5].exposure_s == Catch::Approx(6.));
    }

    SECTION("the total is the sum of the layers, and the running total follows it")
    {
        double expected = 0.;
        for (const SlaLayerTime& layer : estimate.layers)
            expected += layer.total_s();
        REQUIRE(estimate.total_s == Catch::Approx(expected));

        const std::vector<double> running = estimate.running_total_s();
        REQUIRE(running.size() == 3);
        REQUIRE(running[0] == Catch::Approx(estimate.layers[0].total_s()));
        REQUIRE(running[1] == Catch::Approx(estimate.layers[0].total_s() + estimate.layers[1].total_s()));
        REQUIRE(running[2] == Catch::Approx(estimate.total_s));
    }
}

TEST_CASE("The raft interface is exposed at its own exposure in the print time", "[SlaPrintTime]")
{
    ConfigPackSLA config_pack;
    auto& print    = config_pack.sla_print_settings.items;
    auto& material = config_pack.sla_material_settings.items;
    material.opt("exposure_time").set(6.);
    material.opt("initial_exposure_time").set(20.);
    material.opt("bottom_layer_count").set(1);
    print.opt("faded_layers").set(0);
    print.opt("layer_height").set(0.05);
    // A raft of 0.5 mm is 10 layers at 0.05 mm and the interface is the top 4 of them. The band is
    // cut back off the single burn-in layer, which it is nowhere near here.
    print.opt("pad_wall_thickness").set(0.5);
    print.opt("raft_interface_thickness").set(0.2);
    print.opt("raft_interface_exposure").set(12.);

    const SlaPrintTimeEstimate estimate = sla_estimate_print_time(make_view(config_pack), 10);
    REQUIRE(estimate.valid);
    REQUIRE(estimate.layers.size() == 10);
    REQUIRE(estimate.layers[0].exposure_s == Catch::Approx(20.));
    REQUIRE(estimate.layers[1].exposure_s == Catch::Approx(6.));
    REQUIRE(estimate.layers[5].exposure_s == Catch::Approx(6.));
    REQUIRE(estimate.layers[6].exposure_s == Catch::Approx(12.));
    REQUIRE(estimate.layers[9].exposure_s == Catch::Approx(12.));
    // No separation is set, so only the exposures differ: 15 s of bottom, 30 s of normal layers
    // and 48 s of interface.
    for (const SlaLayerTime& layer : estimate.layers) {
        REQUIRE(layer.light_off_s == Catch::Approx(0.));
        REQUIRE(layer.motion_s == Catch::Approx(0.));
    }
    REQUIRE(estimate.total_s == Catch::Approx(20. + 5. * 6. + 4. * 12.));
}

TEST_CASE("The two-stage lift adds a second move to every layer", "[SlaPrintTime]")
{
    ConfigPackSLA config_pack;
    auto& print    = config_pack.sla_print_settings.items;
    auto& material = config_pack.sla_material_settings.items;
    material.opt("exposure_time").set(6.);
    material.opt("initial_exposure_time").set(20.);
    material.opt("bottom_layer_count").set(1);
    print.opt("faded_layers").set(0);
    material.opt("lift_height").set(6.);
    material.opt("lift_speed").set(2.);
    material.opt("retract_speed").set(3.);
    material.opt("bottom_lift_height").set(9.);
    material.opt("bottom_lift_speed").set(1.75);
    material.opt("bottom_retract_speed").set(2.75);
    // The second stage of the lift and of the retract, which the *_2 settings hold.
    material.opt("lift_height_2").set(1.5);
    material.opt("lift_speed_2").set(1.);
    material.opt("retract_speed_2").set(1.5);
    material.opt("bottom_lift_height_2").set(3.);
    material.opt("bottom_lift_speed_2").set(0.5);
    material.opt("bottom_retract_speed_2").set(0.75);

    const SlaPrintTimeEstimate estimate = sla_estimate_print_time(make_view(config_pack), 2);
    REQUIRE(estimate.valid);
    // 6 mm and then 1.5 mm up, and the same two distances back down again.
    REQUIRE(estimate.layers[1].motion_s == Catch::Approx(6. / 2. + 6. / 3. + 1.5 / 1. + 1.5 / 1.5));
    // 9 mm and then 3 mm up, and the same back down, on the bottom values.
    REQUIRE(estimate.layers[0].motion_s
            == Catch::Approx(9. / 1.75 + 9. / 2.75 + 3. / 0.5 + 3. / 0.75));
}

TEST_CASE("A print with nothing to go on is not a print that takes no time", "[SlaPrintTime]")
{
    SECTION("no layers at all")
    {
        ConfigPackSLA config_pack;
        const SlaPrintTimeEstimate estimate = sla_estimate_print_time(make_view(config_pack), 0);
        REQUIRE_FALSE(estimate.valid);
        REQUIRE(estimate.total_s == 0.);
        REQUIRE(estimate.layers.empty());
        REQUIRE(estimate.running_total_s().empty());
    }

    SECTION("a negative layer count is the same state")
    {
        ConfigPackSLA config_pack;
        REQUIRE_FALSE(sla_estimate_print_time(make_view(config_pack), -1).valid);
    }

    SECTION("the defaults expose the layers and move nothing")
    {
        // exposure_time 10 s, initial_exposure_time 15 s, 10 transition layers, so the burn-in is
        // 11 layers and covers a print of four. No wait and no lift is set by default, so the
        // estimate is the exposures and no division by a zero speed happens.
        ConfigPackSLA config_pack;
        const SlaPrintTimeEstimate estimate = sla_estimate_print_time(make_view(config_pack), 4);
        REQUIRE(estimate.valid);
        REQUIRE(estimate.total_s == Catch::Approx(4. * 15.));
        for (const SlaLayerTime& layer : estimate.layers) {
            REQUIRE(layer.exposure_s == Catch::Approx(15.));
            REQUIRE(layer.light_off_s == Catch::Approx(0.));
            REQUIRE(layer.motion_s == Catch::Approx(0.));
        }
    }

    SECTION("a distance with no speed spends no time on that move")
    {
        ConfigPackSLA config_pack;
        config_pack.sla_material_settings.items.opt("exposure_time").set(6.);
        config_pack.sla_material_settings.items.opt("bottom_layer_count").set(0);
        config_pack.sla_print_settings.items.opt("faded_layers").set(0);
        config_pack.sla_material_settings.items.opt("lift_height").set(8.);
        // lift_speed and retract_speed are left at 0.
        const SlaPrintTimeEstimate estimate = sla_estimate_print_time(make_view(config_pack), 2);
        REQUIRE(estimate.valid);
        REQUIRE(estimate.layers[0].motion_s == Catch::Approx(0.));
    }
}
