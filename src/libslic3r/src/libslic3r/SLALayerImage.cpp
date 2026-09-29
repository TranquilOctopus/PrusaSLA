#include <libslic3r/SLALayerImage.hpp>
#include <libslic3r/SLA/RasterBase.hpp>
#include <libslic3r/SLA/AGGRaster.hpp>
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include <libslic3r/ExPolygon.hpp>
#include <libslic3r/ConfigViews.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace Slic3r::sla {

namespace {

// Display geometry of the printer, already resolved for the display orientation.
struct DisplayGeometry
{
    double w_mm = 0.;          // display size along the image x axis
    double h_mm = 0.;          // display size along the image y axis
    size_t px_x = 0;           // display pixels along the image x axis
    size_t px_y = 0;           // display pixels along the image y axis
    double gamma = 0.;
    RasterBase::Trafo trafo;
};

DisplayGeometry get_display_geometry(const Domain::ConfigView& printer_config)
{
    DisplayGeometry g;

    double w = printer_config.get<double>("display_width");
    double h = printer_config.get<double>("display_height");
    g.px_x = size_t(printer_config.get<int>("display_pixels_x"));
    g.px_y = size_t(printer_config.get<int>("display_pixels_y"));

    std::array<bool, 2> mirror;
    mirror[0] = printer_config.get<bool>("display_mirror_x");
    mirror[1] = printer_config.get<bool>("display_mirror_y");

    auto ro = printer_config.get<Domain::SLADisplayOrientation>("display_orientation");
    RasterBase::Orientation orientation = ro == Domain::SLADisplayOrientation::sladoPortrait
        ? RasterBase::roPortrait
        : RasterBase::roLandscape;

    if (orientation == RasterBase::roPortrait) {
        std::swap(w, h);
        std::swap(g.px_x, g.px_y);
    }

    g.w_mm = w;
    g.h_mm = h;
    g.gamma = printer_config.get<double>("gamma_correction");
    g.trafo = RasterBase::Trafo{orientation, mirror};

    return g;
}

void copy_pixels(SlaLayerImage& result, const void* ptr, size_t w, size_t h, size_t num_components)
{
    const uint8_t* src = static_cast<const uint8_t*>(ptr);
    if (num_components == 1) {
        result.pixels.assign(src, src + w * h);
    } else {
        result.pixels.resize(w * h);
        for (size_t i = 0; i < w * h; ++i) {
            result.pixels[i] = src[i * num_components];
        }
    }
}

} // namespace

SlaLayerImage render_sla_layer_image(const Domain::ExPolygons& slice,
                                     const Domain::ConfigView& printer_config,
                                     size_t max_width, size_t max_height)
{
    SlaLayerImage result;

    DisplayGeometry g = get_display_geometry(printer_config);
    size_t pw = g.px_x;
    size_t ph = g.px_y;

    Resolution res{pw, ph};
    PixelDim pxdim{g.w_mm / pw, g.h_mm / ph};

    if (max_width != 0 && max_height != 0) {
        double factor = std::min(1.0, std::min(max_width / double(pw), max_height / double(ph)));
        size_t spw = std::max<size_t>(1, size_t(std::round(pw * factor)));
        size_t sph = std::max<size_t>(1, size_t(std::round(ph * factor)));
        res = Resolution{spw, sph};
        pxdim = PixelDim{g.w_mm / spw, g.h_mm / sph};
    }

    std::unique_ptr<RasterBase> raster = create_raster_grayscale_aa(res, pxdim, g.gamma, g.trafo);

    for (const ExPolygon& part : slice) {
        raster->draw(part);
    }

    result.width = res.width_px;
    result.height = res.height_px;
    result.pixel_width_mm = pxdim.w_mm;
    result.pixel_height_mm = pxdim.h_mm;

    raster->encode([&result](const void* ptr, size_t w, size_t h, size_t num_components) -> EncodedRaster {
        copy_pixels(result, ptr, w, h, num_components);
        return EncodedRaster({}, "");
    });

    return result;
}

SlaLayerImage render_sla_layer_image_region(const Domain::ExPolygons& slice,
                                            const Domain::ConfigView& printer_config,
                                            double center_x_mm, double center_y_mm,
                                            size_t width_px, size_t height_px)
{
    SlaLayerImage result;

    if (width_px == 0 || height_px == 0)
        return result;

    DisplayGeometry g = get_display_geometry(printer_config);
    if (g.px_x == 0 || g.px_y == 0)
        return result;

    // Native, undownscaled pixel size of the display.
    PixelDim pxdim{g.w_mm / g.px_x, g.h_mm / g.px_y};

    // The raster maps the slice onto the image starting at the corner of the display
    // rectangle: x_px = x_mm / pxdim.w_mm and y_px = y_mm / pxdim.h_mm, and the Trafo
    // only mirrors around the image edges (see AGGRaster::to_path). The requested point
    // has to end up in the middle of the window at (width_px / 2, height_px / 2), so the
    // slice is shifted by (half window - center). Mirroring is symmetric around the window
    // center, so the window covers the same area with and without mirroring.
    const Domain::Point shift{
        Biz::Algorithms::Scaling::scaled<Domain::coord_t>(0.5 * double(width_px) * pxdim.w_mm - center_x_mm),
        Biz::Algorithms::Scaling::scaled<Domain::coord_t>(0.5 * double(height_px) * pxdim.h_mm - center_y_mm)};

    Domain::ExPolygons shifted = slice;
    Biz::Algorithms::ExPolygon::translate(shifted, shift);

    Resolution res{width_px, height_px};

    std::unique_ptr<RasterBase> raster = create_raster_grayscale_aa(res, pxdim, g.gamma, g.trafo);

    for (const ExPolygon& part : shifted) {
        raster->draw(part);
    }

    result.width = res.width_px;
    result.height = res.height_px;
    result.pixel_width_mm = pxdim.w_mm;
    result.pixel_height_mm = pxdim.h_mm;

    raster->encode([&result](const void* ptr, size_t w, size_t h, size_t num_components) -> EncodedRaster {
        copy_pixels(result, ptr, w, h, num_components);
        return EncodedRaster({}, "");
    });

    return result;
}

} // namespace Slic3r::sla
