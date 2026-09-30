#pragma once

// Decoders for the layer images the SLA archive writers produce, so a test can look at the
// picture the printer's display receives. Nothing in the product reads .goo, .pwmx or .pm5 back,
// so the two run-length schemes are decoded here, from the encoders in
// src/libslic3r/src/libslic3r/Format/. The PNG and SVG writers need no decoder of their own:
// the image library is already there, and the vector one is parsed for its bounding box.

#include "Slic3r/Biz/Algorithms/PNGReadWrite.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r::Test::Sla {

// A decoded layer image: 8 bit gray, row-major, one row per display row, row 0 at the top.
struct DecodedLayer
{
    size_t width  = 0;
    size_t height = 0;
    std::vector<uint8_t> pixels;

    bool empty() const
    {
        return width == 0 || height == 0 || pixels.empty();
    }
};

// Where a written layer's geometry sits inside the image the display receives: the image size
// and the bounding box of the lit area, in that image's own coordinates. For the raster writers
// those are pixels; for the SVG writer they are viewBox units, which is the same image as a
// grid of `width` x `height` units, top left at (0, 0) as well.
struct LayerPlacement
{
    double width  = 0.;
    double height = 0.;
    double min_x  = 0.;
    double max_x  = 0.;
    double min_y  = 0.;
    double max_y  = 0.;

    bool valid() const
    {
        return width > 0. && height > 0. && max_x >= min_x && max_y >= min_y;
    }
};

// ---------------------------------------------------------------------------
// GOO format decoder (from libslic3r/src/libslic3r/Format/GooSLA.cpp GooSLARasterEncoder)
// ---------------------------------------------------------------------------
// Encoded format (per encoder):
// First byte: 0x55 (magic)
// Runs: each run starts with a type byte (high nibble = type, low nibble = length bits)
// Last byte: checksum (sum of all preceding bytes mod 256)
//
// Types:
// 0x0: black (0x00), run_len = low_nibble (1-15)
// 0x1: black, run_len = (low_nibble<<8) | next_byte (16-4095)
// 0x2: black, run_len = (low_nibble<<16) | next_byte<<8 | next_byte (4096-1048575)
// 0x3: black, run_len = (low_nibble<<24) | ... 4 bytes total
// 0x4: gray, run_len = low_nibble (1-15), followed by gray_val byte (1-14)
// 0x5: gray, run_len = (low_nibble<<8)|next_byte, followed by gray_val byte
// 0x6: gray, run_len = 3 bytes, followed by gray_val byte
// 0x7: gray, run_len = 4 bytes, followed by gray_val byte
// 0xC: white (0xFF), run_len = low_nibble (1-15)
// 0xD: white, run_len = (low_nibble<<8)|next_byte
// 0xE: white, run_len = 3 bytes
// 0xF: white, run_len = 4 bytes
//
// Gray values 1-14 map to pixel values 16,32,...,224 (gray_val * 16)

