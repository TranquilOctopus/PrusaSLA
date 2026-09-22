#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/SlaFixture.hpp"
#include "Slic3r/Biz/ResultExport/SLA/SlaArchiveFormat.hpp"
#include "libslic3r/SLAResult.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/TestUtils/TestTempDir.hpp"

#include <boost/filesystem.hpp>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace fs = boost::filesystem;

using Slic3r::Biz::Slicing::SLAResultData;
using Slic3r::Biz::PrintHost::Sla::SlaArchiveFormatRegistry;
using Slic3r::Biz::PrintHost::Sla::register_sla_archive_formats;
using Slic3r::Biz::Slicing::Sla::FileDataType;

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
}