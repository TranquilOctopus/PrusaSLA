#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cstddef>
#include <memory>
#include <vector>

#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "Slic3r/Domain/PrinterTechnology.hpp"
#include "libslic3r/ConfigViews.hpp"
#include "libslic3r/SLALayerImage.hpp"
#include "libslic3r/SLALayersToMesh.hpp"
#include "libslic3r/libslic3r.h"

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

TEST_CASE("SLA layers to mesh - round trip without files", "[sla][layers_to_mesh]")
{
    constexpr size_t layer_count = 20;
    constexpr double first_layer_height = 0.05;
    constexpr double layer_height = 0.05;
    // A 10 x 10 mm square, somewhere in the middle of the display so that it does
    // not touch the edges of the raster.
    constexpr double lo = 20., hi = 30.;

    Domain::ConfigView printer_config = make_printer_config();

    Domain::ExPolygons slice;
    ExPolygon square;
    square.contour.points = {{scaled(lo), scaled(lo)},
                             {scaled(hi), scaled(lo)},
                             {scaled(hi), scaled(hi)},
                             {scaled(lo), scaled(hi)}};
    slice.push_back(square);

    SlaLayerImage img = render_sla_layer_image(slice, printer_config, 0, 0);
    REQUIRE(img.width > 0);
    REQUIRE(img.height > 0);

    LayersToMeshParams params;
    params.pixel_width_mm = img.pixel_width_mm;
    params.pixel_height_mm = img.pixel_height_mm;
    params.display_width_mm = printer_config.get<double>("display_width");
    params.display_height_mm = printer_config.get<double>("display_height");
    params.mirror_x = printer_config.get<bool>("display_mirror_x");
    params.mirror_y = printer_config.get<bool>("display_mirror_y");
    params.portrait = printer_config.get<Domain::SLADisplayOrientation>("display_orientation") ==
                       Domain::SLADisplayOrientation::sladoPortrait;
    params.first_layer_height_mm = first_layer_height;
    params.layer_height_mm = layer_height;
    params.threshold = 128;

    // The same slice for every layer.
    std::vector<GrayLayerImage> layers(layer_count);
    for (GrayLayerImage &layer : layers) {
        layer.width = img.width;
        layer.height = img.height;
        layer.pixels = img.pixels;
    }

    Domain::TriangleMesh mesh = layers_to_mesh(layers, params);
    REQUIRE_FALSE(mesh.empty());

    // The outlines are reconstructed on whole pixels, so a couple of pixels of
    // error is expected on the size of the mesh.
    double tol_x = 2 * params.pixel_width_mm;
    double tol_y = 2 * params.pixel_height_mm;

    Domain::BoundingBox3d bb = mesh.bounding_box();
    REQUIRE(bb.defined);
    REQUIRE((bb.max.x() - bb.min.x()) == Approx(hi - lo).margin(tol_x));
    REQUIRE((bb.max.y() - bb.min.y()) == Approx(hi - lo).margin(tol_y));
    REQUIRE(bb.min.z() == Approx(0.).margin(layer_height));
    REQUIRE(bb.max.z() == Approx(layer_count * layer_height).margin(layer_height));

    // The mesh has to be back where the square was drawn from.
    REQUIRE(bb.min.x() == Approx(lo).margin(tol_x));
    REQUIRE(bb.min.y() == Approx(lo).margin(tol_y));
}
