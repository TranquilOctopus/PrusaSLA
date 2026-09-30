#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Slic3r/Biz/SlaFixture.hpp"
#include "Slic3r/Biz/ResultExport/SLA/SlaArchiveFormat.hpp"
#include "Slic3r/Biz/ResultExport/SLA/SlaExportFileTypes.hpp"
#include "libslic3r/SLAResult.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/TestUtils/TestTempDir.hpp"

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace fs = boost::filesystem;

using Slic3r::Biz::Slicing::SLAResultData;
using Slic3r::Biz::PrintHost::Sla::SlaArchiveFormatRegistry;
using Slic3r::Biz::PrintHost::Sla::register_sla_archive_formats;
using Slic3r::Biz::Slicing::Sla::FileDataType;

static std::vector<uint8_t> read_file_binary(const fs::path& path)
{
    boost::nowide::ifstream file(path.string(), std::ios::binary);
    file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

static uint32_t read_le32(const std::vector<uint8_t>& data, size_t offset)
{
    uint32_t value = 0;
    for (size_t i = 0; i < 4; ++i)
        value |= uint32_t(data.at(offset + i)) << (8 * i);
    return value;
}

static float read_le_float(const std::vector<uint8_t>& data, size_t offset)
{
    const uint32_t bits = read_le32(data, offset);
    float value = 0.f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

TEST_CASE("PM5 format registry", "[export][sla][pm5]")
{
    register_sla_archive_formats();
    auto& registry = SlaArchiveFormatRegistry::instance();

    // Registry returns a format for FileDataType::pm5
    auto format = registry.find_by_file_data_type(FileDataType::pm5);
    REQUIRE(format != nullptr);
    REQUIRE(format->name() == "PM5");
    REQUIRE(format->file_data_type() == FileDataType::pm5);

    // Extensions contain "pm5"
    auto exts = format->extensions();
    REQUIRE(std::find(exts.begin(), exts.end(), "pm5") != exts.end());
}

// The Photon Workshop family is one container with a machine name and a format version per printer,
// so the registry holds one entry per printer and each has its own file data type: the export picks
// its writer by that type, and a .pm5s file written with the M5's name would be a wrong file.
TEST_CASE("The pm5s and pm7 formats are registered next to pm5", "[export][sla][pm5][pm5s][pm7]")
{
    register_sla_archive_formats();
    auto& registry = SlaArchiveFormatRegistry::instance();

    const struct {
        const char* name;
        const char* extension;
        FileDataType type;
    } expected[] = {
        {"PM5", "pm5", FileDataType::pm5},
        {"PM5S", "pm5s", FileDataType::pm5s},
        {"PM7", "pm7", FileDataType::pm7},
    };

    for (const auto& entry : expected) {
        INFO(entry.name);
        const std::string extension(entry.extension);
        // By name and by file data type, which are the two lookups the export does.
        const std::unique_ptr<Slic3r::Biz::PrintHost::Sla::ISlaArchiveFormat> format =
            registry.get(entry.name);
        REQUIRE(format != nullptr);
        REQUIRE(format->file_data_type() == entry.type);
        REQUIRE(format->extensions().size() == 1u);
        REQUIRE(format->extensions().front() == extension);
        // The extension alone finds it too: that is what the format picker and the upload rules use.
        const std::unique_ptr<Slic3r::Biz::PrintHost::Sla::ISlaArchiveFormat> by_extension =
            registry.find_by_extension(extension);
        REQUIRE(by_extension != nullptr);
        REQUIRE(by_extension->name() == std::string(entry.name));
        REQUIRE(registry.find_by_file_data_type(entry.type) != nullptr);
        // The default extension a save dialog offers for this printer is its own.
        const std::string default_extension =
            Slic3r::Biz::PrintHost::Sla::sla_default_export_extension(extension);
        REQUIRE(default_extension == "." + extension);
    }
}

// The container layout itself is checked in detail in AnycubicExportTests.cpp; this checks the
// format registered for .pm5 now writes a file instead of refusing.
TEST_CASE("PM5 store writes a Photon Workshop version 517 file", "[export][sla][pm5]")
{
    register_sla_archive_formats();
    auto& registry = SlaArchiveFormatRegistry::instance();

    auto format = registry.find_by_file_data_type(FileDataType::pm5);
    REQUIRE(format != nullptr);

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
    // Layer separation, the settings the header takes: mm, mm/s and s.
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
    REQUIRE(sla_result->files.type == FileDataType::pm5);

    Tests::TestTempDir temp_dir;
    fs::path out_path = temp_dir.path() / "out.pm5";

    REQUIRE_NOTHROW(format->store(out_path.string(), *sla_result));
    REQUIRE(fs::exists(out_path));

    std::ifstream in(out_path.string(), std::ios::binary);
    std::vector<char> head(20);
    in.read(head.data(), static_cast<std::streamsize>(head.size()));
    REQUIRE(in.gcount() == 20);
    REQUIRE(std::string(head.data(), 12) == std::string("ANYCUBIC\0\0\0\0", 12));
    const auto u32_at = [&head](size_t o) {
        return std::uint32_t(std::uint8_t(head[o])) | (std::uint32_t(std::uint8_t(head[o + 1])) << 8) |
               (std::uint32_t(std::uint8_t(head[o + 2])) << 16) | (std::uint32_t(std::uint8_t(head[o + 3])) << 24);
    };
    REQUIRE(u32_at(12) == 517u); // format version
    REQUIRE(u32_at(16) == 9u);   // number of addresses

    // The first address is the HEADER block; the container layout is checked in detail in
    // AnycubicExportTests.cpp, this is about the values the resin puts into it.
    auto data = read_file_binary(out_path);
    REQUIRE(data.size() > 20 + 4);
    // The address table holds absolute offsets, and a block starts with its 12-byte name and a u32
    // length, so the body of the block at that address starts 16 bytes further in (0x48 here).
    const size_t header_body = read_le32(data, 20) + 12 + 4;
    REQUIRE(data.size() >= header_body + 72);
    // The light-off delay in s, the lift height in mm, the lift speed and the retract speed in mm/s
    // (pm5.md): the settings are already in those units, so they are written as they are.
    REQUIRE(read_le_float(data, header_body + 12) == Catch::Approx(3.0f));
    REQUIRE(read_le_float(data, header_body + 24) == Catch::Approx(7.5f));
    REQUIRE(read_le_float(data, header_body + 28) == Catch::Approx(1.25f));
    REQUIRE(read_le_float(data, header_body + 32) == Catch::Approx(2.5f));

    // The LAYERDEF entries (the fifth slot of the address table) carry the lift height and speed of
    // each layer, the bottom_* settings on the bottom layers. The body opens with the layer count,
    // so the entries follow it. 11 layers are at the bottom exposure time.
    const size_t layerdef_body = read_le32(data, 20 + 4 * 4) + 12 + 4 + 4;
    const uint32_t layer_count = read_le32(data, layerdef_body - 4);
    REQUIRE(layer_count > 11);
    REQUIRE(data.size() >= layerdef_body + layer_count * 32);
    for (uint32_t i = 0; i < layer_count; ++i) {
        const size_t entry = layerdef_body + i * 32;
        const bool bottom = i < 11;
        INFO("layer " << i);
        REQUIRE(read_le_float(data, entry + 8) == Catch::Approx(bottom ? 9.0f : 7.5f));
        REQUIRE(read_le_float(data, entry + 12) == Catch::Approx(bottom ? 1.75f : 1.25f));
    }

    // The estimated print time in whole seconds at HEADER +68 (pm5.md), the shared MSLA estimate
    // of M1.11c: every layer spends its exposure, its light-off waits and its lift and retract,
    // the burn-in layers on the bottom_* values. This test sets no wait but the one before the
    // lift and no bottom retract speed, so a bottom layer pays 35 s of exposure and its 9 mm lift
    // at 1.75 mm/s, and a normal one 6 s of exposure, the 3 s wait and 7.5 mm up and back down.
    // See doc/sla-fork/profiling/print-time.md.
    const double bottom_layer_s    = 35. + 9. / 1.75;
    const double normal_layer_s    = 6. + 3. + 7.5 / 1.25 + 7.5 / 2.5;
    const double expected_print_time_s = 11. * bottom_layer_s
        + (double(layer_count) - 11.) * normal_layer_s;
    REQUIRE(read_le32(data, header_body + 68) == std::uint32_t(expected_print_time_s));
}

// The raft interface is the band of layers at the top of the raft that the file gives an exposure
// of its own. The raft is 2 mm of wall at a layer height of 0.05 mm, so it covers 40 layers and an
// interface of 0.5 mm is the top 10 of them. The default interface thickness is 0, which leaves
// every layer with the exposure it had before, so this writes a different file.
TEST_CASE("PM5 export exposes the raft interface layers with the interface exposure",
          "[export][sla][pm5][raft]")
{
    register_sla_archive_formats();
    auto& registry = SlaArchiveFormatRegistry::instance();

    auto format = registry.find_by_file_data_type(FileDataType::pm5);
    REQUIRE(format != nullptr);

    Slic3r::Test::SlaSlicingFixture fixture;
    auto model = Slic3r::Test::generate_cubes(1, 5);
    auto config = Slic3r::Domain::ConfigPackSLA{};

    config.sla_printer_settings.items.opt("sla_archive_format").set(std::string("pm5"));
    config.sla_printer_settings.items.opt("display_pixels_x").set(2560);
    config.sla_printer_settings.items.opt("display_pixels_y").set(1440);
    config.sla_printer_settings.items.opt("display_orientation").set(Slic3r::Domain::SLADisplayOrientation::sladoLandscape);
    config.sla_printer_settings.items.opt("display_width").set(120.96);
    config.sla_printer_settings.items.opt("display_height").set(68.04);
    config.sla_printer_settings.items.opt("gamma_correction").set(1.0);
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
    fs::path out_path = temp_dir.path() / "out.pm5";
    REQUIRE_NOTHROW(format->store(out_path.string(), *sla_result));

    auto data = read_file_binary(out_path);
    const size_t layerdef_body = read_le32(data, 20 + 4 * 4) + 12 + 4 + 4;
    const uint32_t layer_count = read_le32(data, layerdef_body - 4);
    REQUIRE(layer_count > 40);
    REQUIRE(data.size() >= layerdef_body + layer_count * 32);

    for (uint32_t i = 0; i < layer_count; ++i) {
        const size_t entry = layerdef_body + i * 32;
        // The exposure of a layer is the 5th field of the entry.
        const float exposure = read_le_float(data, entry + 16);
        INFO("layer " << i);
        if (i < 11)
            REQUIRE(exposure == Catch::Approx(35.0f));
        else if (i < 30)
            REQUIRE(exposure == Catch::Approx(6.0f));
        else if (i < 40)
            REQUIRE(exposure == Catch::Approx(12.0f));
        else
            REQUIRE(exposure == Catch::Approx(6.0f));
    }
}
