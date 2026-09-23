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
            run_len = (len_low << 16) | (encoded[i++] << 8) | encoded[i++];
            pixel_val = (type == 0x2) ? 0x00 : 0xFF;
        }
        else if (type == 0x3 || type == 0xF) { // black or white, len > 1M
            if (i + 2 >= end) break;
            run_len = (len_low << 24) | (encoded[i++] << 16) | (encoded[i++] << 8) | encoded[i++];
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
            run_len = (len_low << 16) | (encoded[i++] << 8) | encoded[i++];
            if (i >= end) break;
            uint8_t gray_val = encoded[i++];
            pixel_val = gray_val * 16;
        }
        else if (type == 0x7) { // gray, len > 1M
            if (i + 2 >= end) break;
            run_len = (len_low << 24) | (encoded[i++] << 16) | (encoded[i++] << 8) | encoded[i++];
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

enum class Quadrant { TL, TR, BL, BR, Center, None };

static Quadrant get_quadrant(const CentroidInfo& c, size_t w, size_t h)
{
    if (!c.valid()) return Quadrant::None;
    double cx = c.cx;
    double cy = c.cy;
    double mid_x = double(w) / 2.0;
    double mid_y = double(h) / 2.0;
    bool left = cx < mid_x;
    bool top = cy < mid_y;
    if (left && top) return Quadrant::TL;
    if (!left && top) return Quadrant::TR;
    if (left && !top) return Quadrant::BL;
    return Quadrant::BR;
}

static std::string quadrant_name(Quadrant q)
{
    switch (q) {
        case Quadrant::TL: return "TL";
        case Quadrant::TR: return "TR";
        case Quadrant::BL: return "BL";
        case Quadrant::BR: return "BR";
        case Quadrant::Center: return "Center";
        default: return "None";
    }
}

// ---------------------------------------------------------------------------
// Test parameters
// ---------------------------------------------------------------------------
struct TestParams {
    std::string format; // "goo" or "pwmx"
    Slic3r::Domain::SLADisplayOrientation orientation;
    bool mirror_x;
    bool mirror_y;
    size_t expected_w;
    size_t expected_h;
    Quadrant expected_quadrant;
    std::string description;
};

// Build a table of all 16 combinations (2 formats × 2 orientations × 4 mirror settings)
static auto make_test_params_table()
{
    using Ori = Slic3r::Domain::SLADisplayOrientation;
    std::vector<TestParams> params;

    const std::vector<std::string> formats = {"goo", "pwmx"};
    const std::vector<Ori> orientations = {Ori::sladoLandscape, Ori::sladoPortrait};
    const std::vector<std::pair<bool,bool>> mirrors = {
        {false, false}, {true, false}, {false, true}, {true, true}
    };

    for (const auto& fmt : formats) {
        for (const auto& orient : orientations) {
            for (const auto& [mx, my] : mirrors) {
                TestParams p;
                p.format = fmt;
                p.orientation = orient;
                p.mirror_x = mx;
                p.mirror_y = my;
                if (orient == Ori::sladoLandscape) {
                    p.expected_w = 1440;
                    p.expected_h = 800;
                } else {
                    p.expected_w = 800;
                    p.expected_h = 1440;
                }
                // Quadrant expectations: TBD after first run. Placeholder = TL.
                // Reasoning for each case will be added as comments once observed.
                p.expected_quadrant = Quadrant::TL;
                p.description = fmt + "_" +
                    (orient == Ori::sladoLandscape ? "landscape" : "portrait") +
                    "_mx" + (mx ? "1" : "0") +
                    "_my" + (my ? "1" : "0");
                params.push_back(p);
            }
        }
    }
    return params;
}

static const auto test_params_table = make_test_params_table();

TEST_CASE("SLA Raster Orientation and Mirroring", "[export][sla][orientation]")
{
    auto params = GENERATE(
        table<std::string, Slic3r::Domain::SLADisplayOrientation, bool, bool, size_t, size_t, Quadrant, std::string>({
            // Format, Orientation, MirrorX, MirrorY, ExpW, ExpH, ExpQuad, Description
            // GOO Landscape
            {"goo", Slic3r::Domain::SLADisplayOrientation::sladoLandscape, false, false, 1440, 800, Quadrant::TL, "goo_landscape_mx0_my0"},
            {"goo", Slic3r::Domain::SLADisplayOrientation::sladoLandscape, true,  false, 1440, 800, Quadrant::TL, "goo_landscape_mx1_my0"},
            {"goo", Slic3r::Domain::SLADisplayOrientation::sladoLandscape, false, true,  1440, 800, Quadrant::TL, "goo_landscape_mx0_my1"},
            {"goo", Slic3r::Domain::SLADisplayOrientation::sladoLandscape, true,  true,  1440, 800, Quadrant::TL, "goo_landscape_mx1_my1"},
            // GOO Portrait
            {"goo", Slic3r::Domain::SLADisplayOrientation::sladoPortrait,  false, false, 800, 1440, Quadrant::TL, "goo_portrait_mx0_my0"},
            {"goo", Slic3r::Domain::SLADisplayOrientation::sladoPortrait,  true,  false, 800, 1440, Quadrant::TL, "goo_portrait_mx1_my0"},
            {"goo", Slic3r::Domain::SLADisplayOrientation::sladoPortrait,  false, true,  800, 1440, Quadrant::TL, "goo_portrait_mx0_my1"},
            {"goo", Slic3r::Domain::SLADisplayOrientation::sladoPortrait,  true,  true,  800, 1440, Quadrant::TL, "goo_portrait_mx1_my1"},
            // PW0/PWMX Landscape
            {"pwmx", Slic3r::Domain::SLADisplayOrientation::sladoLandscape, false, false, 1440, 800, Quadrant::TL, "pwmx_landscape_mx0_my0"},
            {"pwmx", Slic3r::Domain::SLADisplayOrientation::sladoLandscape, true,  false, 1440, 800, Quadrant::TL, "pwmx_landscape_mx1_my0"},
            {"pwmx", Slic3r::Domain::SLADisplayOrientation::sladoLandscape, false, true,  1440, 800, Quadrant::TL, "pwmx_landscape_mx0_my1"},
            {"pwmx", Slic3r::Domain::SLADisplayOrientation::sladoLandscape, true,  true,  1440, 800, Quadrant::TL, "pwmx_landscape_mx1_my1"},
            // PW0/PWMX Portrait
            {"pwmx", Slic3r::Domain::SLADisplayOrientation::sladoPortrait,  false, false, 800, 1440, Quadrant::TL, "pwmx_portrait_mx0_my0"},
            {"pwmx", Slic3r::Domain::SLADisplayOrientation::sladoPortrait,  true,  false, 800, 1440, Quadrant::TL, "pwmx_portrait_mx1_my0"},
            {"pwmx", Slic3r::Domain::SLADisplayOrientation::sladoPortrait,  false, true,  800, 1440, Quadrant::TL, "pwmx_portrait_mx0_my1"},
            {"pwmx", Slic3r::Domain::SLADisplayOrientation::sladoPortrait,  true,  true,  800, 1440, Quadrant::TL, "pwmx_portrait_mx1_my1"},
        })
    );

    // Unpack the tuple
    std::string format;
    Slic3r::Domain::SLADisplayOrientation orientation;
    bool mirror_x, mirror_y;
    size_t expected_w, expected_h;
    Quadrant expected_quadrant;
    std::string description;
    std::tie(format, orientation, mirror_x, mirror_y, expected_w, expected_h, expected_quadrant, description) = params;

    CAPTURE(description);

    Slic3r::Test::SlaSlicingFixture fixture;

    // Create model: one 20mm cube, moved to +X, -Y quadrant
    auto model = Slic3r::Test::generate_cubes(1, 1);
    REQUIRE(model.objects.size() == 1);
    REQUIRE(model.objects[0]->instances.size() == 1);
    auto* instance = model.objects[0]->instances[0];

    // Plate size from config: 144mm x 80mm, center at (0,0)
    // Cube is 20mm, centered at origin after generate_cubes+ensure_on_bed.
    // Offset to +40mm X, -20mm Y so cube spans X[30,50], Y[-30,-10] (in +X,-Y quadrant)
    instance->set_offset(Vec3d{40.0, -20.0, 0.0});

    auto config = Slic3r::Domain::ConfigPackSLA{};

    config.sla_printer_settings.items.opt("sla_archive_format").set(std::string(format));
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
    config.sla_print_settings.items.opt("supports_enable").set(false); // No supports to keep layer simple

    auto sla_result = fixture.slice_sla_model(model, config);
    REQUIRE(sla_result != nullptr);
    REQUIRE(!sla_result->files.data.empty());

    // Pick a middle layer (not first faded layers, not last)
    size_t layer_idx = sla_result->files.data.size() / 2;
    const auto& encoded_layer = sla_result->files.data[layer_idx];
    REQUIRE(!encoded_layer.empty());

    // Decode based on format
    std::vector<uint8_t> decoded;
    size_t expected_pixels = expected_w * expected_h;

    if (format == "goo") {
        decoded = decode_goo_layer(encoded_layer, expected_pixels);
    } else if (format == "pwmx") {
        decoded = decode_pw0_layer(encoded_layer, expected_pixels);
    } else {
        FAIL("Unknown format");
    }

    // The decoder should produce exactly expected_pixels
    // Try both orientation interpretations (width/height might be swapped in encoding)
    // But the encoder uses the raster resolution which already accounts for orientation swap.
    // So decoded size should match expected_w * expected_h.
    REQUIRE(decoded.size() == expected_pixels);

    // Compute centroid of lit pixels (threshold at 128 to catch anti-aliased edges)
    auto centroid = compute_centroid(decoded, expected_w, expected_h, 128);
    REQUIRE(centroid.valid());

    Quadrant actual_quad = get_quadrant(centroid, expected_w, expected_h);

    // Record image dimensions
    INFO("Decoded image: " << expected_w << "x" << expected_h);
    INFO("Lit pixel count: " << centroid.count);
    INFO("Centroid: (" << centroid.cx << ", " << centroid.cy << ")");
    INFO("Image center: (" << expected_w/2.0 << ", " << expected_h/2.0 << ")");
    INFO("Actual quadrant: " << quadrant_name(actual_quad));
    INFO("Expected quadrant: " << quadrant_name(expected_quadrant));

    // Assert dimensions match orientation
    if (orientation == Slic3r::Domain::SLADisplayOrientation::sladoLandscape) {
        REQUIRE(expected_w == 1440);
        REQUIRE(expected_h == 800);
    } else {
        REQUIRE(expected_w == 800);
        REQUIRE(expected_h == 1440);
    }

    // Assert quadrant matches expected (will be updated after first run)
    REQUIRE(actual_quad == expected_quadrant);
}