#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/SlaFixture.hpp"
#include "Slic3r/Biz/ResultExport/SLA/SlaArchiveFormat.hpp"
#include "libslic3r/SLAResult.hpp"
#include "Slic3r/TestUtils/TestTempDir.hpp"

#include <boost/filesystem.hpp>
#include <fstream>
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

TEST_CASE("PM5 store throws UnsupportedOutputFormat", "[export][sla][pm5]")
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

    REQUIRE_THROWS_AS(format->store(out_path.string(), *sla_result), Slic3r::Biz::Slicing::Exception);
}