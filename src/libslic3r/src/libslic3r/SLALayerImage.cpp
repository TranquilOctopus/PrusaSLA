#include <libslic3r/SLALayerImage.hpp>
#include <libslic3r/SLA/RasterBase.hpp>
#include <libslic3r/SLA/AGGRaster.hpp>
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include <libslic3r/ExPolygon.hpp>
#include <libslic3r/ConfigViews.hpp>
#include <algorithm>
#include <cmath>

namespace Slic3r::sla {

SlaLayerImage render_sla_layer_image(const Domain::ExPolygons& slice,
                                     const Domain::ConfigView& printer_config,
                                     size_t max_width, size_t max_height)
{
    SlaLayerImage result;

    double w = printer_config.get<double>("display_width");
    double h = printer_config.get<double>("display_height");
    auto pw = size_t(printer_config.get<int>("display_pixels_x"));
    auto ph = size_t(printer_config.get<int>("display_pixels_y"));

    std::array<bool, 2> mirror;
    mirror[0] = printer_config.get<bool>("display_mirror_x");
    mirror[1] = printer_config.get<bool>("display_mirror_y");

    auto ro = printer_config.get<Domain::SLADisplayOrientation>("display_orientation");
    RasterBase::Orientation orientation = ro == Domain::SLADisplayOrientation::sladoPortrait
        ? RasterBase::roPortrait
        : RasterBase::roLandscape;

    if (orientation == RasterBase::roPortrait) {
        std::swap(w, h);
        std::swap(pw, ph);
    }

    double gamma = printer_config.get<double>("gamma_correction");

    Resolution res{pw, ph};
    PixelDim pxdim{w / pw, h / ph};
    RasterBase::Trafo tr{orientation, mirror};

    if (max_width != 0 && max_height != 0) {
        double factor = std::min(1.0, std::min(max_width / double(pw), max_height / double(ph)));
        size_t spw = std::max<size_t>(1, size_t(std::round(pw * factor)));
        size_t sph = std::max<size_t>(1, size_t(std::round(ph * factor)));
        res = Resolution{spw, sph};
        pxdim = PixelDim{w / spw, h / sph};
    }

    std::unique_ptr<RasterBase> raster = create_raster_grayscale_aa(res, pxdim, gamma, tr);

    for (const ExPolygon& part : slice) {
        raster->draw(part);
    }

    result.width = res.width_px;
    result.height = res.height_px;
    result.pixel_width_mm = pxdim.w_mm;
    result.pixel_height_mm = pxdim.h_mm;

    raster->encode([&result](const void* ptr, size_t w, size_t h, size_t num_components) -> EncodedRaster {
        const uint8_t* src = static_cast<const uint8_t*>(ptr);
        if (num_components == 1) {
            result.pixels.assign(src, src + w * h);
        } else {
            result.pixels.resize(w * h);
            for (size_t i = 0; i < w * h; ++i) {
                result.pixels[i] = src[i * num_components];
            }
        }
        return EncodedRaster({}, "");
    });

    return result;
}

} // namespace Slic3r::sla