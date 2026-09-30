#include "CtbSLA.hpp"

#include "libslic3r/SLA/RasterBase.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"

#include <cstdint>
#include <vector>
#include <string>
#include <array>
#include <algorithm>

using namespace Slic3r::Biz::Slicing;

namespace Slic3r {

// The .ctb layer image is a run-length encoding of the 8-bit gray raster, one pixel per byte, in
// the order the rasterizer produced (the display orientation and the mirroring of the printer are
// already applied to it). doc/sla-fork/formats/ctb.md has the field list; the two run types are:
//
//   control byte 0x00..0xFD : a run of (control + 1) pixels, 1..254 copies of the byte after it
//   control byte 0xFF        : a literal block, the byte after it is the number of raw pixel
//                             bytes (1..255) that follow
//
// There is no end marker and no checksum: the layer definition carries the byte count of the layer,
// so a decoder stops after that many bytes. Nothing is keyed, which is what a v2/v3 file is.
struct CtbSLARasterEncoder
{
    sla::EncodedRaster operator()(const void* ptr, size_t w, size_t h, size_t num_components)
    {
        std::vector<std::uint8_t> dst;
        // Do not reserve for the uncompressed image: every encoded layer is kept until export, and
        // a vector keeps its capacity, which on a 12K display is about 59 MB per layer for data
        // that encodes to kilobytes. See shrink_to_fit below.
        dst.reserve(h * 4);

        const size_t step = num_components > 0 ? num_components : 1;
        const std::uint8_t* src = reinterpret_cast<const std::uint8_t*>(ptr);
        const std::uint8_t* src_end = src + w * h * step;

        const auto at = [&](size_t index) { return src[index * step]; };
        const auto left = [&](size_t index) { return src_end - src > static_cast<ptrdiff_t>(index * step); };

        while (src < src_end) {
            // A run of two or more equal pixels, the cheapest encoding for them.
            size_t run = 1;
            while (run < 254 && left(run + 1) && at(run) == at(0))
                ++run;
            if (run > 1) {
                dst.push_back(std::uint8_t(run - 1));
                dst.push_back(at(0));
                src += run * step;
                continue;
            }

            // A pixel that is alone, and any pixels that are not equal to their neighbour, go into a
            // literal block. The block ends before a run starts, so the decoder never has to guess
            // where one run of pixels ends and the next begins.
            size_t literal = 1;
            while (literal < 255 && left(literal)) {
                if (left(literal + 1) && at(literal) == at(literal + 1))
                    break;
                ++literal;
            }
            dst.push_back(0xFF);
            dst.push_back(std::uint8_t(literal));
            for (size_t i = 0; i < literal; ++i)
                dst.push_back(at(i));
            src += literal * step;
        }

        // Release the growth slack before the layer is stored alongside all the others.
        dst.shrink_to_fit();
        return sla::EncodedRaster(std::move(dst), "ctbimg");
    }
};

class CtbRasterizer : public ISlaRasterizer
{
    sla::Resolution res;
    sla::PixelDim pxdim;
    double gamma;
    sla::RasterBase::Trafo tr;

public:
    explicit CtbRasterizer(const SLAPrintConfigView& cfg)
    {
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

    Sla::FileData create_file(const ExPolygons& slice) override
    {
        std::unique_ptr<sla::RasterBase> raster = create_raster_grayscale_aa(res, pxdim, gamma, tr);
        for (const ExPolygon& part : slice)
            raster->draw(part);

        sla::RasterEncoder encoder = CtbSLARasterEncoder{};
        sla::EncodedRaster encoded_raster = raster->encode(encoder);
        return std::move(encoded_raster.m_buffer);
    }
};

std::unique_ptr<Slic3r::ISlaRasterizer> create_ctb_rasterizer(const SLAPrintConfigView& cfg)
{
    return std::make_unique<CtbRasterizer>(cfg);
}

} // namespace Slic3r
