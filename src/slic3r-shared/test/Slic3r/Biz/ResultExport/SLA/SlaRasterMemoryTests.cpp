#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/Model.hpp"

#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/ResultExport/SLA/SlaArchiveFormat.hpp"
#include "Slic3r/Biz/SlaFixture.hpp"
#include "Slic3r/TestUtils/TestTempDir.hpp"

#include "libslic3r/SLA/RasterMemory.hpp"

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <fstream>
#include <vector>
#include <cstdint>

namespace fs = boost::filesystem;

using Slic3r::Biz::PrintHost::Sla::register_sla_archive_formats;
using Slic3r::Biz::PrintHost::Sla::SlaArchiveFormatRegistry;
using Slic3r::Biz::Slicing::Sla::FileDataType;
using Slic3r::sla::live_raw_rasters;
using Slic3r::sla::peak_live_raw_rasters;
using Slic3r::sla::raw_raster_batch_size;
using Slic3r::sla::raw_raster_byte_budget;
using Slic3r::sla::raw_raster_bytes_per_pixel;
using Slic3r::sla::reset_peak_live_raw_rasters;

namespace {

// The Photon Mono M5 panel: 59 megapixels of 8 bit grayscale per layer.
constexpr size_t M5_WIDTH_PX    = 11520;
constexpr size_t M5_HEIGHT_PX   = 5120;
constexpr size_t M5_LAYER_BYTES = M5_WIDTH_PX * M5_HEIGHT_PX * raw_raster_bytes_per_pixel();

std::vector<uint8_t> read_file_binary(const fs::path& path)
{
    boost::nowide::ifstream file(path.string(), std::ios::binary);
    file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    return std::vector<
        uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

// A cube small enough that the layer count stays low: the point of the test is the number of
// layers held in memory at once, not the number of them.
Slic3r::Domain::Model small_cube(float size_mm)
{
    using Slic3r::Biz::Algorithms::ModelObject::add_volume;
    using Slic3r::Biz::Algorithms::ModelObject::ensure_on_bed;
    namespace TriMesh = Slic3r::Biz::Algorithms::TriangleMesh;

    Slic3r::Domain::Model model;
    Slic3r::Domain::ModelObject* object = model.add_object();
    add_volume(object, TriMesh::make_cube(size_mm, size_mm, size_mm));
    object->add_instance();
    ensure_on_bed(*object);
    return model;
}

Slic3r::Domain::ConfigPackSLA m5_config()
{
    Slic3r::Domain::ConfigPackSLA config;

    config.sla_printer_settings.items.opt("sla_archive_format").set(std::string("pwmx"));
    config.sla_printer_settings.items.opt("display_pixels_x").set(11520);
    config.sla_printer_settings.items.opt("display_pixels_y").set(5120);
    config.sla_printer_settings.items.opt("display_orientation")
        .set(Slic3r::Domain::SLADisplayOrientation::sladoLandscape);
    config.sla_printer_settings.items.opt("display_width").set(302.4);
    config.sla_printer_settings.items.opt("display_height").set(134.4);
    config.sla_printer_settings.items.opt("display_mirror_x").set(true);
    config.sla_printer_settings.items.opt("display_mirror_y").set(false);
    config.sla_printer_settings.items.opt("gamma_correction").set(1.0);
    config.sla_print_settings.items.opt("layer_height").set(0.05);
    config.sla_material_settings.items.opt("initial_layer_height").set(0.05);
    config.sla_material_settings.items.opt("exposure_time").set(6.0);
    config.sla_material_settings.items.opt("initial_exposure_time").set(35.0);
    config.sla_print_settings.items.opt("faded_layers").set(10);
    config.sla_material_settings.items.opt("lift_height").set(7.5);
    config.sla_material_settings.items.opt("lift_speed").set(1.25);
    config.sla_material_settings.items.opt("retract_speed").set(2.5);
    config.sla_material_settings.items.opt("wait_before_lift").set(3.0);
    config.sla_material_settings.items.opt("bottom_lift_height").set(9.0);
    config.sla_material_settings.items.opt("bottom_lift_speed").set(1.75);
    config.sla_material_settings.items.opt("bottle_weight").set(1.0);
    config.sla_material_settings.items.opt("bottle_volume").set(1000.0);
    config.sla_material_settings.items.opt("bottle_cost").set(0.0);
    config.sla_print_settings.items.opt("supports_enable").set(false);

    return config;
}

} // namespace

TEST_CASE("SLA 12K slicing keeps only a window of raw rasters", "[sla][raster_memory][export]")
{
    REQUIRE(M5_LAYER_BYTES == 58982400);

    Slic3r::Test::SlaSlicingFixture fixture;

    // A 1 mm cube at a 0.05 mm layer height: about 20 layers, each one a 59 MB raster. Few enough
    // to slice in a test, many enough that an unbounded run would show up in the peak.
    auto model  = small_cube(1.0f);
    auto config = m5_config();
    // The count is global to the process, so start from this test's own zero.
    reset_peak_live_raw_rasters();
    auto sla_result = fixture.slice_sla_model(std::move(model), std::move(config));
    REQUIRE(sla_result != nullptr);
    REQUIRE(sla_result->files.type == FileDataType::anycubic);
    REQUIRE_FALSE(sla_result->files.data.empty());

    const size_t layer_count = sla_result->files.data.size();
    // 64 stands in for "the machine has plenty of threads", so what binds is the budget and not
    // the parallelism. The step computes the same number with the real core count.
    const size_t batch = raw_raster_batch_size(M5_LAYER_BYTES, layer_count, 64);

    // More layers than the budget allows at once, otherwise the bound below proves nothing.
    REQUIRE(layer_count > batch);
    REQUIRE(batch * M5_LAYER_BYTES <= raw_raster_byte_budget());

    // The peak the slice reached, counted by the raster itself (libslic3r/SLA/RasterMemory.hpp).
    // The step used to rasterize every layer in one unbounded parallel_for, so the peak was one
    // 59 MB raster per worker thread.
    const size_t peak = peak_live_raw_rasters();
    INFO("layers: " << layer_count << ", peak raw rasters: " << peak);
    REQUIRE(peak > 0);
    REQUIRE(peak <= batch);
    REQUIRE(peak * M5_LAYER_BYTES <= raw_raster_byte_budget());
    // Every raster is encoded and dropped inside its layer, so none of them outlives the step.
    REQUIRE(live_raw_rasters() == 0);

    // And the encoded result is still a complete, exportable print.
    for (const auto& layer : sla_result->files.data) {
        REQUIRE_FALSE(layer.empty());
        REQUIRE(layer.capacity() < 1024 * 1024);
    }

    Tests::TestTempDir temp_dir;
    fs::path out_path = temp_dir.path() / "out.pwmx";

    register_sla_archive_formats();
    auto& registry = SlaArchiveFormatRegistry::instance();
    auto format    = registry.find_by_file_data_type(FileDataType::anycubic);
    REQUIRE(format != nullptr);
    REQUIRE_NOTHROW(format->store(out_path.string(), *sla_result));

    REQUIRE(fs::exists(out_path));
    auto data = read_file_binary(out_path);
    REQUIRE(data.size() > 12);
    REQUIRE(
        std::string(reinterpret_cast<const char*>(data.data()), 12)
        == std::string("ANYCUBIC\0\0\0\0", 12)
    );
}
