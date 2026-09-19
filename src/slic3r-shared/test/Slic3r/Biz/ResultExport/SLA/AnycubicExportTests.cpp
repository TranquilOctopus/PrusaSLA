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
    config.sla_printer_settings.items.opt("layer_height").set(0.05);
    config.sla_printer_settings.items.opt("initial_layer_height").set(0.05);
    config.sla_printer_settings.items.opt("exposure_time").set(6.0);
    config.sla_printer_settings.items.opt("initial_exposure_time").set(35.0);
    config.sla_printer_settings.items.opt("faded_layers").set(10);
    config.sla_printer_settings.items.opt("bottle_weight").set(1.0);
    config.sla_printer_settings.items.opt("bottle_volume").set(1000.0);
    config.sla_printer_settings.items.opt("bottle_cost").set(0.0);
    config.sla_print_settings.items.opt("supports_enable").set(true);

    auto sla_result = fixture.slice_sla_model(model, config);
    REQUIRE(sla_result != nullptr);

    REQUIRE(sla_result->files.type == FileDataType::anycubic);

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
    REQUIRE(magic == "ANYCUBIC\0\0\0\0");

    REQUIRE(data.size() >= 16);
    uint32_t version = read_le<uint32_t>(data.data() + 12);
    REQUIRE(version == 1);

    REQUIRE(data.size() >= 20);
    uint32_t area_num = read_le<uint32_t>(data.data() + 16);
    REQUIRE(area_num == 4);

    size_t header_offset = sizeof(uint32_t) * 3 + 12;
    REQUIRE(data.size() >= header_offset + 12);
    std::string header_tag(reinterpret_cast<const char*>(data.data() + header_offset), 12);
    REQUIRE(header_tag == "HEADER\0\0\0\0\0\0");

    size_t header_payload_offset = header_offset + 12 + 4;
    REQUIRE(data.size() >= header_payload_offset + 4);
    float pixel_size_um = *reinterpret_cast<const float*>(data.data() + header_payload_offset);
    REQUIRE(pixel_size_um == Catch::Approx(35.0f));

    REQUIRE(data.size() >= header_payload_offset + 8);
    float layer_height_mm = *reinterpret_cast<const float*>(data.data() + header_payload_offset + 4);
    REQUIRE(layer_height_mm == Catch::Approx(0.05f));

    REQUIRE(data.size() >= header_payload_offset + 48);
    uint32_t res_x = read_le<uint32_t>(data.data() + header_payload_offset + 44);
    uint32_t res_y = read_le<uint32_t>(data.data() + header_payload_offset + 48);
    REQUIRE(res_x == 2560);
    REQUIRE(res_y == 1440);

    size_t layers_header_offset = 0;
    for (size_t i = header_offset + 12; i + 12 <= data.size(); ++i) {
        std::string tag(reinterpret_cast<const char*>(data.data() + i), 12);
        if (tag == "LAYERDEF\0\0\0\0") {
            layers_header_offset = i;
            break;
        }
    }
    REQUIRE(layers_header_offset > 0);

    REQUIRE(data.size() >= layers_header_offset + 12 + 4 + 4);
    uint32_t layer_count = read_le<uint32_t>(data.data() + layers_header_offset + 12 + 4);
    REQUIRE(layer_count == sla_result->files.data.size());
}

TEST_CASE("Anycubic RLE round trip", "[export][sla][anycubic][skip]")
{
    SKIP("AnycubicSLARasterEncoder is file-static in libslic3r's AnycubicSLA.cpp and not reachable from this test binary. Round-trip test requires exposing the encoder or a public decode function.");
}