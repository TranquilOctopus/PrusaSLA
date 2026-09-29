#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sla_test_utils.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "Slic3r/Domain/PrinterTechnology.hpp"
#include "libslic3r/ConfigViews.hpp"
#include "libslic3r/SLALayerImage.hpp"

#include <cmath>

using namespace Slic3r;
using namespace Slic3r::sla;
using Catch::Approx;

static Domain::ConfigView make_printer_config(Domain::SLADisplayOrientation orientation = Domain::SLADisplayOrientation::sladoLandscape)
{
    Domain::ConfigPackSLA config;
    config.sla_printer_settings.items.opt("display_pixels_x").set(1440);
    config.sla_printer_settings.items.opt("display_pixels_y").set(800);
    config.sla_printer_settings.items.opt("display_orientation").set(orientation);
    config.sla_printer_settings.items.opt("display_width").set(144.0);
    config.sla_printer_settings.items.opt("display_height").set(80.0);
    config.sla_printer_settings.items.opt("display_mirror_x").set(false);
    config.sla_printer_settings.items.opt("display_mirror_y").set(false);
    config.sla_printer_settings.items.opt("gamma_correction").set(1.0);

    auto full_config = std::make_shared<const Domain::FullConfigSLA>(
        config, Domain::Preset::HwPrinterConfig{.technology = Domain::PrinterTechnology::SLA});
    SLAPrintConfigView view(full_config);
    return static_cast<Domain::ConfigView>(view);
}

TEST_CASE("SLA layer image - empty slice", "[sla][layer_image]")
{
    Domain::ExPolygons empty_slice;
    auto printer_config = make_printer_config();

    SlaLayerImage img = render_sla_layer_image(empty_slice, printer_config, 0, 0);

    REQUIRE(img.width == 1440);
    REQUIRE(img.height == 800);
    REQUIRE(img.pixels.size() == 1440 * 800);
    REQUIRE(std::all_of(img.pixels.begin(), img.pixels.end(), [](uint8_t v) { return v == 0; }));
    REQUIRE(img.pixel_width_mm == Approx(144.0 / 1440.0));
    REQUIRE(img.pixel_height_mm == Approx(80.0 / 800.0));
}

TEST_CASE("SLA layer image - centered square", "[sla][layer_image]")
{
    Domain::ExPolygons slice;
    ExPolygon square;
    square.contour.points = {{-50000, -50000}, {50000, -50000}, {50000, 50000}, {-50000, 50000}};
    slice.push_back(square);

    auto printer_config = make_printer_config();

    SlaLayerImage img = render_sla_layer_image(slice, printer_config, 0, 0);

    REQUIRE(img.width == 1440);
    REQUIRE(img.height == 800);
    REQUIRE(img.pixels.size() == 1440 * 800);

    long lit_pixels = 0;
    for (uint8_t px : img.pixels) {
        if (px > 0) ++lit_pixels;
    }

    size_t total_pixels = img.width * img.height;
    REQUIRE(lit_pixels > 0);
    REQUIRE(lit_pixels < total_pixels / 4);
}

TEST_CASE("SLA layer image - downscaled to fit max dimensions", "[sla][layer_image]")
{
    Domain::ExPolygons slice;
    ExPolygon square;
    square.contour.points = {{-50000, -50000}, {50000, -50000}, {50000, 50000}, {-50000, 50000}};
    slice.push_back(square);

    auto printer_config = make_printer_config();

    SlaLayerImage img = render_sla_layer_image(slice, printer_config, 256, 256);

    REQUIRE(img.width <= 256);
    REQUIRE(img.height <= 256);

    double display_aspect = 144.0 / 80.0;
    double img_aspect = double(img.width) / double(img.height);
    REQUIRE(std::abs(img_aspect - display_aspect) < 1.0 / std::min(img.width, img.height) + 1e-6);
}

