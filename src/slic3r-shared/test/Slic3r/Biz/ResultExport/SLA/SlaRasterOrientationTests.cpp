#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "Slic3r/Domain/ConfigDefsSLA.hpp"

#include "Slic3r/Biz/SlaFixture.hpp"
#include "Slic3r/Biz/ResultExport/SLA/SlaArchiveFormat.hpp"
#include "Slic3r/TestUtils/TestTempDir.hpp"

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <array>
#include <algorithm>
#include <numeric>
#include <cmath>

namespace fs = boost::filesystem;

using Slic3r::Biz::Slicing::SLAResultData;
using Slic3r::Biz::PrintHost::Sla::SlaArchiveFormatRegistry;
using Slic3r::Biz::PrintHost::Sla::register_sla_archive_formats;
using Slic3r::Biz::Slicing::Sla::FileDataType;
using Slic3r::Domain::Vec3d;

static std::vector<uint8_t> read_file_binary(const fs::path& path)
{
    boost::nowide::ifstream file(path.string(), std::ios::binary);
    file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

// ---------------------------------------------------------------------------
// GOO format decoder (from libslic3r/src/libslic3r/Format/GooSLA.cpp GooSLARasterEncoder)
// ---------------------------------------------------------------------------
// Encoded format (per encoder):
//   First byte: 0x55 (magic)
//   Runs: each run starts with a type byte (high nibble = type, low nibble = length bits)
//   Last byte: checksum (sum of all preceding bytes mod 256)
//
// Types:
//   0x0: black (0x00), run_len = low_nibble (1-15)
//   0x1: black, run_len = (low_nibble<<8) | next_byte (16-4095)
//   0x2: black, run_len = (low_nibble<<16) | next_byte<<8 | next_byte (4096-1048575)
//   0x3: black, run_len = (low_nibble<<24) | ... 4 bytes total
//   0x4: gray, run_len = low_nibble (1-15), followed by gray_val byte (1-14)
//   0x5: gray, run_len = (low_nibble<<8)|next_byte, followed by gray_val byte
//   0x6: gray, run_len = 3 bytes, followed by gray_val byte
//   0x7: gray, run_len = 4 bytes, followed by gray_val byte
//   0xC: white (0xFF), run_len = low_nibble (1-15)
//   0xD: white, run_len = (low_nibble<<8)|next_byte
//   0xE: white, run_len = 3 bytes
//   0xF: white, run_len = 4 bytes
//
// Gray values 1-14 map to pixel values 16,32,...,224 (gray_val * 16)

static std::vector<uint8_t> decode_goo_layer(const std::vector<uint8_t>& encoded, size_t expected_pixels)
{
    std::vector<uint8_t> pixels;
    pixels.reserve(expected_pixels);

    size_t i = 0;
    // Skip leading 0x55 if present
    if (!encoded.empty() && encoded[0] == 0x55) i = 1;

    // Last byte is checksum, don't process it
    size_t end = encoded.size();
    if (end > i) end -= 1;

    while (i < end && pixels.size() < expected_pixels) {
        uint8_t byte = encoded[i++];
        uint8_t type = byte >> 4;
        uint8_t len_low = byte & 0x0F;

        uint32_t run_len = 0;
        uint8_t pixel_val = 0;

        if (type == 0x0 || type == 0xC) { // black or white, len <= 15
            run_len = len_low;
            pixel_val = (type == 0x0) ? 0x00 : 0xFF;
        }
        else if (type == 0x1 || type == 0xD) { // black or white, len <= 4095
            if (i >= end) break;
            run_len = (len_low << 8) | encoded[i++];
            pixel_val = (type == 0x1) ? 0x00 : 0xFF;
        }
        else if (type == 0x2 || type == 0xE) { // black or white, len <= 1M
            if (i + 1 >= end) break;
            run_len = (uint32_t(len_low) << 16) | (uint32_t(encoded[i]) << 8) | encoded[i + 1];
            i += 2;
            pixel_val = (type == 0x2) ? 0x00 : 0xFF;
        }
        else if (type == 0x3 || type == 0xF) { // black or white, len > 1M
            if (i + 2 >= end) break;
            run_len = (uint32_t(len_low) << 24) | (uint32_t(encoded[i]) << 16) | (uint32_t(encoded[i + 1]) << 8) | encoded[i + 2];
            i += 3;
            pixel_val = (type == 0x3) ? 0x00 : 0xFF;
        }
        else if (type == 0x4) { // gray, len <= 15
            run_len = len_low;
            if (i >= end) break;
            uint8_t gray_val = encoded[i++]; // 1-14
            pixel_val = gray_val * 16;
        }
        else if (type == 0x5) { // gray, len <= 4095
            if (i >= end) break;
            run_len = (len_low << 8) | encoded[i++];
            if (i >= end) break;
            uint8_t gray_val = encoded[i++];
            pixel_val = gray_val * 16;
        }
        else if (type == 0x6) { // gray, len <= 1M
            if (i + 1 >= end) break;
            run_len = (uint32_t(len_low) << 16) | (uint32_t(encoded[i]) << 8) | encoded[i + 1];
            i += 2;
            if (i >= end) break;
            uint8_t gray_val = encoded[i++];
            pixel_val = gray_val * 16;
        }
        else if (type == 0x7) { // gray, len > 1M
            if (i + 2 >= end) break;
            run_len = (uint32_t(len_low) << 24) | (uint32_t(encoded[i]) << 16) | (uint32_t(encoded[i + 1]) << 8) | encoded[i + 2];
            i += 3;
            if (i >= end) break;
            uint8_t gray_val = encoded[i++];
            pixel_val = gray_val * 16;
        }
        else {
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

static std::vector<uint8_t> decode_pw0_layer(const std::vector<uint8_t>& encoded, size_t expected_pixels)
{
    std::vector<uint8_t> pixels;
    pixels.reserve(expected_pixels);

    size_t i = 0;
    while (i < encoded.size() && pixels.size() < expected_pixels) {
        uint8_t byte = encoded[i++];
        uint8_t pixel_val = byte >> 4;
        uint8_t len_low = byte & 0x0F;

        uint32_t run_len = 0;
        if (pixel_val == 0 || pixel_val == 0xF) {
            if (i >= encoded.size()) break;
            run_len = (len_low << 8) | encoded[i++];
        }
        else {
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

// ---------------------------------------------------------------------------
// Helper to compute lit pixel centroid and quadrant
// ---------------------------------------------------------------------------
struct CentroidInfo {
    double cx = 0;
    double cy = 0;
    size_t count = 0;
    bool valid() const { return count > 0; }
};

static CentroidInfo compute_centroid(const std::vector<uint8_t>& img, size_t w, size_t h, uint8_t threshold = 128)
{
    CentroidInfo info;
    double sum_x = 0, sum_y = 0;
    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            if (img[y * w + x] >= threshold) {
                sum_x += double(x);
                sum_y += double(y);
                info.count++;
            }
        }
    }
    if (info.count > 0) {
        info.cx = sum_x / info.count;
        info.cy = sum_y / info.count;
    }
    return info;
}


// ---------------------------------------------------------------------------
// Slice one cube placed well off-centre, and decode a middle layer of the chosen format.
// ---------------------------------------------------------------------------
static constexpr size_t DISPLAY_PIXELS = 1440 * 800;

static std::vector<uint8_t> slice_and_decode(const std::string& format,
                                             Slic3r::Domain::SLADisplayOrientation orientation,
                                             bool mirror_x, bool mirror_y)
{
    Slic3r::Test::SlaSlicingFixture fixture;

    auto model = Slic3r::Test::generate_cubes(1, 1);
    REQUIRE(model.objects.size() == 1);
    REQUIRE(model.objects[0]->instances.size() == 1);
    // The raster covers 0..144 x 0..80 mm from the bed origin. The 20 mm cube starts at the origin,
    // so this offset puts it at 100..120 x 50..70: wholly on the display and clear of both centre
    // lines, so every flip of an axis is visible.
    model.objects[0]->instances[0]->set_offset(Vec3d{100.0, 50.0, 0.0});

    auto config = Slic3r::Domain::ConfigPackSLA{};
    config.sla_printer_settings.items.opt("sla_archive_format").set(format);
    config.sla_printer_settings.items.opt("display_pixels_x").set(1440);
    config.sla_printer_settings.items.opt("display_pixels_y").set(800);
    config.sla_printer_settings.items.opt("display_orientation").set(orientation);
    config.sla_printer_settings.items.opt("display_width").set(144.0);
    config.sla_printer_settings.items.opt("display_height").set(80.0);
    config.sla_printer_settings.items.opt("display_mirror_x").set(mirror_x);
    config.sla_printer_settings.items.opt("display_mirror_y").set(mirror_y);
    config.sla_printer_settings.items.opt("gamma_correction").set(1.0);
    config.sla_print_settings.items.opt("layer_height").set(0.05);
    config.sla_material_settings.items.opt("initial_layer_height").set(0.05);
    config.sla_material_settings.items.opt("exposure_time").set(6.0);
    config.sla_material_settings.items.opt("initial_exposure_time").set(35.0);
    config.sla_print_settings.items.opt("faded_layers").set(10);
    config.sla_material_settings.items.opt("bottle_weight").set(1.0);
    config.sla_material_settings.items.opt("bottle_volume").set(1000.0);
    config.sla_material_settings.items.opt("bottle_cost").set(0.0);
    // No supports and no pad: every layer is a plain square.
    config.sla_print_settings.items.opt("supports_enable").set(false);
    config.sla_print_settings.items.opt("pad_enable").set(false);
    config.sla_print_settings.items.opt("raft_type").set(Slic3r::Domain::sla::RaftType::None);

    auto sla_result = fixture.slice_sla_model(model, config);
    REQUIRE(sla_result != nullptr);
    REQUIRE_FALSE(sla_result->files.data.empty());

    const auto& layer = sla_result->files.data[sla_result->files.data.size() / 2];
    REQUIRE_FALSE(layer.empty());
    std::vector<uint8_t> image = format == "goo" ? decode_goo_layer(layer, DISPLAY_PIXELS)
                                                 : decode_pw0_layer(layer, DISPLAY_PIXELS);
    REQUIRE(image.size() == DISPLAY_PIXELS);
    return image;
}

// How much of their bounding box the lit pixels fill when rows are `row` pixels wide. A real
// layer is compact. The same bytes read with the wrong row width smear into a sparse band, which
// is the only way to tell a transposed image apart: its pixel count is exactly right.
static double fill_ratio(const std::vector<uint8_t>& image, size_t row)
{
    const size_t rows = image.size() / row;
    size_t min_x = row, max_x = 0, min_y = rows, max_y = 0, lit = 0;
    for (size_t y = 0; y < rows; ++y) {
        for (size_t x = 0; x < row; ++x) {
            if (image[y * row + x] >= 128) {
                ++lit;
                min_x = std::min(min_x, x);
                max_x = std::max(max_x, x);
                min_y = std::min(min_y, y);
                max_y = std::max(max_y, y);
            }
        }
    }
    if (lit == 0)
        return 0.;
    return double(lit) / double((max_x - min_x + 1) * (max_y - min_y + 1));
}

// These tests assert relations, not absolute positions: which image corner a part lands in, and
// which image axis each mirror flag flips, are conventions that still have to be confirmed on a
// real printer (M5.4b). The relations hold whatever that convention turns out to be.
TEST_CASE("SLA raster orientation and mirroring", "[export][sla][orientation]")
{
    using Orientation = Slic3r::Domain::SLADisplayOrientation;
    const std::string format = GENERATE(std::string("goo"), std::string("pwmx"));
    const Orientation orientation = GENERATE(Orientation::sladoLandscape, Orientation::sladoPortrait);
    const bool landscape = orientation == Orientation::sladoLandscape;
    CAPTURE(format, landscape);

    // Landscape keeps the display's long side along the rows (1440-pixel rows); portrait swaps it.
    const size_t row = landscape ? 1440 : 800;
    const size_t other_row = landscape ? 800 : 1440;
    const size_t rows = DISPLAY_PIXELS / row;

    const std::vector<uint8_t> plain = slice_and_decode(format, orientation, false, false);

    // The image must only make sense at the expected row width.
    const double fill = fill_ratio(plain, row);
    const double fill_other = fill_ratio(plain, other_row);
    CAPTURE(fill, fill_other);
    REQUIRE(fill > 0.5);
    REQUIRE(fill_other < fill / 2);

    const CentroidInfo c0 = compute_centroid(plain, row, rows);
    REQUIRE(c0.valid());
    CAPTURE(c0.cx, c0.cy);
    REQUIRE(std::abs(c0.cx - row / 2.0) > row * 0.1);
    REQUIRE(std::abs(c0.cy - rows / 2.0) > rows * 0.1);

    const CentroidInfo cmx = compute_centroid(slice_and_decode(format, orientation, true, false), row, rows);
    const CentroidInfo cmy = compute_centroid(slice_and_decode(format, orientation, false, true), row, rows);
    REQUIRE(cmx.valid());
    REQUIRE(cmy.valid());
    CAPTURE(cmx.cx, cmx.cy, cmy.cx, cmy.cy);

    const auto flipped = [](double a, double b, double mid) { return (a < mid) != (b < mid); };
    const bool mx_flips_columns = flipped(c0.cx, cmx.cx, row / 2.0);
    const bool mx_flips_rows = flipped(c0.cy, cmx.cy, rows / 2.0);
    const bool my_flips_columns = flipped(c0.cx, cmy.cx, row / 2.0);
    const bool my_flips_rows = flipped(c0.cy, cmy.cy, rows / 2.0);
    CAPTURE(mx_flips_columns, mx_flips_rows, my_flips_columns, my_flips_rows);

    // Each mirror flag moves the part across exactly one image axis, and the two flags move it
    // across different axes.
    REQUIRE(mx_flips_columns != mx_flips_rows);
    REQUIRE(my_flips_columns != my_flips_rows);
    REQUIRE(mx_flips_columns != my_flips_columns);
}
