// M4.13: does every writer describe the anti-aliasing it actually rasterized with?
//
// One slice of a 20 mm cube per format, with gamma_correction 0 (thresholded, AA off) and 1
// (anti-aliased), exported to each of the five rasterized containers. The rasterizer is the same
// for all of them (sla::create_raster_grayscale_aa, keyed off gamma_correction), so the interesting
// part is what each header says and what each encoder did to the 8-bit raster afterwards.
//
// Checked, per format:
//   - a thresholded layer decodes to exactly two greys (0 and 255), an anti-aliased one to more;
//   - every decoded value is on the quantization grid that format's encoder uses;
//   - the AA / level-count field the header declares, at the offset the format documents: the
//     "no anti-aliasing" value of that field when gamma_correction is 0 (M4.13b), and the value
//     the writer has always written when it is above 0.
//
// See doc/sla-fork/profiling/aa-and-z-correction.md for the offsets, and SlaAntiAliasing.hpp for
// the two values a header states per container. The no-anti-aliasing values are unverified: no
// printer sample settles what a firmware reads these fields as.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "Slic3r/Biz/ResultExport/SLA/SlaArchiveFormat.hpp"
#include "Slic3r/Biz/ResultExport/SLA/SlaLayerDecoders.hpp"
#include "Slic3r/Biz/SlaFixture.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/TestUtils/TestTempDir.hpp"

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fs = boost::filesystem;

using Slic3r::Test::Sla::decode_ctb_layer;
using Slic3r::Test::Sla::decode_goo_layer;
using Slic3r::Test::Sla::decode_png_layer;
using Slic3r::Test::Sla::decode_pw0_layer;
using Slic3r::Test::Sla::distinct_greys;
using Slic3r::Test::Sla::on_quantization_grid;

using Slic3r::Biz::PrintHost::Sla::SlaArchiveFormatRegistry;
using Slic3r::Biz::PrintHost::Sla::register_sla_archive_formats;
using Slic3r::Biz::Slicing::Sla::FileDataType;

