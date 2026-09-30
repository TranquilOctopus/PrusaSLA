// Decoders for the layer encodings the SLA writers produce, so a test can look at the pixels a
// printer would see instead of at the run-length bytes. One decoder per encoding, shared by the
// export tests in this folder (M4.13).
//
// Every decoder returns 0..255 greyscale values, one per pixel, row major: the same range the
// rasterizer produced before the encoder quantized it, so a test can tell a thresholded layer
// (only 0 and 255) from an anti-aliased one (anything in between) no matter what the encoder did
// with the levels in between.
#pragma once

#include <cstdint>
#include <set>
#include <vector>

#include "Slic3r/Biz/Algorithms/PNGReadWrite.hpp"

namespace Slic3r::Test::Sla {

// ---------------------------------------------------------------------------
// Elegoo .goo ("gooimg")
// ---------------------------------------------------------------------------
// From libslic3r/src/libslic3r/Format/GooSLA.cpp, GooSLARasterEncoder:
//   First byte 0x55, then runs, then one checksum byte (the sum of the run bytes, mod 256).
//   A run starts with a type byte whose high nibble is the type and whose low nibble holds the
//   high bits of the run length:
//     0x0..0x3 black (0x00), run length in 1..4 bytes
//     0x4..0x7 grey,  run length in 1..4 bytes, then the grey nibble as its own byte
//     0xC..0xF white (0xF0), run length in 1..4 bytes
//   The grey nibble is 1..14, and the encoder masked the raster to its high nibble, so a grey
//   pixel reads back as grey * 16 while black and white read back as 0 and 255.
inline std::vector<uint8_t> decode_goo_layer(const std::vector<uint8_t>& encoded, size_t expected_pixels)
{
    std::vector<uint8_t> pixels;
    pixels.reserve(expected_pixels);

    size_t i = 0;
    if (!encoded.empty() && encoded[0] == 0x55) i = 1; // the magic byte
    const size_t end = encoded.empty() ? 0 : encoded.size() - 1; // the trailing checksum

    auto take_length = [&](size_t nibble_high, size_t bytes) -> uint32_t {
        uint32_t len = uint32_t(nibble_high);
        for (size_t b = 0; b < bytes; ++b) {
            if (i >= end) return 0;
            len = (len << 8) | encoded[i++];
        }
        return len;
    };

    while (i < end && pixels.size() < expected_pixels) {
        const uint8_t type_byte = encoded[i++];
        const uint8_t type       = uint8_t(type_byte >> 4);
        const uint8_t len_high   = uint8_t(type_byte & 0x0F);

        uint32_t run_len = 0;
        uint8_t value    = 0;

        if (type == 0x0 || type == 0xC) { // black or white, length in the low nibble
            run_len = len_high;
            value   = (type == 0x0) ? 0x00 : 0xFF;
        } else if (type == 0x1 || type == 0xD) { // 16..4095
            run_len = take_length(uint32_t(len_high) << 8, 1);
            value   = (type == 0x1) ? 0x00 : 0xFF;
        } else if (type == 0x2 || type == 0xE) { // 4096..1048575
            run_len = take_length(uint32_t(len_high) << 16, 2);
            value   = (type == 0x2) ? 0x00 : 0xFF;
        } else if (type == 0x3 || type == 0xF) { // over a million
            run_len = take_length(uint32_t(len_high) << 24, 3);
            value   = (type == 0x3) ? 0x00 : 0xFF;
        } else if (type >= 0x4 && type <= 0x7) { // grey, the length then one more byte
            const size_t extra = size_t(type - 0x3); // 1..4
            run_len           = take_length(uint32_t(len_high) << (8 * (extra - 1)), extra);
            if (i >= end) break;
            const uint8_t grey = encoded[i++];
            if (grey == 0 || grey == 0xF) continue; // the encoder routes those through black/white
            value = uint8_t(grey * 16);
        } else {
            continue; // not a run type this encoder writes
        }

        if (pixels.size() + run_len > expected_pixels) run_len = uint32_t(expected_pixels - pixels.size());
        pixels.insert(pixels.end(), run_len, value);
    }

    return pixels;
}

// ---------------------------------------------------------------------------
// Anycubic pw0 ("pwimg"): .pwmo, .pwmx, .pwms and .pm5 all use this one
// ---------------------------------------------------------------------------
// From libslic3r/src/libslic3r/Format/AnycubicSLA.cpp, AnycubicSLARasterEncoder: no magic byte
// and no checksum, just runs. Each run starts with a byte whose high nibble is the grey and whose
// low nibble holds the high bits of the run length; for grey 0 and grey 15 the length takes a
// second byte, for the other greys the low nibble is the whole length. The encoder masked the
// raster to its high nibble, so a grey reads back as grey * 17 and black/white as 0 and 255.
inline std::vector<uint8_t> decode_pw0_layer(const std::vector<uint8_t>& encoded, size_t expected_pixels)
{
    std::vector<uint8_t> pixels;
    pixels.reserve(expected_pixels);

    size_t i = 0;
    while (i < encoded.size() && pixels.size() < expected_pixels) {
        const uint8_t byte = encoded[i++];
        const uint8_t grey  = uint8_t(byte >> 4);
        const uint8_t low   = uint8_t(byte & 0x0F);

        uint32_t run_len = low;
        if (grey == 0x0 || grey == 0xF) {
            if (i >= encoded.size()) break;
            run_len = (uint32_t(low) << 8) | encoded[i++];
        }

        if (pixels.size() + run_len > expected_pixels) run_len = uint32_t(expected_pixels - pixels.size());
        pixels.insert(pixels.end(), run_len, uint8_t(grey * 17));
    }

    return pixels;
}

// The same decoder for a layer that is already somewhere in a file, as an offset and a length.
inline std::vector<uint8_t> decode_pw0_layer(const uint8_t* data, size_t size, size_t expected_pixels)
{
    return decode_pw0_layer(std::vector<uint8_t>(data, data + size), expected_pixels);
}

// ---------------------------------------------------------------------------
// Prusa .sl1 layer images: plain 8-bit greyscale PNG
// ---------------------------------------------------------------------------
// From libslic3r/src/libslic3r/SLA/RasterBase.cpp, PNGRasterEncoder: the raster buffer goes out
// through miniz as a 1-component PNG. png::decode_png only accepts 8-bit greyscale, so a layer
// that decodes at all is a greyscale one, and the values are the rasterizer's own 0..255.
// Returns an empty vector when the bytes are not such a PNG.
inline std::vector<uint8_t> decode_sl1_png_layer(const std::vector<uint8_t>& encoded)
{
    Slic3r::png::ImageGreyscale image;
    if (!Slic3r::png::decode_png(Slic3r::png::ReadBuf{encoded.data(), encoded.size()}, image)) return {};
    return std::move(image.buf);
}

// ---------------------------------------------------------------------------
// Level bookkeeping
// ---------------------------------------------------------------------------

/// The number of distinct greys in a decoded layer, and the greys themselves, smallest first.
inline std::set<uint8_t> distinct_greys(const std::vector<uint8_t>& pixels)
{
    return std::set<uint8_t>(pixels.begin(), pixels.end());
}

/// True when every value in the layer is one an encoder that kept a fixed number of levels could
/// have written: 0, a multiple of `step` below `top`, or `top` itself. The two RLE encoders
/// quantize differently (pw0 spreads 16 levels over the full range in steps of 17, goo steps by 16
/// and writes the top nibble as 255), so the caller passes the grid it expects.
inline bool on_quantization_grid(const std::vector<uint8_t>& pixels, int step, int top)
{
    if (step < 1) return false;
    for (uint8_t value : pixels) {
        if (value == 0 || value == top) continue;
        if (value % step != 0) return false;
        if (value >= top) return false;
    }
    return true;
}

} // namespace Slic3r::Test::Sla
