#include "AnycubicSLA.hpp"

#include "libslic3r/SLA/RasterBase.hpp"
#include "libslic3r/Domain/ConfigDefsSLA.hpp"

#include <cstdint>
#include <vector>
#include <string>
#include <array>
#include <algorithm>

namespace Slic3r {

static void anycubicsla_get_pixel_span(const std::uint8_t* ptr, const std::uint8_t* end,
                               std::uint8_t& pixel, size_t& span_len)
{
    size_t max_len;

    span_len = 0;
    pixel = (*ptr) & 0xF0;
    max_len = (pixel == 0 || pixel == 0xF0) ? 0xFFF : 0xF;
    while (ptr < end && span_len < max_len && ((*ptr) & 0xF0) == pixel) {
        span_len++;
        ptr++;
    }
}

struct AnycubicSLARasterEncoder
{
    sla::EncodedRaster operator()(const void *ptr,
                                  size_t      w,
                                  size_t      h,
                                  size_t      num_components)
    {
        std::vector<uint8_t> dst;
        size_t               span_len;
        std::uint8_t         pixel;
        auto                 size = w * h * num_components;
        dst.reserve(size);

        const std::uint8_t *src = reinterpret_cast<const std::uint8_t *>(ptr);
        const std::uint8_t *src_end = src + size;
        while (src < src_end) {
            anycubicsla_get_pixel_span(src, src_end, pixel, span_len);
            src += span_len;
            if (pixel == 0 || pixel == 0xF0) {
                pixel = pixel | (span_len >> 8);
                std::copy(&pixel, (&pixel) + 1, std::back_inserter(dst));
                pixel = span_len & 0xFF;
                std::copy(&pixel, (&pixel) + 1, std::back_inserter(dst));
            }
            else {
                pixel = pixel | span_len;
                std::copy(&pixel, (&pixel) + 1, std::back_inserter(dst));
            }
        }

        return sla::EncodedRaster(std::move(dst), "pwimg");
    }
};

class AnycubicRasterizer : public ISlaRasterizer
{
    sla::Resolution res;
    sla::PixelDim pxdim;
    double gamma;
    sla::RasterBase::Trafo tr;

public:
    explicit AnycubicRasterizer(const SLAPrintConfigView& cfg) {
        double w = cfg.get<double>("display_width");
        double h = cfg.get<double>("display_height");
        auto pw = size_t(cfg.get<int>("display_pixels_x"));
        auto ph = size_t(cfg.get<int>("display_pixels_y"));

        std::array<bool, 2> mirror;
        mirror[0] = cfg.get<bool>("display_mirror_x");
        mirror[1] = cfg.get<bool>("display_mirror_y");

        auto ro = cfg.get<Domain::SLADisplayOrientation>("display_orientation");
        sla::RasterBase::Orientation orientation = ro == Domain::SLADisplayOrientation::sladoPortrait
            ? sla::RasterBase::roPortrait
            : sla::RasterBase::roLandscape;

        if (orientation == sla::RasterBase::roPortrait) {
            std::swap(w, h);
            std::swap(pw, ph);
        }

        res = sla::Resolution{pw, ph};
        pxdim = sla::PixelDim{w / pw, h / ph};
        gamma = cfg.get<double>("gamma_correction");
        tr = sla::RasterBase::Trafo{orientation, mirror};
    }

    Sla::FileData create_file(const ExPolygons& slice) override {
        std::unique_ptr<sla::RasterBase> raster = create_raster_grayscale_aa(res, pxdim, gamma, tr);
        for (const ExPolygon& part : slice)
            raster->draw(part);

        sla::RasterEncoder encoder = AnycubicSLARasterEncoder{};
        sla::EncodedRaster encoded_raster = raster->encode(encoder);
        return std::move(encoded_raster.m_buffer);
    }
};

std::unique_ptr<Slic3r::ISlaRasterizer> create_anycubic_rasterizer(const SLAPrintConfigView& cfg){
    return std::make_unique<AnycubicRasterizer>(cfg);
}

} // namespace Slic3r