inline std::vector<uint8_t>
decode_goo_layer(const std::vector<uint8_t>& encoded, size_t expected_pixels)
{
    std::vector<uint8_t> pixels;
    pixels.reserve(expected_pixels);

    size_t i = 0;
    // Skip leading 0x55 if present
    if (!encoded.empty() && encoded[0] == 0x55)
        i = 1;

    // Last byte is checksum, don't process it
    size_t end = encoded.size();
    if (end > i)
        end -= 1;

    while (i < end && pixels.size() < expected_pixels) {
        uint8_t byte    = encoded[i++];
        uint8_t type    = byte >> 4;
        uint8_t len_low = byte & 0x0F;

        uint32_t run_len  = 0;
        uint8_t pixel_val = 0;

        if (type == 0x0 || type == 0xC) { // black or white, len <= 15
            run_len   = len_low;
            pixel_val = (type == 0x0) ? 0x00 : 0xFF;
        } else if (type == 0x1 || type == 0xD) { // black or white, len <= 4095
            if (i >= end)
                break;
            run_len   = (len_low << 8) | encoded[i++];
            pixel_val = (type == 0x1) ? 0x00 : 0xFF;
        } else if (type == 0x2 || type == 0xE) { // black or white, len <= 1M
            if (i + 1 >= end)
                break;
            run_len = (uint32_t(len_low) << 16) | (uint32_t(encoded[i]) << 8) | encoded[i + 1];
            i += 2;
            pixel_val = (type == 0x2) ? 0x00 : 0xFF;
        } else if (type == 0x3 || type == 0xF) { // black or white, len > 1M
            if (i + 2 >= end)
                break;
            run_len = (uint32_t(len_low) << 24)
                | (uint32_t(encoded[i]) << 16)
                | (uint32_t(encoded[i + 1]) << 8)
                | encoded[i + 2];
            i += 3;
            pixel_val = (type == 0x3) ? 0x00 : 0xFF;
        } else if (type == 0x4) { // gray, len <= 15
            run_len = len_low;
            if (i >= end)
                break;
            uint8_t gray_val = encoded[i++]; // 1-14
            pixel_val        = gray_val * 16;
        } else if (type == 0x5) { // gray, len <= 4095
            if (i >= end)
                break;
            run_len = (len_low << 8) | encoded[i++];
            if (i >= end)
                break;
            uint8_t gray_val = encoded[i++];
            pixel_val        = gray_val * 16;
        } else if (type == 0x6) { // gray, len <= 1M
            if (i + 1 >= end)
                break;
            run_len = (uint32_t(len_low) << 16) | (uint32_t(encoded[i]) << 8) | encoded[i + 1];
            i += 2;
            if (i >= end)
                break;
            uint8_t gray_val = encoded[i++];
            pixel_val        = gray_val * 16;
        } else if (type == 0x7) { // gray, len > 1M
            if (i + 2 >= end)
                break;
            run_len = (uint32_t(len_low) << 24)
                | (uint32_t(encoded[i]) << 16)
                | (uint32_t(encoded[i + 1]) << 8)
                | encoded[i + 2];
            i += 3;
            if (i >= end)
                break;
            uint8_t gray_val = encoded[i++];
            pixel_val        = gray_val * 16;
        } else {
            // Unknown type, skip
            continue;
        }

        // Clamp to expected pixels
        if (pixels.size() + run_len > expected_pixels) {
            run_len = expected_pixels - pixels.size();
        }
        pixels.insert(pixels.end(), run_len, pixel_val);
    }

    return pixels;
}

// ---------------------------------------------------------------------------
// Anycubic PW0 format decoder (from libslic3r/src/libslic3r/Format/AnycubicSLA.cpp AnycubicSLARasterEncoder)
// ---------------------------------------------------------------------------
// Each run: high nibble = pixel value (0-15), low nibble = run length
// For pixel value 0 (black) or 15 (white): run length = ((byte & 0x0F) << 8) | next_byte
// For other pixel values: run length = byte & 0x0F (1-15)
// Pixel values 0-15 map to 0-255 by *17

inline std::vector<uint8_t>
decode_pw0_layer(const std::vector<uint8_t>& encoded, size_t expected_pixels)
{
    std::vector<uint8_t> pixels;
    pixels.reserve(expected_pixels);

    size_t i = 0;
    while (i < encoded.size() && pixels.size() < expected_pixels) {
        uint8_t byte      = encoded[i++];
        uint8_t pixel_val = byte >> 4;
        uint8_t len_low   = byte & 0x0F;

        uint32_t run_len = 0;
        if (pixel_val == 0 || pixel_val == 0xF) {
            if (i >= encoded.size())
                break;
            run_len = (len_low << 8) | encoded[i++];
        } else {
            run_len = len_low;
        }

        uint8_t gray = pixel_val * 17; // 0->0, 15->255
        if (pixels.size() + run_len > expected_pixels) {
            run_len = expected_pixels - pixels.size();
        }
        pixels.insert(pixels.end(), run_len, gray);
    }

    return pixels;
}

// The .sl1 rasterizer writes plain 8 bit gray PNGs (Format/SL1.cpp).
inline DecodedLayer decode_png_layer(const std::vector<uint8_t>& encoded)
{
    png::ImageGreyscale image;
    if (!png::decode_png(png::ReadBuf{encoded.data(), encoded.size()}, image))
        return DecodedLayer{};

    return DecodedLayer{image.cols, image.rows, std::move(image.buf)};
}

