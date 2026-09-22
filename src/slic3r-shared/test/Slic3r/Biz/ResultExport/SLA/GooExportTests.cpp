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

    // Check ending string at end of file
    REQUIRE(data.size() >= 11);
    std::string ending(reinterpret_cast<const char*>(data.data() + data.size() - 11), 11);
    REQUIRE(ending == GOO_ENDING);
}