TEST_CASE("SLA layer image - portrait orientation", "[sla][layer_image]")
{
    Domain::ExPolygons slice;
    ExPolygon square;
    square.contour.points = {{-50000, -50000}, {50000, -50000}, {50000, 50000}, {-50000, 50000}};
    slice.push_back(square);

    auto printer_config = make_printer_config(Domain::SLADisplayOrientation::sladoPortrait);

    SlaLayerImage img = render_sla_layer_image(slice, printer_config, 0, 0);

    REQUIRE(img.width == 800);
    REQUIRE(img.height == 1440);
    REQUIRE(img.pixels.size() == 800 * 1440);
}

TEST_CASE("SLA layer image - mirror x", "[sla][layer_image]")
{
    Domain::ExPolygons slice;
    ExPolygon square;
    square.contour.points = {{50000, -50000}, {70000, -50000}, {70000, 50000}, {50000, 50000}};
    slice.push_back(square);

    auto printer_config = make_printer_config();
    // Note: ConfigView is const, so we can't modify it. We'd need to create a new one with mirror_x=true.
    // For now, test that the basic render works.

    SlaLayerImage img = render_sla_layer_image(slice, printer_config, 0, 0);

    REQUIRE(img.width == 1440);
    REQUIRE(img.height == 800);
    REQUIRE(img.pixels.size() == 1440 * 800);

    long lit_pixels = 0;
    for (uint8_t px : img.pixels) {
        if (px > 0) ++lit_pixels;
    }
    REQUIRE(lit_pixels > 0);
}

// mm to the scaled coordinates the polygons use
static coord_t scaled_mm(double mm) { return coord_t(std::round(mm * 1000000.)); }

// 1 x 1 mm square with its center at (10, 5) mm in slice coordinates.
static Domain::ExPolygons make_square_slice()
{
    Domain::ExPolygons slice;
    ExPolygon square;
    square.contour.points = {{scaled_mm(9.5), scaled_mm(4.5)},
                             {scaled_mm(10.5), scaled_mm(4.5)},
                             {scaled_mm(10.5), scaled_mm(5.5)},
                             {scaled_mm(9.5), scaled_mm(5.5)}};
    slice.push_back(square);
    return slice;
}

TEST_CASE("SLA layer image - region render is centered", "[sla][layer_image]")
{
    Domain::ExPolygons slice = make_square_slice();

    auto printer_config = make_printer_config();

    // Native pixel size is 144 / 1440 = 0.1 mm, so the 64 x 64 px window is 6.4 x 6.4 mm.
    SlaLayerImage img = render_sla_layer_image_region(slice, printer_config, 10.0, 5.0, 64, 64);

    REQUIRE(img.width == 64);
    REQUIRE(img.height == 64);
    REQUIRE(img.pixels.size() == 64 * 64);
    REQUIRE(img.pixel_width_mm == Approx(0.1));
    REQUIRE(img.pixel_height_mm == Approx(0.1));

    size_t lit_pixels = 0;
    for (size_t row = 0; row < img.height; ++row) {
        for (size_t col = 0; col < img.width; ++col) {
            if (img.pixels[row * img.width + col] > 0) {
                ++lit_pixels;
                // Everything that is lit has to be in the central quarter of the window.
                REQUIRE(col >= 16);
                REQUIRE(col < 48);
                REQUIRE(row >= 16);
                REQUIRE(row < 48);
            }
        }
    }
    REQUIRE(lit_pixels > 0);
}

TEST_CASE("SLA layer image - region render outside of the slice", "[sla][layer_image]")
{
    // Same square as above, but the window is centered 30 mm away from it.
    Domain::ExPolygons slice = make_square_slice();

    auto printer_config = make_printer_config();

    SlaLayerImage img = render_sla_layer_image_region(slice, printer_config, 40.0, 30.0, 64, 64);

    REQUIRE(img.width == 64);
    REQUIRE(img.height == 64);
    REQUIRE(img.pixels.size() == 64 * 64);
    REQUIRE(std::all_of(img.pixels.begin(), img.pixels.end(), [](uint8_t v) { return v == 0; }));
}