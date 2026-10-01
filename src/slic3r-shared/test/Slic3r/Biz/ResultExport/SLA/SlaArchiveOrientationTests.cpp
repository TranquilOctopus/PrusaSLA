#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/ResultExport/SLA/SlaArchiveFormat.hpp"
#include "Slic3r/Biz/ResultExport/SLA/SlaLayerDecoders.hpp"
#include "Slic3r/Biz/SlaFixture.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/Model.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using Slic3r::Biz::PrintHost::Sla::ISlaArchiveFormat;
using Slic3r::Biz::PrintHost::Sla::register_sla_archive_formats;
using Slic3r::Biz::PrintHost::Sla::SlaArchiveFormatRegistry;
using Slic3r::Biz::Slicing::Sla::FileDataType;
using Slic3r::Domain::SLADisplayOrientation;
using Slic3r::Domain::Vec3d;
using Slic3r::Test::Sla::decode_ctb_layer;
using Slic3r::Test::Sla::decode_goo_layer;
using Slic3r::Test::Sla::decode_png_layer;
using Slic3r::Test::Sla::decode_pw0_layer;
using Slic3r::Test::Sla::DecodedLayer;
using Slic3r::Test::Sla::LayerPlacement;
using Slic3r::Test::Sla::placement_of_pixels;
using Slic3r::Test::Sla::placement_of_svg;

// The display the pattern is rasterized onto. 0.2 mm pixels keep a slice cheap and the 144 x 80 mm
// rectangle has the 1.8 aspect ratio of a real landscape display.
constexpr double DISPLAY_WIDTH  = 144.;
constexpr double DISPLAY_HEIGHT = 80.;
constexpr int DISPLAY_PIXELS_X  = 720;
constexpr int DISPLAY_PIXELS_Y  = 400;

// An "F" of three rectangles, 26 x 32 mm in the plate's xy plane and 6 mm tall, flush with the
// plate corner at x = 0, y = 0 and clear of both centre lines (72 mm and 40 mm). That makes the
// corner it lands in unambiguous: every mirror and every orientation puts it in a different one.
// The spine is on the low x side and the arms point to high x, so the F reads the right way up
// in a landscape image written without mirroring.
struct PatternRectangle
{
    double x, y, size_x, size_y, height;
};

static const std::array<PatternRectangle, 3> PATTERN{{
    {0., 0., 8., 32., 6.}, // the spine
    {8., 6., 12., 6., 6.}, // the lower arm
    {8., 22., 18., 6., 6.}, // the upper arm
}};

static Slic3r::Domain::Model make_pattern_model()
{
    Slic3r::Domain::Model model;

    for (const PatternRectangle& r : PATTERN) {
        Slic3r::Domain::ModelObject* object = model.add_object();
        Slic3r::Biz::Algorithms::ModelObject::add_volume(
            object,
            Slic3r::Biz::Algorithms::TriangleMesh::make_cube(r.size_x, r.size_y, r.height)
        );
        object->add_instance();
        Slic3r::Biz::Algorithms::ModelObject::ensure_on_bed(*object);
        // The instance offset puts the rectangle where the pattern says, measured from the plate
        // corner the display is mapped to.
        object->instances[0]->set_offset(Vec3d{r.x, r.y, 0.});
    }

    return model;
}

// One display setting and the corner of the layer image the pattern has to end up in.
// -------------------------------------------------------------------------
// The raster transform is RasterBase::Trafo (libslic3r/SLA/RasterBase.hpp), which all the
// rasterizers build from the profile: the image origin is its top left corner, so mirror_y is
// always set and a profile's display_mirror_y inverts it; display_mirror_x is taken as it is in
// landscape and inverted in portrait, which also swaps the plate's x and y axes. So for a
// pattern at the plate's x = 0, y = 0 corner:
// landscape, no mirroring : column = x, row = height - y  -> bottom left
// landscape, mirror_x     : column = width - x            -> bottom right
// landscape, mirror_y     : column = x, row = y            -> top left
// landscape, both         : column = width - x, row = y    -> top right
// portrait, no mirroring  : column = width - y, row = height - x -> bottom right
// portrait, mirror_x      : column = y, row = height - x   -> bottom left
// portrait, mirror_y      : column = width - y, row = x    -> top right
// portrait, both          : column = y, row = x             -> top left
// All eight are sliced: a preset can carry any of them, so every one of them has a pinned corner.
// -------------------------------------------------------------------------
struct DisplayCase
{
    bool portrait;
    bool mirror_x;
    bool mirror_y;
    const char* corner;
};

