#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Slic3r/Domain/ConfigDefsSLA.hpp"

#include "Slic3r/Biz/SlaFixture.hpp"
#include "Slic3r/Biz/ResultExport/SLA/SlaArchiveFormat.hpp"
#include "Slic3r/Biz/ResultExport/SLA/SlaExportFileTypes.hpp"
#include "Slic3r/Biz/ResultExport/SLA/SlaLayerDecoders.hpp"
#include "Slic3r/TestUtils/TestTempDir.hpp"

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = boost::filesystem;

using Slic3r::Biz::Slicing::SLAResultData;
using Slic3r::Biz::PrintHost::Sla::SlaArchiveFormatRegistry;
using Slic3r::Biz::PrintHost::Sla::register_sla_archive_formats;
using Slic3r::Biz::Slicing::Sla::FileDataType;
using Slic3r::Test::decode_ctb_layer;

static std::vector<uint8_t> read_file_binary(const fs::path& path)
{
    boost::nowide::ifstream file(path.string(), std::ios::binary);
    file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

// ---------------------------------------------------------------------------
// A reader for the container the writer produces, one field at a time, so a field that moves or a
// length that stops matching shows up as a wrong value instead of a diff. doc/sla-fork/formats/
// ctb.md is the field list it follows. The layer count is not a field of the file (the layer
// definitions are counted by the reader's caller, which knows how many layers were sliced), so it
// is a parameter.
// ---------------------------------------------------------------------------
namespace {

class CtbCursor
{
public:
    explicit CtbCursor(const std::vector<uint8_t>& data) : m_data(data) {}

    std::uint32_t u32()
    {
        need(4);
        const std::uint32_t value = std::uint32_t(m_data[m_pos]) | (std::uint32_t(m_data[m_pos + 1]) << 8)
            | (std::uint32_t(m_data[m_pos + 2]) << 16) | (std::uint32_t(m_data[m_pos + 3]) << 24);
        m_pos += 4;
        return value;
    }

    float f32()
    {
        const std::uint32_t bits = u32();
        float value = 0.f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    std::string bytes(std::size_t len)
    {
        need(len);
        std::string value(reinterpret_cast<const char*>(m_data.data() + m_pos), len);
        m_pos += len;
        return value;
    }

    // A fixed-size string field: the NUL padding is dropped, the rest of the field is.
    std::string fixed_string(std::size_t len)
    {
        const std::string value = bytes(len);
        const std::size_t nul = value.find('\0');
        return nul == std::string::npos ? value : value.substr(0, nul);
    }

    std::vector<std::uint8_t> raw(std::size_t len)
    {
        need(len);
        std::vector<std::uint8_t> value(m_data.begin() + m_pos, m_data.begin() + m_pos + len);
        m_pos += len;
        return value;
    }

    std::size_t position() const { return m_pos; }

private:
    void need(std::size_t len) const
    {
        if (m_pos + len > m_data.size())
            throw std::out_of_range("CtbCursor read past the end of the file");
    }

    const std::vector<std::uint8_t>& m_data;
    std::size_t m_pos{0};
};

struct CtbPreview
{
    std::uint32_t reserved{0};
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::uint32_t bytes_per_pixel{0};
    std::uint32_t x_offset{0};
    std::uint32_t y_offset{0};
    std::uint32_t type{0};
    std::vector<std::uint8_t> pixels;
};

struct CtbLayerDef
{
    std::uint32_t height_um{0};
    std::uint32_t exposure_ms{0};
    std::uint32_t before_lift_ms{0};
    std::uint32_t after_lift_ms{0};
    std::uint32_t after_retract_ms{0};
    std::uint32_t lift_um{0};
    std::uint32_t retract_um{0};
    std::uint32_t volume_mm3{0};
    std::uint32_t total_height_um{0};
    std::uint32_t data_size{0};
    std::uint32_t key{0};
    std::uint32_t reserved{0};
};

struct CtbFile
{
    std::uint32_t version{0};
    std::uint32_t reserved1{0};
    std::uint32_t reserved2{0};
    std::string software_version;
    std::string software_version_padding;
    std::string software;
    std::string software_padding;
    std::string timestamp;
    std::string timestamp_padding;
    std::string printer_name;
    std::uint32_t printer_name_padding{0};

    std::uint32_t resolution_x{0};
    std::uint32_t resolution_y{0};
    float x_offset{0.f};
    float y_offset{0.f};
    std::uint32_t mirror_x{0};
    std::uint32_t mirror_y{0};
    std::uint32_t preview_count{0};
    std::vector<CtbPreview> previews;

    float layer_thickness{0.f};
    float exposure_time{0.f};
    float bottom_exposure_time{0.f};
    std::uint32_t bottom_layers{0};
    float light_off_time{0.f};
    float before_lift_time{0.f};
    float after_lift_time{0.f};
    float after_retract_time{0.f};
    float lift_distance{0.f};
    float lift_speed{0.f};
    float retract_distance{0.f};
    float retract_speed{0.f};
    float bottom_before_lift_time{0.f};
    float bottom_after_lift_time{0.f};
    float bottom_after_retract_time{0.f};
    float bottom_lift_distance{0.f};
    float bottom_lift_speed{0.f};
    float bottom_retract_distance{0.f};
    float bottom_retract_speed{0.f};
    std::uint32_t transition_layers{0};
    std::uint32_t bottom_light_pwm{0};
    std::uint32_t light_pwm{0};
    std::uint32_t advance_mode{0};
    std::uint32_t print_time_s{0};
    float total_volume_ml{0.f};
    float total_weight_g{0.f};
    float total_price{0.f};
    std::string price_unit;
    std::uint32_t anti_aliasing{0};
    std::uint32_t parameters_reserved{0};

    std::vector<CtbLayerDef> layers;
    std::size_t layer_data_offset{0};
    std::vector<std::vector<std::uint8_t>> layer_images;
    std::uint32_t end_marker{0};
};

CtbFile parse_ctb(const std::vector<uint8_t>& data, std::size_t layer_count)
{
    CtbCursor cursor(data);
    CtbFile file;

    file.version = cursor.u32();
    file.reserved1 = cursor.u32();
    file.reserved2 = cursor.u32();
    file.software_version = cursor.fixed_string(25);
    file.software_version_padding = cursor.bytes(7);
    file.software = cursor.fixed_string(25);
    file.software_padding = cursor.bytes(7);
    file.timestamp = cursor.fixed_string(20);
    file.timestamp_padding = cursor.bytes(4);
    file.printer_name = cursor.fixed_string(32);
    file.printer_name_padding = cursor.u32();

    file.resolution_x = cursor.u32();
    file.resolution_y = cursor.u32();
    file.x_offset = cursor.f32();
    file.y_offset = cursor.f32();
    file.mirror_x = cursor.u32();
    file.mirror_y = cursor.u32();
    file.preview_count = cursor.u32();

    for (std::uint32_t i = 0; i < file.preview_count; ++i) {
        CtbPreview preview;
        preview.reserved = cursor.u32();
        preview.width = cursor.u32();
        preview.height = cursor.u32();
        preview.bytes_per_pixel = cursor.u32();
        preview.x_offset = cursor.u32();
        preview.y_offset = cursor.u32();
        preview.type = cursor.u32();
        preview.pixels = cursor.raw(size_t(preview.width) * preview.height * preview.bytes_per_pixel);
        file.previews.push_back(std::move(preview));
    }

    file.layer_thickness = cursor.f32();
    file.exposure_time = cursor.f32();
    file.bottom_exposure_time = cursor.f32();
    file.bottom_layers = cursor.u32();
    file.light_off_time = cursor.f32();
    file.before_lift_time = cursor.f32();
    file.after_lift_time = cursor.f32();
    file.after_retract_time = cursor.f32();
    file.lift_distance = cursor.f32();
    file.lift_speed = cursor.f32();
    file.retract_distance = cursor.f32();
    file.retract_speed = cursor.f32();
    file.bottom_before_lift_time = cursor.f32();
    file.bottom_after_lift_time = cursor.f32();
    file.bottom_after_retract_time = cursor.f32();
    file.bottom_lift_distance = cursor.f32();
    file.bottom_lift_speed = cursor.f32();
    file.bottom_retract_distance = cursor.f32();
    file.bottom_retract_speed = cursor.f32();
    file.transition_layers = cursor.u32();
    file.bottom_light_pwm = cursor.u32();
    file.light_pwm = cursor.u32();
    file.advance_mode = cursor.u32();
    file.print_time_s = cursor.u32();
    file.total_volume_ml = cursor.f32();
    file.total_weight_g = cursor.f32();
    file.total_price = cursor.f32();
    file.price_unit = cursor.fixed_string(8);
    file.anti_aliasing = cursor.u32();
    file.parameters_reserved = cursor.u32();

    for (std::size_t i = 0; i < layer_count; ++i) {
        CtbLayerDef layer;
        layer.height_um = cursor.u32();
        layer.exposure_ms = cursor.u32();
        layer.before_lift_ms = cursor.u32();
        layer.after_lift_ms = cursor.u32();
        layer.after_retract_ms = cursor.u32();
        layer.lift_um = cursor.u32();
        layer.retract_um = cursor.u32();
        layer.volume_mm3 = cursor.u32();
        layer.total_height_um = cursor.u32();
        layer.data_size = cursor.u32();
        layer.key = cursor.u32();
        layer.reserved = cursor.u32();
        file.layers.push_back(layer);
    }

    file.layer_data_offset = cursor.position();
    for (std::size_t i = 0; i < layer_count; ++i)
        file.layer_images.push_back(cursor.raw(file.layers[i].data_size));
    file.end_marker = cursor.u32();

    return file;
}

} // namespace

TEST_CASE("CTB format registry", "[export][sla][ctb]")
{
    register_sla_archive_formats();
    auto& registry = SlaArchiveFormatRegistry::instance();

    auto format = registry.find_by_file_data_type(FileDataType::ctb);
    REQUIRE(format != nullptr);
    REQUIRE(format->name() == "CTB");
    REQUIRE(format->file_data_type() == FileDataType::ctb);

    const auto exts = format->extensions();
    REQUIRE(std::find(exts.begin(), exts.end(), "ctb") != exts.end());

    // The format picker lists every registered format, so .ctb is offered as a file type of its own
    // next to the printer's own type.
    const auto types = Slic3r::Biz::PrintHost::Sla::sla_export_file_types("pm5");
    const auto offered = std::ranges::find_if(types, [](const auto& type) { return type.extension == "ctb"; });
    REQUIRE(offered != types.end());
    // Nothing was sliced for it here, so it must not claim to be the sliced format: choosing it
    // for a plate sliced for another format is the case the export dialog warns about.
    REQUIRE_FALSE(offered->matches_sliced_format);

    // No printer preset selects it. There is no Chitubox-sliced sample to say that a machine reads
    // the file, so no printer is pointed at it; the format is reachable through the registry, the
    // picker and a printer preset that sets sla_archive_format: ctb by hand.
    const auto by_extension = registry.find_by_extension("ctb");
    REQUIRE(by_extension != nullptr);
    REQUIRE(by_extension->file_data_type() == FileDataType::ctb);
    REQUIRE(Slic3r::Biz::PrintHost::Sla::sla_default_export_extension("ctb") == ".ctb");
}

TEST_CASE("CTB header round trip", "[export][sla][ctb]")
{
    Slic3r::Test::SlaSlicingFixture fixture;

    auto model = Slic3r::Test::generate_cubes(1, 5);
    auto config = Slic3r::Domain::ConfigPackSLA{};

    config.sla_printer_settings.items.opt("sla_archive_format").set(std::string("ctb"));
    config.sla_printer_settings.items.opt("display_pixels_x").set(800);
    config.sla_printer_settings.items.opt("display_pixels_y").set(450);
    config.sla_printer_settings.items.opt("display_orientation").set(Slic3r::Domain::SLADisplayOrientation::sladoLandscape);
    config.sla_printer_settings.items.opt("display_width").set(68.04);
    config.sla_printer_settings.items.opt("display_height").set(38.04);
    config.sla_printer_settings.items.opt("display_mirror_x").set(true);
    config.sla_printer_settings.items.opt("display_mirror_y").set(false);
    config.sla_printer_settings.items.opt("gamma_correction").set(1.0);
    config.sla_print_settings.items.opt("layer_height").set(0.05);
    // The engine slices the first layer at the resin's own initial layer height, so the first layer
    // definition has to carry it and the rest the layer height.
    config.sla_material_settings.items.opt("initial_layer_height").set(0.1);
    config.sla_material_settings.items.opt("exposure_time").set(6.0);
    config.sla_material_settings.items.opt("initial_exposure_time").set(35.0);
    config.sla_print_settings.items.opt("faded_layers").set(10);
    // Layer separation. The distances are mm, the speeds mm/s and the waits s, which is the unit of
    // the settings, so all of them go in unchanged; the two light PWMs are 0-255 as well.
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
    // No supports and no pad: every layer is a plain square of the cube, which keeps the file small.
    config.sla_print_settings.items.opt("supports_enable").set(false);
    config.sla_print_settings.items.opt("pad_enable").set(false);
    config.sla_print_settings.items.opt("raft_type").set(Slic3r::Domain::sla::RaftType::None);

    auto sla_result = fixture.slice_sla_model(model, config);
    REQUIRE(sla_result != nullptr);
    REQUIRE(sla_result->files.type == FileDataType::ctb);
    REQUIRE_FALSE(sla_result->files.data.empty());

    Tests::TestTempDir temp_dir;
    fs::path out_path = temp_dir.path() / "out.ctb";

    register_sla_archive_formats();
    auto& registry = SlaArchiveFormatRegistry::instance();
    auto format = registry.find_by_file_data_type(FileDataType::ctb);
    REQUIRE(format != nullptr);
    REQUIRE_NOTHROW(format->store(out_path.string(), *sla_result));
    REQUIRE(fs::exists(out_path));

    const auto data = read_file_binary(out_path);
    REQUIRE_FALSE(data.empty());

    const std::size_t layer_count = sla_result->files.data.size();
    CtbFile ctb;
    REQUIRE_NOTHROW(ctb = parse_ctb(data, layer_count));

    // The fixed header: the version of the unencrypted container this writer produces (3), and two
    // reserved words.
    REQUIRE(ctb.version == 3u);
    REQUIRE(ctb.reserved1 == 0u);
    REQUIRE(ctb.reserved2 == 0u);
    // The software is ours, not Chitubox: a file written here says who wrote it.
    REQUIRE(ctb.software == "PrusaSlicer");
    REQUIRE_FALSE(ctb.software_version.empty());
    REQUIRE_FALSE(ctb.software_version_padding.empty());
    REQUIRE(ctb.software_padding == std::string(7, '\0'));
    // The time stamp is "YYYY-MM-DD HH:MM:SS" in 20 bytes.
    REQUIRE(ctb.timestamp.size() == 19);
    REQUIRE(ctb.timestamp[4] == '-');
    REQUIRE(ctb.timestamp[13] == ':');
    REQUIRE(ctb.timestamp_padding == std::string(4, '\0'));
    // No key of this fork's config view states the machine name, so the field is empty rather than
    // a made-up model.
    REQUIRE(ctb.printer_name.empty());
    REQUIRE(ctb.printer_name_padding == 0u);

    // The display the layers were rasterized for, and the mirroring the printer expects.
    REQUIRE(ctb.resolution_x == 800u);
    REQUIRE(ctb.resolution_y == 450u);
    REQUIRE(ctb.x_offset == Catch::Approx(0.f));
    REQUIRE(ctb.y_offset == Catch::Approx(0.f));
    REQUIRE(ctb.mirror_x == 1u);
    REQUIRE(ctb.mirror_y == 0u);

    // Two previews: the color one the printer screen shows and a gray thumbnail. The slicing
    // fixture renders no thumbnail, so both blocks are black, but their sub-headers and their sizes
    // are checked.
    REQUIRE(ctb.preview_count == 2u);
    REQUIRE(ctb.previews.size() == 2);
    REQUIRE(ctb.previews[0].reserved == 0u);
    REQUIRE(ctb.previews[0].width == 800u);
    REQUIRE(ctb.previews[0].height == 600u);
    REQUIRE(ctb.previews[0].bytes_per_pixel == 3u);
    REQUIRE(ctb.previews[0].x_offset == 0u);
    REQUIRE(ctb.previews[0].y_offset == 0u);
    REQUIRE(ctb.previews[0].type == 0u);
    REQUIRE(ctb.previews[0].pixels.size() == 800 * 600 * 3);
    REQUIRE(ctb.previews[1].width == 400u);
    REQUIRE(ctb.previews[1].height == 300u);
    REQUIRE(ctb.previews[1].bytes_per_pixel == 1u);
    REQUIRE(ctb.previews[1].type == 1u);
    REQUIRE(ctb.previews[1].pixels.size() == 400 * 300);
    REQUIRE(std::ranges::all_of(ctb.previews[0].pixels, [](std::uint8_t pixel) { return pixel == 0; }));
    REQUIRE(std::ranges::all_of(ctb.previews[1].pixels, [](std::uint8_t pixel) { return pixel == 0; }));

    // The print parameters. The layer height is the effective one, the exposures are seconds, and
    // the bottom layer count is the fade plus the first layer the engine exposes at it.
    REQUIRE(ctb.layer_thickness == Catch::Approx(0.05f));
    REQUIRE(ctb.exposure_time == Catch::Approx(6.0f));
    REQUIRE(ctb.bottom_exposure_time == Catch::Approx(35.0f));
    REQUIRE(ctb.bottom_layers == 11u);
    REQUIRE(ctb.light_off_time == Catch::Approx(0.5f));
    REQUIRE(ctb.before_lift_time == Catch::Approx(3.0f));
    REQUIRE(ctb.after_lift_time == Catch::Approx(0.4f));
    REQUIRE(ctb.after_retract_time == Catch::Approx(0.6f));
    // There is no retract distance setting, so the plate returns over the lift distance.
    REQUIRE(ctb.lift_distance == Catch::Approx(7.5f));
    REQUIRE(ctb.lift_speed == Catch::Approx(1.25f));
    REQUIRE(ctb.retract_distance == Catch::Approx(7.5f));
    REQUIRE(ctb.retract_speed == Catch::Approx(2.5f));
    REQUIRE(ctb.bottom_before_lift_time == Catch::Approx(4.0f));
    REQUIRE(ctb.bottom_after_lift_time == Catch::Approx(0.7f));
    REQUIRE(ctb.bottom_after_retract_time == Catch::Approx(0.8f));
    REQUIRE(ctb.bottom_lift_distance == Catch::Approx(9.0f));
    REQUIRE(ctb.bottom_lift_speed == Catch::Approx(1.75f));
    REQUIRE(ctb.bottom_retract_distance == Catch::Approx(9.0f));
    REQUIRE(ctb.bottom_retract_speed == Catch::Approx(2.75f));
    REQUIRE(ctb.transition_layers == 10u);
    REQUIRE(ctb.bottom_light_pwm == 200u);
    REQUIRE(ctb.light_pwm == 128u);
    REQUIRE(ctb.advance_mode == 0u);
    // The print time is the exposure and the three waits of every layer, the bottom layers with
    // their own values, summed in whole milliseconds and rounded to seconds. The 20 mm cube is 400
    // layers at 0.05 mm, so both parts of the sum are there.
    REQUIRE(layer_count > 11);
    const std::uint32_t bottom_layer_ms = 35000u + 4000u + 700u + 800u;
    const std::uint32_t normal_layer_ms = 6000u + 3000u + 400u + 600u;
    const std::uint64_t expected_print_time_ms = 11u * std::uint64_t(bottom_layer_ms)
        + (std::uint64_t(layer_count) - 11u) * std::uint64_t(normal_layer_ms);
    REQUIRE(ctb.print_time_s == std::uint32_t((expected_print_time_ms + 500) / 1000));
    // The material fields come from the bottle settings the test set: 1 g in 1000 ml and no cost.
    REQUIRE(ctb.total_volume_ml > 0.f);
    REQUIRE(ctb.total_weight_g == Catch::Approx(ctb.total_volume_ml));
    REQUIRE(ctb.total_price == Catch::Approx(0.f));
    REQUIRE(ctb.price_unit == "USD");
    REQUIRE(ctb.anti_aliasing == 1u);
    REQUIRE(ctb.parameters_reserved == 0u);

    // One layer definition per sliced layer, in print order, and the images right behind them.
    REQUIRE(ctb.layers.size() == layer_count);
    REQUIRE(ctb.layer_images.size() == layer_count);
    REQUIRE(ctb.end_marker == 0u);

    std::size_t image_bytes = 0;
    for (std::size_t i = 0; i < layer_count; ++i) {
        const CtbLayerDef& layer = ctb.layers[i];
        INFO("layer " << i);
        const bool bottom = i < ctb.bottom_layers;
        // The first layer is the thicker one the resin asks for, the rest the print preset's.
        const std::uint32_t expected_height_um = i == 0 ? 100u : 50u;
        const std::uint32_t expected_exposure_ms = bottom ? 35000u : 6000u;
        const std::uint32_t expected_before_lift_ms = bottom ? 4000u : 3000u;
        const std::uint32_t expected_after_lift_ms = bottom ? 700u : 400u;
        const std::uint32_t expected_after_retract_ms = bottom ? 800u : 600u;
        const std::uint32_t expected_lift_um = bottom ? 9000u : 7500u;
        // Heights and distances are written in um, the exposure and the waits in ms.
        REQUIRE(layer.height_um == expected_height_um);
        REQUIRE(layer.exposure_ms == expected_exposure_ms);
        REQUIRE(layer.before_lift_ms == expected_before_lift_ms);
        REQUIRE(layer.after_lift_ms == expected_after_lift_ms);
        REQUIRE(layer.after_retract_ms == expected_after_retract_ms);
        REQUIRE(layer.lift_um == expected_lift_um);
        // The plate returns over the distance it was lifted: there is no retract distance setting.
        REQUIRE(layer.retract_um == expected_lift_um);
        // The volume of a single layer is not computed, and the key is 0: the image is stored as it
        // is, which is what an unencrypted file is.
        REQUIRE(layer.volume_mm3 == 0u);
        const std::uint32_t expected_total_height_um = i == 0 ? 100u : 100u + 50u * std::uint32_t(i);
        REQUIRE(layer.total_height_um == expected_total_height_um);
        REQUIRE(layer.key == 0u);
        REQUIRE(layer.reserved == 0u);
        // The image is the layer the rasterizer wrote, byte for byte, and its length is the one the
        // definition announces.
        REQUIRE(layer.data_size == sla_result->files.data[i].size());
        REQUIRE(ctb.layer_images[i] == sla_result->files.data[i]);
        image_bytes += layer.data_size;
    }

    // The layer data starts right behind the layer definitions, and nothing follows it but the
    // end-of-file marker.
    REQUIRE(ctb.layer_data_offset + image_bytes + 4 == data.size());

    // The images decode to the display they were rasterized for: the first layer (the thicker one
    // at the initial exposure) and a middle layer, both a full 800 x 450 pixels.
    for (const std::size_t index : {std::size_t(0), layer_count / 2}) {
        INFO("decoded layer " << index);
        const std::vector<std::uint8_t> pixels = decode_ctb_layer(ctb.layer_images[index], 800 * 450);
        REQUIRE(pixels.size() == 800 * 450);
    }
    // The cube sits in the middle of the display, so a middle layer has lit pixels and dark ones.
    const std::vector<std::uint8_t> middle = decode_ctb_layer(ctb.layer_images[layer_count / 2], 800 * 450);
    const std::size_t lit = std::size_t(std::ranges::count_if(middle, [](std::uint8_t pixel) { return pixel >= 128; }));
    INFO("lit pixels " << lit);
    REQUIRE(lit > 0);
    REQUIRE(lit < middle.size() / 2);
}

TEST_CASE("CTB layer RLE decodes a known pattern", "[export][sla][ctb]")
{
    // The two run types of the encoding, built by hand from Format/CtbSLA.cpp:
    //   control 0x00..0xFD: that many plus one pixels of the byte after it
    //   control 0xFF: a literal block, the byte after it is its length, then the raw pixels
    std::vector<std::uint8_t> encoded;
    const auto run = [&encoded](std::size_t count, std::uint8_t value) {
        encoded.push_back(std::uint8_t(count - 1));
        encoded.push_back(value);
    };
    const auto literal = [&encoded](const std::vector<std::uint8_t>& pixels) {
        encoded.push_back(0xFF);
        encoded.push_back(std::uint8_t(pixels.size()));
        encoded.insert(encoded.end(), pixels.begin(), pixels.end());
    };

    std::vector<std::uint8_t> expected;
    const auto expect = [&expected](std::size_t count, std::uint8_t value) {
        expected.insert(expected.end(), count, value);
    };

    run(1, 0x00);                // a single black pixel
    run(254, 0xFF);              // the longest run there is
    run(15, 0x40);               // a gray run
    literal({0x10, 0x20, 0x30}); // a literal block
    literal({0xFF});             // a lone white pixel, which cannot be told apart from a control
    run(3, 0x00);
    literal({0x01, 0x02, 0x03, 0x04});
    run(2, 0x10);

    expect(1, 0x00);
    expect(254, 0xFF);
    expect(15, 0x40);
    expect(1, 0x10);
    expect(1, 0x20);
    expect(1, 0x30);
    expect(1, 0xFF);
    expect(3, 0x00);
    expect(1, 0x01);
    expect(1, 0x02);
    expect(1, 0x03);
    expect(1, 0x04);
    expect(2, 0x10);

    const std::vector<std::uint8_t> decoded = decode_ctb_layer(encoded, expected.size());
    REQUIRE(decoded == expected);

    // A truncated stream decodes to what is there and never reads past the end.
    const std::vector<std::uint8_t> truncated{std::uint8_t(9), 0x00};
    const std::vector<std::uint8_t> short_decode = decode_ctb_layer(truncated, 10);
    REQUIRE(short_decode.size() == 10);
    REQUIRE(std::ranges::all_of(short_decode, [](std::uint8_t pixel) { return pixel == 0x00; }));
}
