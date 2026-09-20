#include "GooSLA.hpp"

#include "libslic3r/SLA/RasterBase.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"

#include <cstdint>
#include <vector>
#include <string>
#include <array>
#include <algorithm>

using namespace Slic3r::Biz::Slicing;

namespace Slic3r {

struct GooSLARasterEncoder
{
    sla::EncodedRaster operator()(const void *ptr,
                                  size_t      w,
                                  size_t      h,
                                  size_t      num_components)
    {
        std::vector<uint8_t> dst;
        size_t total_pixels = w * h;
        dst.reserve(total_pixels * 2);

        const std::uint8_t *src = reinterpret_cast<const std::uint8_t *>(ptr);
        const std::uint8_t *src_end = src + total_pixels * num_components;

        dst.push_back(0x55);

        std::uint32_t checksum = 0;
        auto write_byte = [&](uint8_t b) {
            dst.push_back(b);
            checksum += b;
        };

        while (src < src_end) {
            uint8_t pixel_val = (*src) & 0xF0;
            size_t span_len = 0;
            const uint8_t *span_start = src;

            size_t max_len = (pixel_val == 0 || pixel_val == 0xF0) ? 0xFFFFFFFF : 0xF;
            while (src < src_end && span_len < max_len && ((*src) & 0xF0) == pixel_val) {
                span_len++;
                src += num_components;
            }

            if (pixel_val == 0 || pixel_val == 0xF0) {
                uint8_t type_byte;
                if (span_len <= 0xF) {
                    type_byte = (pixel_val == 0) ? 0x00 : 0xC0;
                    type_byte |= (span_len & 0xF);
                    write_byte(type_byte);
                } else if (span_len <= 0xFFF) {
                    type_byte = (pixel_val == 0) ? 0x10 : 0xD0;
                    type_byte |= ((span_len >> 8) & 0xF);
                    write_byte(type_byte);
                    write_byte(span_len & 0xFF);
                } else if (span_len <= 0xFFFFF) {
                    type_byte = (pixel_val == 0) ? 0x20 : 0xE0;
                    type_byte |= ((span_len >> 16) & 0xF);
                    write_byte(type_byte);
                    write_byte((span_len >> 8) & 0xFF);
                    write_byte(span_len & 0xFF);
                } else {
                    type_byte = (pixel_val == 0) ? 0x30 : 0xF0;
                    type_byte |= ((span_len >> 24) & 0xF);
                    write_byte(type_byte);
                    write_byte((span_len >> 16) & 0xFF);
                    write_byte((span_len >> 8) & 0xFF);
                    write_byte(span_len & 0xFF);
                }
            } else {
                uint8_t gray_val = pixel_val >> 4;
                uint8_t type_byte;
                if (span_len <= 0xF) {
                    type_byte = 0x40 | (span_len & 0xF);
                    write_byte(type_byte);
                    write_byte(gray_val);
                } else if (span_len <= 0xFFF) {
                    type_byte = 0x50 | ((span_len >> 8) & 0xF);
                    write_byte(type_byte);
                    write_byte((span_len & 0xFF));
                    write_byte(gray_val);
                } else if (span_len <= 0xFFFFF) {
                    type_byte = 0x60 | ((span_len >> 16) & 0xF);
                    write_byte(type_byte);
                    write_byte((span_len >> 8) & 0xFF);
                    write_byte(span_len & 0xFF);
                    write_byte(gray_val);
                } else {
                    type_byte = 0x70 | ((span_len >> 24) & 0xF);
                    write_byte(type_byte);
                    write_byte((span_len >> 16) & 0xFF);
                    write_byte((span_len >> 8) & 0xFF);
                    write_byte(span_len & 0xFF);
                    write_byte(gray_val);
                }
            }
        }

        write_byte(checksum & 0xFF);

        return sla::EncodedRaster(std::move(dst), "gooimg");
    }
};

class GooRasterizer : public ISlaRasterizer
{
    sla::Resolution res;
    sla::PixelDim pxdim;
    double gamma;
    sla::RasterBase::Trafo tr;

public:
    explicit GooRasterizer(const SLAPrintConfigView& cfg) {
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

        sla::RasterEncoder encoder = GooSLARasterEncoder{};
        sla::EncodedRaster encoded_raster = raster->encode(encoder);
        return std::move(encoded_raster.m_buffer);
    }
};

std::unique_ptr<Slic3r::ISlaRasterizer> create_goo_rasterizer(const SLAPrintConfigView& cfg){
    return std::make_unique<GooRasterizer>(cfg);
}

} // namespace Slic3r