static const std::array<DisplayCase, 8> DISPLAY_CASES{{
    {false, false, false, "bottom_left"}, // what a landscape screen wants, if it is not mirrored
    {false, true, false, "bottom_right"}, // the shipped community-sla profiles: display_mirror_x
    {false, false, true, "top_left"},
    {false, true, true, "top_right"},
    {true, false, false, "bottom_right"},
    {true, true, false, "bottom_left"}, // the Original Prusa SL1 and SL1S profiles
    {true, false, true, "top_right"},
    {true, true, true, "top_left"},
}};

enum class Corner
{
    top_left,
    top_right,
    bottom_left,
    bottom_right
};

static const char* corner_name(Corner corner)
{
    switch (corner) {
    case Corner::top_left:
        return "top_left";
    case Corner::top_right:
        return "top_right";
    case Corner::bottom_left:
        return "bottom_left";
    case Corner::bottom_right:
        return "bottom_right";
    }
    return "unknown";
}

// The corner of the image the geometry is in: the two image edges it is closest to. The pattern
// sits in one quarter of the display, so the answer is the same however big the image is.
static Corner corner_of(const LayerPlacement& placement)
{
    const bool left = placement.min_x <= placement.width - placement.max_x;
    const bool top  = placement.min_y <= placement.height - placement.max_y;
    if (top)
        return left ? Corner::top_left : Corner::top_right;
    return left ? Corner::bottom_left : Corner::bottom_right;
}

