#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Slic3r/Domain/ConfigDefsSLA.hpp"

#include "Slic3r/Biz/SlaFixture.hpp"
#include "Slic3r/Biz/ResultExport/SLA/GooSLA.hpp"
#include "Slic3r/Biz/ResultExport/SLA/SlaArchiveFormat.hpp"
#include "Slic3r/TestUtils/TestTempDir.hpp"

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>

namespace fs = boost::filesystem;

using Slic3r::Biz::Slicing::SLAResultData;
using Slic3r::Biz::PrintHost::Sla::store_goo;
using Slic3r::Biz::PrintHost::Sla::SlaArchiveFormatRegistry;
using Slic3r::Biz::PrintHost::Sla::register_sla_archive_formats;
using Slic3r::Biz::Slicing::Sla::FileDataType;

static std::vector<uint8_t> read_file_binary(const fs::path& path)
{
    boost::nowide::ifstream file(path.string(), std::ios::binary);
    file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

template<typename T>
T read_be(const uint8_t* ptr)
{
    T val = 0;
    for (size_t i = 0; i < sizeof(T); ++i) {
        val = (val << 8) | static_cast<T>(ptr[i]);
    }
    return val;
}

// Both contain NUL bytes, so the length must be explicit or the string stops at the first one.
static const std::string GOO_ENDING("\x00\x00\x00\x07\x00\x00\x00\x44\x4C\x50\x00", 11);
static const std::string GOO_MAGIC_TAG("\x07\x00\x00\x00\x44\x4C\x50\x00", 8);
static const std::string GOO_DELIMITER = "\x0D\x0A";

TEST_CASE("Goo export", "[export][sla][goo]")
{
    Slic3r::Test::SlaSlicingFixture fixture;

    auto model = Slic3r::Test::generate_cubes(1, 5);
    auto config = Slic3r::Domain::ConfigPackSLA{};

    config.sla_printer_settings.items.opt("sla_archive_format").set(std::string("goo"));
    config.sla_printer_settings.items.opt("display_pixels_x").set(2560);
    config.sla_printer_settings.items.opt("display_pixels_y").set(1440);
    config.sla_printer_settings.items.opt("display_orientation").set(Slic3r::Domain::SLADisplayOrientation::sladoLandscape);
    config.sla_printer_settings.items.opt("display_width").set(120.96);
    config.sla_printer_settings.items.opt("display_height").set(68.04);
    config.sla_printer_settings.items.opt("display_mirror_x").set(true);
    config.sla_printer_settings.items.opt("display_mirror_y").set(false);
    config.sla_printer_settings.items.opt("gamma_correction").set(1.0);
    config.sla_print_settings.items.opt("layer_height").set(0.05);
    config.sla_material_settings.items.opt("initial_layer_height").set(0.05);
    config.sla_material_settings.items.opt("exposure_time").set(6.0);
    config.sla_material_settings.items.opt("initial_exposure_time").set(35.0);
    config.sla_print_settings.items.opt("faded_layers").set(10);
    // Layer separation. The .goo header holds these in mm, mm/s and s and the light PWM in 0-255,
    // the units of the settings themselves, so all of them go in unchanged.
    config.sla_material_settings.items.opt("lift_height").set(7.5);
    config.sla_material_settings.items.opt("lift_speed").set(1.25);
    config.sla_material_settings.items.opt("retract_speed").set(2.5);
    config.sla_material_settings.items.opt("wait_before_lift").set(3.0);
    config.sla_material_settings.items.opt("wait_after_lift").set(0.4);
    config.sla_material_settings.items.opt("wait_after_retract").set(0.6);
    config.sla_material_settings.items.opt("light_pwm").set(128);
    config.sla_material_settings.items.opt("bottom_lift_height").set(9.0);
    config.sla_material_settings.items.opt("bottom_lift_speed").set(1.75);
    config.sla_material_settings.items.opt("bottom_retract_speed").set(2.75);
    config.sla_material_settings.items.opt("bottom_wait_before_lift").set(4.0);
    config.sla_material_settings.items.opt("bottom_wait_after_lift").set(0.7);
    config.sla_material_settings.items.opt("bottom_wait_after_retract").set(0.8);
    config.sla_material_settings.items.opt("bottom_light_pwm").set(200);
    config.sla_material_settings.items.opt("bottle_weight").set(1.0);
    config.sla_material_settings.items.opt("bottle_volume").set(1000.0);
    config.sla_material_settings.items.opt("bottle_cost").set(0.0);
    config.sla_print_settings.items.opt("supports_enable").set(true);

    auto sla_result = fixture.slice_sla_model(model, config);
    REQUIRE(sla_result != nullptr);

    REQUIRE(sla_result->files.type == FileDataType::goo);

    // Every encoded layer is held in memory until export. The encoder used to reserve room for
    // the uncompressed image and keep it (about 118 MB per layer at 12K), which ran real slices
    // out of memory. A layer of this cube encodes to kilobytes, so its capacity must stay small.
    REQUIRE_FALSE(sla_result->files.data.empty());
    for (const auto& layer : sla_result->files.data)
        REQUIRE(layer.capacity() < 1024 * 1024);

    Tests::TestTempDir temp_dir;
    fs::path out_path = temp_dir.path() / "out.goo";

    register_sla_archive_formats();
    auto& registry = SlaArchiveFormatRegistry::instance();
    auto format = registry.find_by_file_data_type(FileDataType::goo);
    REQUIRE(format != nullptr);
    REQUIRE_NOTHROW(format->store(out_path.string(), *sla_result));

    REQUIRE(fs::exists(out_path));

    auto data = read_file_binary(out_path);
    REQUIRE(!data.empty());

    // Check magic tag at offset 4 (after 4-byte version)
    REQUIRE(data.size() >= 12);
    std::string magic(reinterpret_cast<const char*>(data.data() + 4), 8);
    REQUIRE(magic == GOO_MAGIC_TAG);

    // Header size: 4(version) + 8(magic) + 32(sw_info) + 24(sw_ver) + 24(file_time) + 32(printer_name) + 32(printer_type) + 32(profile_name) + 2*3(aa,grey,blur) + 2*116*116(small_preview) + 2(delim) + 2*290*290(big_preview) + 2(delim) + 4(total_layers) + 2(x_res) + 2(y_res) + 1(x_mirror) + 1(y_mirror) + 4*4(platform sizes) + 4(layer_thickness) + 4(common_exp) + 1(exp_delay_mode) + 4(turn_off) + 4*6(bottom times) + 4*6(normal times) + 4(bottom_exp) + 4(bottom_layers) + 4*12(lift/retract distances/speeds) + 2*2(pwm) + 1(advance) + 4(print_time) + 4(volume) + 4(weight) + 4(price) + 8(price_unit) + 4(layer_content_offset) + 1(gray_scale) + 2(transition)
    size_t header_fixed = 4 + 8 + 32 + 24 + 24 + 32 + 32 + 32 + 6 + 2*116*116 + 2 + 2*290*290 + 2 + 4 + 2 + 2 + 1 + 1 + 16 + 4 + 4 + 1 + 4 + 24 + 24 + 4 + 4 + 48 + 4 + 1 + 4 + 4 + 4 + 8 + 4 + 1 + 2;

    // Check total layers == files.data.size()
    size_t total_layers_offset = 4 + 8 + 32 + 24 + 24 + 32 + 32 + 32 + 6 + 2*116*116 + 2 + 2*290*290 + 2;
    REQUIRE(data.size() >= total_layers_offset + 4);
    uint32_t total_layers = read_be<uint32_t>(data.data() + total_layers_offset);
    REQUIRE(total_layers == sla_result->files.data.size());

    // Check X/Y resolution == display_pixels_x/y
    size_t res_offset = total_layers_offset + 4;
    REQUIRE(data.size() >= res_offset + 4);
    uint16_t x_res = read_be<uint16_t>(data.data() + res_offset);
    uint16_t y_res = read_be<uint16_t>(data.data() + res_offset + 2);
    REQUIRE(x_res == 2560);
    REQUIRE(y_res == 1440);

    // Check layer thickness == layer_height
    // X/Y resolution (2+2), X/Y mirror (1+1), platform X/Y/Z floats (3*4).
    size_t layer_thickness_offset = res_offset + 4 + 2 + 12;
    REQUIRE(data.size() >= layer_thickness_offset + 4);
    float layer_thickness;
    uint32_t layer_thickness_bits = read_be<uint32_t>(data.data() + layer_thickness_offset);
    std::memcpy(&layer_thickness, &layer_thickness_bits, sizeof(float));
    REQUIRE(layer_thickness == Catch::Approx(0.05f));

    // After layer_thickness come common_exposure_time, exposure_delay_mode, turn_off_time, the
    // six bottom and normal lift/retract times and bottom_exposure_time, then bottom_layers.
    size_t bottom_layers_offset = layer_thickness_offset + 4 + 4 + 1 + 4 + 6 * 4 + 4;
    REQUIRE(data.size() >= bottom_layers_offset + 4);
    // The engine has no burn-in of its own, it fades the exposure over the 10 transition layers
    // starting with the first one, so that is 11 layers at the bottom exposure time.
    REQUIRE(read_be<uint32_t>(data.data() + bottom_layers_offset) == 11);

    // The layer separation of the resin, all in the units of the settings: the six waits in s, the
    // lift and retract distances in mm (there is no retract distance setting, so the plate returns
    // over the lift distance), the speeds in mm/s and the two light PWMs in 0-255.
    const auto f = [&data](size_t offset) {
        uint32_t bits = read_be<uint32_t>(data.data() + offset);
        float value = 0.f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    };
    // After bottom_layers: bottom_lift_distance, bottom_lift_speed, lift_distance, lift_speed,
    // bottom_retract_distance, bottom_retract_speed, retract_distance, retract_speed, then the two
    // second-stage distances and speeds, then the two PWMs. The bottom exposure time comes before
    // bottom_layers and is already part of bottom_layers_offset.
    size_t motion = bottom_layers_offset + 4;
    REQUIRE(f(motion + 0) == Catch::Approx(9.0f));    // bottom_lift_distance, bottom_lift_height
    REQUIRE(f(motion + 4) == Catch::Approx(1.75f));   // bottom_lift_speed
    REQUIRE(f(motion + 8) == Catch::Approx(7.5f));    // lift_distance, lift_height
    REQUIRE(f(motion + 12) == Catch::Approx(1.25f));  // lift_speed
    REQUIRE(f(motion + 16) == Catch::Approx(9.0f));   // bottom_retract_distance == bottom lift
    REQUIRE(f(motion + 20) == Catch::Approx(2.75f));  // bottom_retract_speed
    REQUIRE(f(motion + 24) == Catch::Approx(7.5f));   // retract_distance == lift distance
    REQUIRE(f(motion + 28) == Catch::Approx(2.5f));   // retract_speed
    // The second stage is off unless the *_2 settings say otherwise, so all eight of its fields
    // (bottom and normal, lift and retract, distance and speed) are 0 here.
    for (size_t i = 0; i < 8; ++i) {
        INFO("second stage field " << i);
        REQUIRE(f(motion + 32 + i * 4) == Catch::Approx(0.0f));
    }
    REQUIRE(read_be<int16_t>(data.data() + motion + 64) == 200); // bottom_light_pwm
    REQUIRE(read_be<int16_t>(data.data() + motion + 66) == 128); // light_pwm

    // The six waits around the separation come before the bottom exposure time: the bottom ones
    // first, then the normal ones.
    size_t waits = layer_thickness_offset + 4 + 4 + 1 + 4;
    REQUIRE(f(waits + 0) == Catch::Approx(4.0f));  // bottom_before_lift_time
    REQUIRE(f(waits + 4) == Catch::Approx(0.7f));  // bottom_after_lift_time
    REQUIRE(f(waits + 8) == Catch::Approx(0.8f));  // bottom_after_retract_time
    REQUIRE(f(waits + 12) == Catch::Approx(3.0f)); // before_lift_time
    REQUIRE(f(waits + 16) == Catch::Approx(0.4f)); // after_lift_time
    REQUIRE(f(waits + 20) == Catch::Approx(0.6f)); // after_retract_time

    // transition_layers is the last field of the header, after layer_content_offset and
    // gray_scale_level, and the fade itself comes from faded_layers.
    size_t header_size = bottom_layers_offset + 4 + 16 * 4 + 2 * 2 + 1 + 4 + 3 * 4 + 8 + 4 + 1 + 2;
    REQUIRE(data.size() >= header_size);
    REQUIRE(read_be<uint16_t>(data.data() + header_size - 2) == 10);

    // Check ending string at end of file
    REQUIRE(data.size() >= 11);
    std::string ending(reinterpret_cast<const char*>(data.data() + data.size() - 11), 11);
    REQUIRE(ending == GOO_ENDING);
}

// The raft interface is the band of layers at the top of the raft that the file gives an exposure
// of its own. The raft is 2 mm of wall at a layer height of 0.05 mm, so it covers 40 layers and an
// interface of 0.5 mm is the top 10 of them. The default interface thickness is 0, which leaves
// every layer with the exposure it had before, so this writes a different file.
TEST_CASE("Goo export exposes the raft interface layers with the interface exposure",
          "[export][sla][goo][raft]")
{
    Slic3r::Test::SlaSlicingFixture fixture;

    auto model = Slic3r::Test::generate_cubes(1, 5);
    auto config = Slic3r::Domain::ConfigPackSLA{};

    config.sla_printer_settings.items.opt("sla_archive_format").set(std::string("goo"));
    config.sla_printer_settings.items.opt("display_pixels_x").set(2560);
    config.sla_printer_settings.items.opt("display_pixels_y").set(1440);
    config.sla_printer_settings.items.opt("display_orientation").set(Slic3r::Domain::SLADisplayOrientation::sladoLandscape);
    config.sla_printer_settings.items.opt("display_width").set(120.96);
    config.sla_printer_settings.items.opt("display_height").set(68.04);
    config.sla_print_settings.items.opt("layer_height").set(0.05);
    config.sla_material_settings.items.opt("initial_layer_height").set(0.05);
    config.sla_material_settings.items.opt("exposure_time").set(6.0);
    config.sla_material_settings.items.opt("initial_exposure_time").set(35.0);
    config.sla_print_settings.items.opt("faded_layers").set(10);
    config.sla_print_settings.items.opt("supports_enable").set(true);
    // The raft: 2 mm of wall, and 0.5 mm of it exposed at 12 s instead of the normal 6 s.
    config.sla_print_settings.items.opt("pad_wall_thickness").set(2.0);
    config.sla_print_settings.items.opt("pad_wall_height").set(0.0);
    config.sla_print_settings.items.opt("raft_interface_thickness").set(0.5);
    config.sla_print_settings.items.opt("raft_interface_exposure").set(12.0);

    auto sla_result = fixture.slice_sla_model(model, config);
    REQUIRE(sla_result != nullptr);

    Tests::TestTempDir temp_dir;
    fs::path out_path = temp_dir.path() / "out.goo";

    register_sla_archive_formats();
    auto& registry = SlaArchiveFormatRegistry::instance();
    auto format = registry.find_by_file_data_type(FileDataType::goo);
    REQUIRE(format != nullptr);
    REQUIRE_NOTHROW(format->store(out_path.string(), *sla_result));

    auto data = read_file_binary(out_path);
    REQUIRE(!data.empty());

    // The layer records follow the header, each of them the record itself, the image of the layer
    // and the delimiter after it. The header states where they start, which is the size the
    // packed header struct has.
    const size_t total_layers_offset = 4 + 8 + 32 + 24 + 24 + 32 + 32 + 32 + 6 + 2 * 116 * 116 + 2 + 2 * 290 * 290 + 2;
    const uint32_t total_layers = read_be<uint32_t>(data.data() + total_layers_offset);
    REQUIRE(total_layers == sla_result->files.data.size());
    REQUIRE(total_layers > 40);

    const size_t header_size = total_layers_offset + 4 + 4 + 2 + 12 + 4 + 4 + 1 + 4 + 6 * 4 + 4
                               + 4 + 16 * 4 + 2 * 2 + 1 + 4 + 3 * 4 + 8 + 4 + 1 + 2;
    REQUIRE(data.size() >= header_size);
    // layer_content_offset is the last int32 of the header but two.
    REQUIRE(read_be<uint32_t>(data.data() + header_size - 7) == header_size);

    const auto f = [&data](size_t offset) {
        const uint32_t bits = read_be<uint32_t>(data.data() + offset);
        float value = 0.f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    };

    // 2 bytes of pause flag, then the z of the layer, then the exposure of the layer.
    constexpr size_t layer_def_size    = 70;
    constexpr size_t exposure_offset   = 10;
    constexpr size_t data_size_offset  = 66;

    size_t offset = header_size;
    for (uint32_t i = 0; i < total_layers; ++i) {
        REQUIRE(data.size() >= offset + layer_def_size);
        INFO("layer " << i);
        if (i < 11)
            REQUIRE(f(offset + exposure_offset) == Catch::Approx(35.0f));
        else if (i < 30)
            REQUIRE(f(offset + exposure_offset) == Catch::Approx(6.0f));
        else if (i < 40)
            REQUIRE(f(offset + exposure_offset) == Catch::Approx(12.0f));
        else
            REQUIRE(f(offset + exposure_offset) == Catch::Approx(6.0f));

        const int32_t image_size = read_be<int32_t>(data.data() + offset + data_size_offset);
        REQUIRE(image_size >= 0);
        offset += layer_def_size + static_cast<size_t>(image_size) + 2;
    }
}