// The bounding box of everything at or above `threshold` in a decoded image. An image that is
// shorter than `width` x `height` pixels yields an invalid placement, which is how a truncated
// or wrongly sized layer shows up.
inline LayerPlacement placement_of_pixels(
    const std::vector<uint8_t>& pixels,
    size_t width,
    size_t height,
    uint8_t threshold = 128
)
{
    LayerPlacement placement;
    placement.width  = double(width);
    placement.height = double(height);

    if (width == 0 || height == 0 || pixels.size() < width * height)
        return LayerPlacement{};

    bool any = false;
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x) {
            if (pixels[y * width + x] < threshold)
                continue;
            if (!any) {
                placement.min_x = placement.max_x = double(x);
                placement.min_y = placement.max_y = double(y);
                any                               = true;
                continue;
            }
            placement.min_x = std::min(placement.min_x, double(x));
            placement.max_x = std::max(placement.max_x, double(x));
            placement.min_y = std::min(placement.min_y, double(y));
            placement.max_y = std::max(placement.max_y, double(y));
        }
    }

    return any ? placement : LayerPlacement{};
}

// Every number in text[from, to), skipping the SVG command letters. The writer prints whole
// numbers only (SL1_SVG.cpp decimal_from), but a decimal point is accepted as well.
inline std::vector<double> read_svg_numbers(const std::string& text, size_t from, size_t to)
{
    std::vector<double> numbers;

    for (size_t i = from; i < to;) {
        const char c     = text[i];
        const bool digit = std::isdigit(static_cast<unsigned char>(c)) != 0;
        if (!digit && c != '-' && c != '+' && c != '.') {
            ++i;
            continue;
        }

        const size_t start = i;
        if (c == '-' || c == '+')
            ++i;
        const size_t first_digit = i;
        while (i < to && (std::isdigit(static_cast<unsigned char>(text[i])) != 0 || text[i] == '.'))
            ++i;
        if (i == first_digit) {
            // A sign or a dot with no digits behind it.
            ++i;
            continue;
        }

        numbers.push_back(std::stod(text.substr(start, i - start)));
    }

    return numbers;
}

// The bounding box of the paths of an SVG layer, in viewBox units. Each path is written as
// "M x y l dx dy ... z" (SL1_SVG.cpp append_svg): the first point absolute, every later one a
// delta from the previous point. The mirroring and orientation of the paths are the same as in
// the raster writers (SL1_SVG.cpp transform), so the viewBox is read as the image grid.
inline LayerPlacement placement_of_svg(const std::vector<uint8_t>& encoded)
{
    const std::string svg(encoded.begin(), encoded.end());

    LayerPlacement placement;

    const size_t view_box = svg.find("viewBox=\"");
    if (view_box == std::string::npos)
        return placement;
    const size_t box_begin = view_box + 9; // strlen("viewBox=\"")
    const size_t box_end   = svg.find('"', box_begin);
    if (box_end == std::string::npos)
        return placement;
    const std::vector<double> box = read_svg_numbers(svg, box_begin, box_end);
    if (box.size() < 4)
        return placement;
    placement.width  = box[2];
    placement.height = box[3];

    bool any     = false;
    double min_x = 0., max_x = 0., min_y = 0., max_y = 0.;
    const auto fold = [&](double x, double y)
    {
        if (!any) {
            min_x = max_x = x;
            min_y = max_y = y;
            any           = true;
            return;
        }
        min_x = std::min(min_x, x);
        max_x = std::max(max_x, x);
        min_y = std::min(min_y, y);
        max_y = std::max(max_y, y);
    };

    for (size_t path = svg.find("<path d=\""); path != std::string::npos;
         path        = svg.find("<path d=\"", path + 1))
    {
        const size_t path_begin = path + 9; // strlen("<path d=\"")
        const size_t path_end   = svg.find('"', path_begin);
        if (path_end == std::string::npos)
            break;

        const std::vector<double> points = read_svg_numbers(svg, path_begin, path_end);
        if (points.size() < 2)
            continue;

        double x = points[0], y = points[1];
        fold(x, y);
        for (size_t i = 2; i + 1 < points.size(); i += 2) {
            x += points[i];
            y += points[i + 1];
            fold(x, y);
        }
    }

    placement.min_x = min_x;
    placement.max_x = max_x;
    placement.min_y = min_y;
    placement.max_y = max_y;
    return any ? placement : LayerPlacement{};
}

} // namespace Slic3r::Test::Sla