// Slice the pattern for one archive format and one display setting, and return where the written
// layer's geometry ended up. The layer bytes are the ones the writer puts in the archive (see
// SLAPrintSteps.cpp rasterize and the store_* functions), so decoding them reads the picture the
// printer's display gets.
static LayerPlacement written_layer_placement(
    const std::unique_ptr<ISlaArchiveFormat>& format,
    const std::string& archive_format,
    const DisplayCase& display
)
{
    Slic3r::Test::SlaSlicingFixture fixture;

    auto config = Slic3r::Domain::ConfigPackSLA{};
    config.sla_printer_settings.items.opt("sla_archive_format").set(archive_format);
    config.sla_printer_settings.items.opt("display_pixels_x").set(DISPLAY_PIXELS_X);
    config.sla_printer_settings.items.opt("display_pixels_y").set(DISPLAY_PIXELS_Y);
    config.sla_printer_settings.items.opt("display_width").set(DISPLAY_WIDTH);
    config.sla_printer_settings.items.opt("display_height").set(DISPLAY_HEIGHT);
    config.sla_printer_settings.items.opt("display_orientation")
        .set(
            display.portrait ?
                SLADisplayOrientation::sladoPortrait :
                SLADisplayOrientation::sladoLandscape
        );
    config.sla_printer_settings.items.opt("display_mirror_x").set(display.mirror_x);
    config.sla_printer_settings.items.opt("display_mirror_y").set(display.mirror_y);
    config.sla_printer_settings.items.opt("gamma_correction").set(1.0);
    config.sla_print_settings.items.opt("layer_height").set(0.5);
    config.sla_material_settings.items.opt("initial_layer_height").set(0.5);
    config.sla_material_settings.items.opt("exposure_time").set(6.0);
    config.sla_material_settings.items.opt("initial_exposure_time").set(35.0);
    config.sla_print_settings.items.opt("faded_layers").set(10);
    config.sla_material_settings.items.opt("bottle_weight").set(1.0);
    config.sla_material_settings.items.opt("bottle_volume").set(1000.0);
    config.sla_material_settings.items.opt("bottle_cost").set(0.0);
    // No supports and no raft: every layer is the pattern's cross section.
    config.sla_print_settings.items.opt("supports_enable").set(false);
    config.sla_print_settings.items.opt("pad_enable").set(false);
    config.sla_print_settings.items.opt("raft_type").set(Slic3r::Domain::sla::RaftType::None);

    const auto sla_result = fixture.slice_sla_model(make_pattern_model(), config);
    REQUIRE(sla_result != nullptr);
    // The archive format decides the raster path, and this is what ties a registered format to it.
    REQUIRE(sla_result->files.type == format->file_data_type());

    const std::vector<std::vector<uint8_t>>& layers = sla_result->files.data;
    REQUIRE(layers.size() > 2u);
    const std::vector<uint8_t>& layer = layers[layers.size() / 2];
    REQUIRE_FALSE(layer.empty());

    // The raster writers use the display's pixel grid as it is, the SVG writer the grid implied
    // by the output precision; portrait swaps the two in both.
    const size_t pixels_x = display.portrait ? size_t(DISPLAY_PIXELS_Y) : size_t(DISPLAY_PIXELS_X);
    const size_t pixels_y = display.portrait ? size_t(DISPLAY_PIXELS_X) : size_t(DISPLAY_PIXELS_Y);

    switch (format->file_data_type()) {
    case FileDataType::sl1_png: {
        const DecodedLayer image = decode_png_layer(layer);
        REQUIRE_FALSE(image.empty());
        REQUIRE(image.width == pixels_x);
        REQUIRE(image.height == pixels_y);
        return placement_of_pixels(image.pixels, image.width, image.height);
    }
    case FileDataType::sl1_svg:
        return placement_of_svg(layer);
    case FileDataType::anycubic:
    case FileDataType::pm5:
    case FileDataType::pm5s:
    case FileDataType::pm7:
        return placement_of_pixels(
            decode_pw0_layer(layer, pixels_x * pixels_y),
            pixels_x,
            pixels_y
        );
    case FileDataType::goo:
        return placement_of_pixels(
            decode_goo_layer(layer, pixels_x * pixels_y),
            pixels_x,
            pixels_y
        );
    case FileDataType::ctb:
        return placement_of_pixels(
            decode_ctb_layer(layer, pixels_x * pixels_y),
            pixels_x,
            pixels_y
        );
    default:
        FAIL(
            "No layer decoder for the format "
            << format->name()
            << " (file data type "
            << int(format->file_data_type())
            << ")."
        );
    }

    return LayerPlacement{};
}

TEST_CASE(
    "Every SLA archive writer puts the orientation pattern in the expected image corner",
    "[export][sla][orientation]"
)
{
    register_sla_archive_formats();
    const auto& registry = SlaArchiveFormatRegistry::instance();

    const std::vector<std::string> format_names = registry.names();
    REQUIRE_FALSE(format_names.empty());

    // Every registered format, so a new writer cannot skip this check.
    for (const std::string& format_name : format_names) {
        CAPTURE(format_name);

        const std::unique_ptr<ISlaArchiveFormat> format = registry.get(format_name);
        REQUIRE(format != nullptr);
        const std::vector<std::string> extensions = format->extensions();
        REQUIRE_FALSE(extensions.empty());
        // The engine picks the raster path from the archive format name, and the first extension
        // is the one a printer gets by default.
        const std::string archive_format = extensions.front();
        CAPTURE(archive_format);

        for (const DisplayCase& display : DISPLAY_CASES) {
            CAPTURE(display.portrait, display.mirror_x, display.mirror_y);
            CAPTURE(display.corner);

            const LayerPlacement placement =
                written_layer_placement(format, archive_format, display);
            REQUIRE(placement.valid());
            CAPTURE(placement.width, placement.height);
            CAPTURE(placement.min_x, placement.max_x, placement.min_y, placement.max_y);

            // Landscape writes an image as wide as the display, portrait as tall as it. A
            // transposed image (the bug the portrait display presets caused) fails here.
            if (display.portrait) {
                REQUIRE(placement.height > placement.width);
            } else {
                REQUIRE(placement.width > placement.height);
            }

            REQUIRE(std::string{corner_name(corner_of(placement))} == std::string{display.corner});
        }
    }
}