namespace {

// A small display and a coarse layer height: only one layer is decoded, so the cost is in the
// slicing, and a 20 mm cube at 0.5 mm is 40 layers instead of 400.
constexpr int    DISPLAY_PIXELS_X = 320;
constexpr int    DISPLAY_PIXELS_Y = 180;
constexpr size_t LAYER_PIXELS     = size_t(DISPLAY_PIXELS_X) * DISPLAY_PIXELS_Y;

// The display in mm, 68.04 x 38.04 as the other export tests use. The size is not free: a pixel has
// to be a size the 20 mm cube of generate_cubes() does not divide into a whole number of pixels,
// because agg antialiasing is per pixel coverage and a polygon edge that falls exactly on a pixel
// boundary covers every pixel it touches either wholly or not at all. On a 64 x 36 mm display of
// 320 x 180 pixels, where a pixel is exactly 0.2 mm, the cube is exactly 100 pixels wide and its
// raster is 0 or 255 whatever gamma_correction says, which is what an anti-aliased file must not
// look like. These pixels are 0.213 x 0.211 mm, so the edges of the cube cross them.
constexpr double DISPLAY_WIDTH  = 68.04;
constexpr double DISPLAY_HEIGHT = 38.04;

// Body offsets of the fields under test, counted from the first byte after a section's 12-byte tag
// and 4-byte declared length (pm5.md, and the writers themselves).
constexpr size_t PM5_LEVELS_OFFSET      = 40; // u32 grey level count
constexpr size_t ANYCUBIC_AA_OFFSET    = 40; // u32 antialiasing flag
constexpr size_t GOO_AA_OFFSET         = 188; // int16 big endian
constexpr size_t GOO_GREY_LEVEL_OFFSET = 190; // int16 big endian

// The .ctb container has no tag or address table, so its anti-aliasing flag sits at a constant
// offset from the first byte of the file (store_ctb, CtbSLA.cpp): three words of version and
// padding, the two 25-byte software strings with 7 bytes of padding each, a 20-byte time stamp
// with 4 bytes of padding, a 32-byte printer name with 4 bytes of padding, then seven words of
// resolution, mirroring and preview count. What follows is the two fixed-size previews and the
// 27 print parameters, with the 8-byte price unit as the last of them. A preview carries a SEVEN
// word sub-header (write_preview): reserved, width, height, bytes per pixel, the two offsets and
// the type, and the pixels start at +28 (ctb.md), so each preview is 8 bytes longer than a
// six-word count makes it and both of them together put the flag 8 bytes further out.
constexpr size_t CTB_HEADER_BYTES  = 12 + (25 + 7) + (25 + 7) + (20 + 4) + (32 + 4) + 7 * 4;
constexpr size_t CTB_PREVIEW_WORDS = 7; // the sub-header write_preview puts in front of the pixels
constexpr size_t CTB_PARAM_WORDS  = 29;
constexpr size_t CTB_AA_OFFSET    = CTB_HEADER_BYTES + 2 * CTB_PREVIEW_WORDS * 4 + 800 * 600 * 3
                        + 400 * 300 + CTB_PARAM_WORDS * 4;

std::vector<uint8_t> read_file_binary(const fs::path& path)
{
    boost::nowide::ifstream file(path.string(), std::ios::binary);
    file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

uint32_t read_le32(const std::vector<uint8_t>& data, size_t offset)
{
    uint32_t value = 0;
    for (size_t i = 0; i < 4; ++i) value |= uint32_t(data.at(offset + i)) << (8 * i);
    return value;
}

int16_t read_be16(const std::vector<uint8_t>& data, size_t offset)
{
    return int16_t((uint16_t(data.at(offset)) << 8) | uint16_t(data.at(offset + 1)));
}

/// The body of the block an intro address table entry points at: a section starts with a 12-byte
/// tag and a 4-byte declared length, and the fields follow those.
size_t section_body(const std::vector<uint8_t>& data, size_t address)
{
    return address + 12 + 4;
}

/// Slice one 20 mm cube into `format` with the given gamma and store the file. The layer images
/// are small enough to decode in full, so the test decodes the middle one out of the slicing
/// result, which is what gets written into the container.
struct ExportedCube {
    fs::path             path;
    std::vector<uint8_t> bytes;
    FileDataType         type  = FileDataType::other;
    std::vector<uint8_t> layer; // one decoded layer, the middle one
    std::set<uint8_t>    greys; // its distinct values
};

/// The (step, top) of the grid the format's encoder can produce. The two RLE encoders that
/// quantize differ: pw0 spreads 16 levels over the whole range in steps of 17, goo steps by 16 and
/// writes the top nibble as 255. A .ctb or .sl1 layer keeps the rasterizer's own 8 bits, so
/// anything goes.
std::pair<int, int> quantization_grid(FileDataType type)
{
    switch (type) {
    case FileDataType::anycubic:
    // The Photon Workshop container, all three of its variants, encodes layers as PW0 runs.
    case FileDataType::pm5:
    case FileDataType::pm5s:
    case FileDataType::pm7: return {17, 255};
    case FileDataType::goo: return {16, 255};
    case FileDataType::ctb:
    case FileDataType::sl1_png:
    default: return {1, 255};
    }
}

ExportedCube export_cube(const std::string& format, double gamma_correction)
{
    Slic3r::Test::SlaSlicingFixture fixture;

    auto model = Slic3r::Test::generate_cubes(1, 5);
    auto config = Slic3r::Domain::ConfigPackSLA{};

    config.sla_printer_settings.items.opt("sla_archive_format").set(format);
    config.sla_printer_settings.items.opt("display_pixels_x").set(DISPLAY_PIXELS_X);
    config.sla_printer_settings.items.opt("display_pixels_y").set(DISPLAY_PIXELS_Y);
    config.sla_printer_settings.items.opt("display_orientation").set(
        Slic3r::Domain::SLADisplayOrientation::sladoLandscape);
    config.sla_printer_settings.items.opt("display_width").set(DISPLAY_WIDTH);
    config.sla_printer_settings.items.opt("display_height").set(DISPLAY_HEIGHT);
    config.sla_printer_settings.items.opt("display_mirror_x").set(false);
    config.sla_printer_settings.items.opt("display_mirror_y").set(false);
    // 0 thresholds the raster, 1 anti-aliases it. Nothing in between: the test is about the two
    // ends, and a gamma in between still produces an 8-bit AA raster.
    config.sla_printer_settings.items.opt("gamma_correction").set(gamma_correction);
    config.sla_print_settings.items.opt("layer_height").set(0.5);
    config.sla_material_settings.items.opt("initial_layer_height").set(0.5);
    // No supports and no raft: every layer is then a plain square, so the pixels under test are
    // the object's own and not a support pillar's. raft_type has to be cleared as well, it wins
    // over pad_enable (is_pad_enabled, SLAPrint.cpp:104).
    config.sla_print_settings.items.opt("supports_enable").set(false);
    config.sla_print_settings.items.opt("pad_enable").set(false);
    config.sla_print_settings.items.opt("raft_type").set(Slic3r::Domain::sla::RaftType::None);

    auto sla_result = fixture.slice_sla_model(model, config);
    REQUIRE(sla_result != nullptr);
    REQUIRE_FALSE(sla_result->files.data.empty());

    register_sla_archive_formats();
    auto format_entry =
        SlaArchiveFormatRegistry::instance().find_by_file_data_type(sla_result->files.type);
    REQUIRE(format_entry != nullptr);
    REQUIRE_FALSE(format_entry->extensions().empty());

    Tests::TestTempDir temp_dir;
    ExportedCube out;
    out.type = sla_result->files.type;
    out.path = temp_dir.path() / ("out." + format_entry->extensions().front());
    REQUIRE_NOTHROW(format_entry->store(out.path.string(), *sla_result));
    out.bytes = read_file_binary(out.path);
    REQUIRE_FALSE(out.bytes.empty());

    // The middle layer, so the test never looks at a first layer a fade or a raft touched.
    const std::vector<uint8_t>& middle = sla_result->files.data[sla_result->files.data.size() / 2];
    if (out.type == FileDataType::goo) {
        out.layer = decode_goo_layer(middle, LAYER_PIXELS);
    } else if (out.type == FileDataType::anycubic || out.type == FileDataType::pm5
               || out.type == FileDataType::pm5s || out.type == FileDataType::pm7) {
        out.layer = decode_pw0_layer(middle, LAYER_PIXELS);
    } else if (out.type == FileDataType::ctb) {
        out.layer = decode_ctb_layer(middle, LAYER_PIXELS);
    } else {
        out.layer = decode_png_layer(middle).pixels;
    }
    out.greys = distinct_greys(out.layer);
    return out;
}

/// The decoded layer covers the whole display and has something on it, anti-aliased or not.
void require_whole_layer(const ExportedCube& cube)
{
    REQUIRE(cube.layer.size() == LAYER_PIXELS);
    const size_t lit = std::count_if(cube.layer.begin(), cube.layer.end(), [](uint8_t v) { return v > 0; });
    REQUIRE(lit > 0);
}

/// A thresholded raster carries nothing but black and white. Checked one value at a time so a
/// failure says how many greys the file really had and which ones.
void require_binary(const std::set<uint8_t>& greys)
{
    INFO("distinct greys: " << greys.size());
    REQUIRE(greys.size() == 2);
    REQUIRE(greys.count(0) == 1);
    REQUIRE(greys.count(255) == 1);
}

/// The first address in the intro table is the HEADER block, in every one of these containers.
size_t header_body(const ExportedCube& cube)
{
    return section_body(cube.bytes, read_le32(cube.bytes, 20));
}

} // namespace

TEST_CASE("A thresholded layer is binary and an anti-aliased one is not", "[export][sla][aa]")
{
    const std::string format = GENERATE(std::string("sl1"), std::string("pwmx"), std::string("pm5"),
                                        std::string("pm5s"), std::string("pm7"), std::string("goo"),
                                        std::string("ctb"));
    const bool        aa_on = GENERATE(false, true);
    CAPTURE(format, aa_on);

    const ExportedCube cube = export_cube(format, aa_on ? 1.0 : 0.0);
    require_whole_layer(cube);

    if (aa_on) {
        // An 8-bit anti-aliased raster has intermediate greys along the edges of the cube. Exactly
        // two would mean the raster had been thresholded despite gamma_correction = 1.
        REQUIRE(cube.greys.size() > 2);
    } else {
        // agg's threshold gamma (.5) makes the raster binary before any encoder sees it, so a
        // thresholded file must carry nothing but 0 and 255 however it is encoded.
        require_binary(cube.greys);
    }

    // Whatever the encoder does with the levels in between, it may not invent values its own
    // quantization cannot produce.
    const auto [step, top] = quantization_grid(cube.type);
    REQUIRE(on_quantization_grid(cube.layer, step, top));
}

TEST_CASE("The pm5 levels field is the level count of the encoding, not of the raster",
          "[export][sla][aa][pm5]")
{
    // The field at body offset 40 is the grey level count (pm5.md), and the encoder keeps the top
    // nibble of every pixel, so 16 is both the declared and the achievable count.
    const ExportedCube aa = export_cube("pm5", 1.0);
    require_whole_layer(aa);
    REQUIRE(read_le32(aa.bytes, header_body(aa) + PM5_LEVELS_OFFSET) == 16u);
    REQUIRE(aa.greys.size() <= 16);
    // The layer colour table declares the same count as the header; it is the fourth address.
    REQUIRE(read_le32(aa.bytes, read_le32(aa.bytes, 20 + 4 * 3) + 4) == 16u);

    // A thresholded pm5 declares the same 16 and uses two of them. Unlike the flags of the other
    // containers this field counts the levels of the encoding, which a binary raster does not
    // change, and the colour table next to it is 16 bytes wide whatever the raster holds, so it
    // stays as it is in both cases (M4.13b, and the "not a finding" note in the review doc).
    const ExportedCube thresholded = export_cube("pm5", 0.0);
    require_whole_layer(thresholded);
    REQUIRE(read_le32(thresholded.bytes, header_body(thresholded) + PM5_LEVELS_OFFSET) == 16u);
    require_binary(thresholded.greys);
}

TEST_CASE("The Anycubic antialiasing flag follows gamma_correction",
          "[export][sla][aa][anycubic]")
{
    // store_anycubic writes the AA level of the raster, so an anti-aliased file says 1 and a
    // thresholded one 0 (AnycubicSLA.cpp, SlaAntiAliasing.hpp). The field is a flag, not a level
    // count, so the number of levels this format can carry is the encoder's 16 either way.
    const ExportedCube aa = export_cube("pwmx", 1.0);
    require_whole_layer(aa);
    REQUIRE(read_le32(aa.bytes, header_body(aa) + ANYCUBIC_AA_OFFSET) == 1u);

    const ExportedCube thresholded = export_cube("pwmx", 0.0);
    require_whole_layer(thresholded);
    REQUIRE(read_le32(thresholded.bytes, header_body(thresholded) + ANYCUBIC_AA_OFFSET) == 0u);
    require_binary(thresholded.greys);
}

TEST_CASE("The goo header states the anti-aliasing of the raster", "[export][sla][aa][goo]")
{
    // store_goo writes an anti-aliasing level and a grey depth, both of them following the raster
    // (GooSLA.cpp, SlaAntiAliasing.hpp): 1 and 4 bits for an anti-aliased layer, 0 and 1 bit for a
    // thresholded one. The 4 bits are what the nibble encoder writes 16 greys with, so the header
    // no longer claims fewer levels than the file carries, and a binary layer claims the one bit it
    // uses. Unverified: there is no .goo sample, so what a firmware makes of either field is still
    // the open half of finding 2 in the review doc.
    const ExportedCube aa = export_cube("goo", 1.0);
    require_whole_layer(aa);
    REQUIRE(read_be16(aa.bytes, GOO_AA_OFFSET) == 1);
    REQUIRE(read_be16(aa.bytes, GOO_GREY_LEVEL_OFFSET) == 4);
    REQUIRE(aa.greys.size() > 2);

    const ExportedCube thresholded = export_cube("goo", 0.0);
    require_whole_layer(thresholded);
    REQUIRE(read_be16(thresholded.bytes, GOO_AA_OFFSET) == 0);
    REQUIRE(read_be16(thresholded.bytes, GOO_GREY_LEVEL_OFFSET) == 1);
    require_binary(thresholded.greys);
}

TEST_CASE("The ctb anti-aliasing word follows gamma_correction", "[export][sla][aa][ctb]")
{
    // store_ctb writes the AA level of the raster, 1 anti-aliased and 0 thresholded (CtbSLA.cpp,
    // SlaAntiAliasing.hpp), and there is no level-count field next to it to check the encoder
    // against: the .ctb layer image is a run-length encoding of the 8-bit raster as it is
    // (CtbSLARasterEncoder), so the pixels below are the only other statement the file makes.
    const ExportedCube aa = export_cube("ctb", 1.0);
    require_whole_layer(aa);
    REQUIRE(read_le32(aa.bytes, CTB_AA_OFFSET) == 1u);
    REQUIRE(aa.greys.size() > 2);

    const ExportedCube thresholded = export_cube("ctb", 0.0);
    require_whole_layer(thresholded);
    REQUIRE(read_le32(thresholded.bytes, CTB_AA_OFFSET) == 0u);
    require_binary(thresholded.greys);
}

TEST_CASE("An sl1 layer is an 8-bit greyscale png whatever the gamma", "[export][sla][aa][sl1]")
{
    // store_sl1 writes no AA or level-count key at all (fill_iniconf, SL1.cpp:144): the printer is
    // expected to read the PNG's own bit depth, which PNGRasterEncoder always writes as 8. So the
    // only thing left to check is that both kinds of layer decode as 8-bit greyscale, which
    // png::decode_png accepts and nothing else, and that the thresholded one is binary.
    const ExportedCube aa = export_cube("sl1", 1.0);
    require_whole_layer(aa);
    REQUIRE(aa.greys.size() > 2);

    const ExportedCube thresholded = export_cube("sl1", 0.0);
    require_whole_layer(thresholded);
    require_binary(thresholded.greys);
}
