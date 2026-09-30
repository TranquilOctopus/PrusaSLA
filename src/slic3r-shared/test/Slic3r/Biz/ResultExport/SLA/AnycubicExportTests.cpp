#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Slic3r/Domain/ConfigDefsSLA.hpp"

#include "Slic3r/Biz/SlaFixture.hpp"
#include "Slic3r/Biz/ResultExport/SLA/AnycubicSLA.hpp"
#include "Slic3r/Biz/ResultExport/SLA/SlaArchiveFormat.hpp"
#include "Slic3r/TestUtils/TestTempDir.hpp"

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <fstream>
#include <vector>
#include <cstdint>
#include <array>
#include <algorithm>

namespace fs = boost::filesystem;

using Slic3r::Biz::Slicing::SLAResultData;
using Slic3r::Biz::PrintHost::Sla::store_anycubic;
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
T read_le(const uint8_t* ptr)
{
    T val = 0;
    for (size_t i = 0; i < sizeof(T); ++i) {
        val |= static_cast<T>(ptr[i]) << (8 * i);
    }
    return val;
}

TEST_CASE("Anycubic pwmx export", "[export][sla][anycubic]")
{
    Slic3r::Test::SlaSlicingFixture fixture;

    auto model = Slic3r::Test::generate_cubes(1, 5);
    auto config = Slic3r::Domain::ConfigPackSLA{};

    config.sla_printer_settings.items.opt("sla_archive_format").set(std::string("pwmx"));
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
    // Layer separation. The header takes the lift distance in mm, the lift and retract speeds in
    // mm/s and the wait in s, which is the unit of every one of these settings.
    config.sla_material_settings.items.opt("lift_height").set(7.5);
    config.sla_material_settings.items.opt("lift_speed").set(1.25);
    config.sla_material_settings.items.opt("retract_speed").set(2.5);
    config.sla_material_settings.items.opt("wait_before_lift").set(3.0);
    config.sla_material_settings.items.opt("bottom_lift_height").set(9.0);
    config.sla_material_settings.items.opt("bottom_lift_speed").set(1.75);
    config.sla_material_settings.items.opt("bottle_weight").set(1.0);
    config.sla_material_settings.items.opt("bottle_volume").set(1000.0);
    config.sla_material_settings.items.opt("bottle_cost").set(0.0);
    config.sla_print_settings.items.opt("supports_enable").set(true);

    auto sla_result = fixture.slice_sla_model(model, config);
    REQUIRE(sla_result != nullptr);

    REQUIRE(sla_result->files.type == FileDataType::anycubic);

    // Every encoded layer is held in memory until export. The encoder used to reserve room for
    // the uncompressed image and keep it (about 59 MB per layer at 12K; the Photon Mono M5 uses
    // this encoder), which ran real slices out of memory. Capacity must stay near the encoded size.
    REQUIRE_FALSE(sla_result->files.data.empty());
    for (const auto& layer : sla_result->files.data)
        REQUIRE(layer.capacity() < 1024 * 1024);

    Tests::TestTempDir temp_dir;
    fs::path out_path = temp_dir.path() / "out.pwmx";

    register_sla_archive_formats();
    auto& registry = SlaArchiveFormatRegistry::instance();
    auto format = registry.find_by_file_data_type(FileDataType::anycubic);
    REQUIRE(format != nullptr);
    REQUIRE_NOTHROW(format->store(out_path.string(), *sla_result));

    REQUIRE(fs::exists(out_path));

    auto data = read_file_binary(out_path);
    REQUIRE(!data.empty());

    REQUIRE(data.size() >= 12);
    std::string magic(reinterpret_cast<const char*>(data.data()), 12);
    REQUIRE(magic == std::string("ANYCUBIC\0\0\0\0", 12));

    REQUIRE(data.size() >= 16);
    uint32_t version = read_le<uint32_t>(data.data() + 12);
    REQUIRE(version == 1);

    REQUIRE(data.size() >= 20);
    uint32_t area_num = read_le<uint32_t>(data.data() + 16);
    REQUIRE(area_num == 4);

    // The intro block stores the offset of every section; the header is not at a fixed position.
    REQUIRE(data.size() >= 24);
    size_t header_offset = read_le<uint32_t>(data.data() + 20);
    REQUIRE(data.size() >= header_offset + 12);
    std::string header_tag(reinterpret_cast<const char*>(data.data() + header_offset), 12);
    REQUIRE(header_tag == std::string("HEADER\0\0\0\0\0\0", 12));

    size_t header_payload_offset = header_offset + 12 + 4;
    REQUIRE(data.size() >= header_payload_offset + 4);
    float pixel_size_um = *reinterpret_cast<const float*>(data.data() + header_payload_offset);
    REQUIRE(pixel_size_um == Catch::Approx(35.0f));

    REQUIRE(data.size() >= header_payload_offset + 8);
    float layer_height_mm = *reinterpret_cast<const float*>(data.data() + header_payload_offset + 4);
    REQUIRE(layer_height_mm == Catch::Approx(0.05f));

    // The bottom layer count is how many layers the file exposes at the bottom exposure time. The
    // engine has no burn-in of its own, it fades the exposure over 10 transition layers starting
    // with the first one, so that is 11 layers.
    REQUIRE(data.size() >= header_payload_offset + 24);
    float bottom_layer_count = *reinterpret_cast<const float*>(data.data() + header_payload_offset + 20);
    REQUIRE(bottom_layer_count == Catch::Approx(11.0f));

    // The transition layer count is the fade itself, taken from faded_layers.
    REQUIRE(data.size() >= header_payload_offset + 76);
    uint32_t transition_layer_count = read_le<uint32_t>(data.data() + header_payload_offset + 72);
    REQUIRE(transition_layer_count == 10);

    // The separation settings of the resin reach the file: +12 is the wait before the lift in s,
    // +24 the lift distance in mm, +28 the lift speed and +32 the retract speed, both in mm/s.
    REQUIRE(data.size() >= header_payload_offset + 36);
    const auto f = [&data, header_payload_offset](size_t offset) {
        return *reinterpret_cast<const float*>(data.data() + header_payload_offset + offset);
    };
    REQUIRE(f(12) == Catch::Approx(3.0f));  // wait_before_lift
    REQUIRE(f(24) == Catch::Approx(7.5f));  // lift_height
    REQUIRE(f(28) == Catch::Approx(1.25f)); // lift_speed
    REQUIRE(f(32) == Catch::Approx(2.5f));  // retract_speed

    REQUIRE(data.size() >= header_payload_offset + 48);
    uint32_t res_x = read_le<uint32_t>(data.data() + header_payload_offset + 44);
    uint32_t res_y = read_le<uint32_t>(data.data() + header_payload_offset + 48);
    REQUIRE(res_x == 2560);
    REQUIRE(res_y == 1440);

    // Intro layout: tag(12), version, area_num, then the section offsets; layer_data_offset is the fifth.
    size_t layers_header_offset = read_le<uint32_t>(data.data() + 36);
    REQUIRE(layers_header_offset > 0);
    REQUIRE(data.size() >= layers_header_offset + 12);
    std::string layers_tag(reinterpret_cast<const char*>(data.data() + layers_header_offset), 12);
    REQUIRE(layers_tag == std::string("LAYERDEF\0\0\0\0", 12));

    REQUIRE(data.size() >= layers_header_offset + 12 + 4 + 4);
    uint32_t layer_count = read_le<uint32_t>(data.data() + layers_header_offset + 12 + 4);
    REQUIRE(layer_count == sla_result->files.data.size());

    // Every layer carries its own lift distance and lift speed, the bottom_* settings on the
    // bottom layers and the plain ones after them. 11 layers are at the bottom exposure time, the
    // engine fading the exposure over faded_layers starting with the first one.
    const size_t layer_entry_base = layers_header_offset + 12 + 4 + 4;
    const size_t layer_entry_size = 32;
    REQUIRE(data.size() >= layer_entry_base + layer_entry_size * layer_count);
    for (uint32_t i = 0; i < layer_count; ++i) {
        const size_t entry = layer_entry_base + i * layer_entry_size;
        const bool bottom = i < 11;
        INFO("layer " << i);
        REQUIRE(*reinterpret_cast<const float*>(data.data() + entry + 8)
                == Catch::Approx(bottom ? 9.0f : 7.5f));
        REQUIRE(*reinterpret_cast<const float*>(data.data() + entry + 12)
                == Catch::Approx(bottom ? 1.75f : 1.25f));
    }
}

// PW0 decoder for test verification
static std::vector<uint8_t> decode_pw0_layer(const uint8_t* data, size_t size, uint32_t expected_pixels)
{
    std::vector<uint8_t> pixels;
    pixels.reserve(expected_pixels);
    size_t i = 0;
    while (i < size && pixels.size() < expected_pixels) {
        uint8_t byte = data[i++];
        uint8_t grey = byte >> 4;
        uint8_t run_low = byte & 0x0F;
        uint32_t run_len;
        if (grey == 0x0 || grey == 0xF) {
            if (i >= size) break;
            run_len = (static_cast<uint32_t>(run_low) << 8) | data[i++];
        } else {
            run_len = run_low;
        }
        pixels.insert(pixels.end(), run_len, grey);
    }
    return pixels;
}

TEST_CASE("Anycubic PM5 export", "[export][sla][anycubic][pm5]")
{
    Slic3r::Test::SlaSlicingFixture fixture;

    auto model = Slic3r::Test::generate_cubes(1, 5);
    auto config = Slic3r::Domain::ConfigPackSLA{};

    config.sla_printer_settings.items.opt("sla_archive_format").set(std::string("pm5"));
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
    // The resin owns the transition layer count, the print preset keeps a value of its own.
    config.sla_material_settings.items.opt("resin_faded_layers").set(4);
    // Layer separation: mm, mm/s and s, the units the .pm5 header fields are in.
    config.sla_material_settings.items.opt("lift_height").set(7.5);
    config.sla_material_settings.items.opt("lift_speed").set(1.25);
    config.sla_material_settings.items.opt("retract_speed").set(2.5);
    config.sla_material_settings.items.opt("wait_before_lift").set(3.0);
    config.sla_material_settings.items.opt("bottom_lift_height").set(9.0);
    config.sla_material_settings.items.opt("bottom_lift_speed").set(1.75);
    config.sla_material_settings.items.opt("bottle_weight").set(1.0);
    config.sla_material_settings.items.opt("bottle_volume").set(1000.0);
    config.sla_material_settings.items.opt("bottle_cost").set(0.0);
    config.sla_print_settings.items.opt("supports_enable").set(true);
    config.sla_printer_settings.items.opt("max_print_height").set(200.0);

    auto sla_result = fixture.slice_sla_model(model, config);
    REQUIRE(sla_result != nullptr);

    REQUIRE(sla_result->files.type == FileDataType::pm5);

    REQUIRE_FALSE(sla_result->files.data.empty());
    for (const auto& layer : sla_result->files.data)
        REQUIRE(layer.capacity() < 1024 * 1024);

    Tests::TestTempDir temp_dir;
    fs::path out_path = temp_dir.path() / "out.pm5";

    register_sla_archive_formats();
    auto& registry = SlaArchiveFormatRegistry::instance();
    auto format = registry.find_by_file_data_type(FileDataType::pm5);
    REQUIRE(format != nullptr);
    REQUIRE_NOTHROW(format->store(out_path.string(), *sla_result));

    REQUIRE(fs::exists(out_path));

    auto data = read_file_binary(out_path);
    REQUIRE(!data.empty());

    // Magic "ANYCUBIC" + 4 NULs
    REQUIRE(data.size() >= 12);
    std::string magic(reinterpret_cast<const char*>(data.data()), 12);
    REQUIRE(magic == std::string("ANYCUBIC\0\0\0\0", 12));

    // Version 517
    REQUIRE(data.size() >= 16);
    uint32_t version = read_le<uint32_t>(data.data() + 12);
    REQUIRE(version == 517);

    // area_num 9
    REQUIRE(data.size() >= 20);
    uint32_t area_num = read_le<uint32_t>(data.data() + 16);
    REQUIRE(area_num == 9);

    // Read the 9 addresses from the intro
    REQUIRE(data.size() >= 20 + 9 * 4);
    std::array<uint32_t, 9> addrs;
    for (int i = 0; i < 9; ++i) {
        addrs[i] = read_le<uint32_t>(data.data() + 20 + i * 4);
        // Each address must be inside the file
        REQUIRE(addrs[i] < data.size());
    }

    // Address table order: header, software, preview, color_table, layerdef, extra, machine, first_layer, model
    uint32_t addr_header = addrs[0];
    uint32_t addr_software = addrs[1];
    uint32_t addr_preview = addrs[2];
    uint32_t addr_color_table = addrs[3];
    uint32_t addr_layerdef = addrs[4];
    uint32_t addr_extra = addrs[5];
    uint32_t addr_machine = addrs[6];
    uint32_t addr_first_layer = addrs[7];
    uint32_t addr_model = addrs[8];

    // Verify HEADER section
    REQUIRE(data.size() >= addr_header + 12);
    std::string header_tag(reinterpret_cast<const char*>(data.data() + addr_header), 12);
    REQUIRE(header_tag == std::string("HEADER\0\0\0\0\0\0", 12));

    REQUIRE(data.size() >= addr_header + 12 + 4);
    uint32_t header_payload_size = read_le<uint32_t>(data.data() + addr_header + 12);
    REQUIRE(header_payload_size == 92); // declared length

    size_t header_body = addr_header + 12 + 4;
    REQUIRE(data.size() >= header_body + 92);

    float pixel_size_um = *reinterpret_cast<const float*>(data.data() + header_body);
    REQUIRE(pixel_size_um == Catch::Approx(47.25f).margin(0.01f)); // 120.96mm * 1000 / 2560

    float layer_height_mm = *reinterpret_cast<const float*>(data.data() + header_body + 4);
    REQUIRE(layer_height_mm == Catch::Approx(0.05f));

    float exposure_time_s = *reinterpret_cast<const float*>(data.data() + header_body + 8);
    REQUIRE(exposure_time_s == Catch::Approx(6.0f));

    float initial_exposure_time_s = *reinterpret_cast<const float*>(data.data() + header_body + 16);
    REQUIRE(initial_exposure_time_s == Catch::Approx(35.0f));

    // HEADER +20 is the bottom layer count; +24 is the lift height (pm5.md). The engine fades the
    // exposure over the resin's 4 transition layers, starting with the first one, so the file
    // exposes 5 layers at the initial exposure time.
    float bottom_layer_count_f = *reinterpret_cast<const float*>(data.data() + header_body + 20);
    REQUIRE(bottom_layer_count_f == Catch::Approx(5.0f));

    // The separation settings of the resin reach the file: +12 is the light-off delay in s, +24
    // the lift height in mm, +28 the lift speed and +32 the retract speed, both in mm/s, so none of
    // them is converted.
    REQUIRE(data.size() >= header_body + 36);
    float wait_before_lift_s = *reinterpret_cast<const float*>(data.data() + header_body + 12);
    REQUIRE(wait_before_lift_s == Catch::Approx(3.0f));
    float lift_height_mm = *reinterpret_cast<const float*>(data.data() + header_body + 24);
    REQUIRE(lift_height_mm == Catch::Approx(7.5f));
    float lift_speed_mms = *reinterpret_cast<const float*>(data.data() + header_body + 28);
    REQUIRE(lift_speed_mms == Catch::Approx(1.25f));
    float retract_speed_mms = *reinterpret_cast<const float*>(data.data() + header_body + 32);
    REQUIRE(retract_speed_mms == Catch::Approx(2.5f));

    // HEADER +72 is the transition layer count, the fade taken from the resin.
    REQUIRE(data.size() >= header_body + 76);
    uint32_t transition_layer_count = read_le<uint32_t>(data.data() + header_body + 72);
    REQUIRE(transition_layer_count == 4);

    uint32_t res_x = read_le<uint32_t>(data.data() + header_body + 44);
    uint32_t res_y = read_le<uint32_t>(data.data() + header_body + 48);
    REQUIRE(res_x == 2560);
    REQUIRE(res_y == 1440);

    // Verify PREVIEW section
    REQUIRE(data.size() >= addr_preview + 12);
    std::string preview_tag(reinterpret_cast<const char*>(data.data() + addr_preview), 12);
    REQUIRE(preview_tag == std::string("PREVIEW\0\0\0\0\0", 12));

    REQUIRE(data.size() >= addr_preview + 12 + 4);
    uint32_t preview_payload_size = read_le<uint32_t>(data.data() + addr_preview + 12);
    REQUIRE(preview_payload_size == 75292); // declared length

    size_t preview_body = addr_preview + 12 + 4;
    REQUIRE(data.size() >= preview_body + 12);
    uint32_t prev_w = read_le<uint32_t>(data.data() + preview_body);
    uint32_t prev_dpi = read_le<uint32_t>(data.data() + preview_body + 4);
    uint32_t prev_h = read_le<uint32_t>(data.data() + preview_body + 8);
    REQUIRE(prev_w == 224);
    REQUIRE(prev_dpi == 120);
    REQUIRE(prev_h == 168);

    // Verify LAYERDEF section
    REQUIRE(data.size() >= addr_layerdef + 12);
    std::string layerdef_tag(reinterpret_cast<const char*>(data.data() + addr_layerdef), 12);
    REQUIRE(layerdef_tag == std::string("LAYERDEF\0\0\0\0", 12));

    REQUIRE(data.size() >= addr_layerdef + 12 + 4);
    uint32_t layerdef_payload_size = read_le<uint32_t>(data.data() + addr_layerdef + 12);
    uint32_t expected_layerdef_payload = 4 + static_cast<uint32_t>(sla_result->files.data.size()) * 32;
    REQUIRE(layerdef_payload_size == expected_layerdef_payload);

    REQUIRE(data.size() >= addr_layerdef + 12 + 4 + 4);
    uint32_t layer_count = read_le<uint32_t>(data.data() + addr_layerdef + 12 + 4);
    REQUIRE(layer_count == sla_result->files.data.size());

    // Verify each layer entry
    size_t layer_entry_base = addr_layerdef + 12 + 4 + 4;
    uint32_t expected_pixels_per_layer = 2560 * 1440;
    for (uint32_t i = 0; i < layer_count; ++i) {
        size_t entry_off = layer_entry_base + i * 32;
        REQUIRE(data.size() >= entry_off + 32);

        uint32_t img_offset = read_le<uint32_t>(data.data() + entry_off);
        uint32_t img_size = read_le<uint32_t>(data.data() + entry_off + 4);
        uint32_t lit_count = read_le<uint32_t>(data.data() + entry_off + 24);
        uint32_t reserved = read_le<uint32_t>(data.data() + entry_off + 28);
        REQUIRE(reserved == 0);

        // Entry +8 is the lift height in mm and +12 the lift speed in mm/s of that layer: the
        // bottom_* settings on the 5 bottom layers, the plain ones after them.
        const bool bottom = i < 5;
        INFO("layer " << i);
        REQUIRE(*reinterpret_cast<const float*>(data.data() + entry_off + 8)
                == Catch::Approx(bottom ? 9.0f : 7.5f));
        REQUIRE(*reinterpret_cast<const float*>(data.data() + entry_off + 12)
                == Catch::Approx(bottom ? 1.75f : 1.25f));

        // offset + length must be inside file
        REQUIRE(img_offset + img_size <= data.size());
        // Bytes at offset must equal result's layer data
        REQUIRE(img_size == sla_result->files.data[i].size());
        REQUIRE(std::equal(
            data.data() + img_offset,
            data.data() + img_offset + img_size,
            sla_result->files.data[i].data()
        ));

        // Decode PW0 and verify pixel count and lit count
        auto decoded = decode_pw0_layer(data.data() + img_offset, img_size, expected_pixels_per_layer);
        REQUIRE(decoded.size() == expected_pixels_per_layer);
        uint32_t decoded_lit = 0;
        for (uint8_t g : decoded) if (g != 0) ++decoded_lit;
        REQUIRE(lit_count == decoded_lit);
    }

    // Verify EXTRA section exists
    REQUIRE(data.size() >= addr_extra + 12);
    std::string extra_tag(reinterpret_cast<const char*>(data.data() + addr_extra), 12);
    REQUIRE(extra_tag == std::string("EXTRA\0\0\0\0\0\0\0", 12));

    // Verify MACHINE section contains "pw0Img"
    REQUIRE(data.size() >= addr_machine + 12);
    std::string machine_tag(reinterpret_cast<const char*>(data.data() + addr_machine), 12);
    REQUIRE(machine_tag == std::string("MACHINE\0\0\0\0\0", 12));

    size_t machine_body = addr_machine + 12 + 4;
    // Printer name at machine_body (96 bytes), then image format at machine_body + 96 (16 bytes)
    REQUIRE(data.size() >= machine_body + 96 + 16);
    std::string img_format(reinterpret_cast<const char*>(data.data() + machine_body + 96), 16);
    // Should contain "pw0Img" (NUL-padded)
    REQUIRE(img_format.find("pw0Img") == 0);

    // Physical layout, as measured in the Photon Workshop sample (doc/sla-fork/formats/pm5.md).
    // The address table alone cannot show these, and each one was wrong in the first version:
    // MACHINE's body is 140 bytes and the software block follows it directly;
    REQUIRE(read_le<uint32_t>(data.data() + machine_body + 112) == 16u);
    REQUIRE(addr_software == machine_body + 140);
    // MACHINE's maximum Z, in mm, is the printer preset's max_print_height.
    const float machine_max_z = *reinterpret_cast<const float*>(data.data() + machine_body + 128);
    REQUIRE(machine_max_z == Catch::Approx(200.0f));
    // the software block is a 32-byte name, then its own total length (164);
    REQUIRE(read_le<uint32_t>(data.data() + addr_software + 32) == 164u);
    // MODEL comes right after the software block and spans 48 bytes, and the layer images follow.
    REQUIRE(addr_model == addr_software + 164);
    REQUIRE(addr_first_layer == addr_model + 48);

    // Verify MODEL section
    REQUIRE(data.size() >= addr_model + 12);
    std::string model_tag(reinterpret_cast<const char*>(data.data() + addr_model), 12);
    REQUIRE(model_tag == std::string("MODEL\0\0\0\0\0\0\0", 12));

    REQUIRE(data.size() >= addr_model + 12 + 4);
    uint32_t model_payload_size = read_le<uint32_t>(data.data() + addr_model + 12);
    REQUIRE(model_payload_size == 0); // declared length 0

    // MODEL body has 6 floats (24 bytes) after the header
    size_t model_body = addr_model + 12 + 4;
    REQUIRE(data.size() >= model_body + 24);
    // Just verify we can read them (values are zeros since no bbox)
    for (int i = 0; i < 6; ++i) {
        float val = *reinterpret_cast<const float*>(data.data() + model_body + i * 4);
        REQUIRE(val == 0.0f); // No bounding box in result data
    }
}

TEST_CASE("Anycubic RLE round trip", "[export][sla][anycubic][skip]")
{
    SKIP("AnycubicSLARasterEncoder is file-static in libslic3r's AnycubicSLA.cpp and not reachable from this test binary. Round-trip test requires exposing the encoder or a public decode function.");
}