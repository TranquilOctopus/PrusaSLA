#include <unordered_map>
#include <random>
#include <numeric>
#include <cstdint>
#include <algorithm>

#include "sla_test_utils.hpp"

#include <libslic3r/TriangleMeshSlicer.hpp>
#include <libslic3r/SLA/SupportTreeMesher.hpp>
#include <libslic3r/BranchingTree/PointCloud.hpp>
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Slicing/BackgroundProcess.hpp"
#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "Slic3r/Domain/Preset/SelectedPreset.hpp"
#include "Slic3r/Domain/PrinterTechnology.hpp"
#include "Slic3r/TestUtils/HwConfigUtils.hpp"
#include "libslic3r/IThumbnailImageGenerator.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/SlicingInput.hpp"

namespace BB = Slic3r::Biz::Algorithms::BoundingBox;
namespace {

const char *const BELOW_PAD_TEST_OBJECTS[] = {
    "20mm_cube.obj",
    "V.obj",
};

const char *const AROUND_PAD_TEST_OBJECTS[] = {
    "20mm_cube.obj",
    "V.obj",
    "frog_legs.obj",
    "cube_with_concave_hole_enlarged.obj",
};

const char *const SUPPORT_TEST_MODELS[] = {
    "cube_with_concave_hole_enlarged_standing.obj",
    "A_upsidedown.obj",
    "extruder_idler.obj"
};

// Local stand-in for the fff_print test helper, which is not on this target's include path.
Domain::Preset::SelectedPresetMetadata
create_dummy_selected_preset_metadata(const Domain::Preset::HwPrinterConfig& hw_config)
{
    return Domain::Preset::SelectedPresetMetadata{
        .hw_config = hw_config,
        .tools     = std::vector<Domain::Preset::EvaluatedPresetMetadata>{hw_config.tool_count},
        .materials = std::vector<Domain::Preset::EvaluatedPresetMetadata>{hw_config.material_slot_count()}
    };
}

// The tests never inspect thumbnails, so requests are dropped right away.
class ThumbnailGenerator : public Biz::Slicing::IThumbnailImageGenerator
{
    std::future<Biz::Slicing::ThumbnailImageResults> enqueue_thumbnail_requests(
        const Biz::Slicing::ThumbnailImageRequests&) override
    {
        std::promise<Biz::Slicing::ThumbnailImageResults> promise;
        promise.set_value(Biz::Slicing::ThumbnailImageResults{});
        return promise.get_future();
    }

    void handle_enqueued_requests() override {}
};

} // namespace

TEST_CASE("Flat pad geometry is valid", "[SLASupportGeneration]") {
    sla::PadConfig padcfg;
    
    // Disable wings
    padcfg.wall_height_mm = .0;
    
    for (auto &fname : BELOW_PAD_TEST_OBJECTS) test_pad(fname, padcfg);
}

TEST_CASE("WingedPadGeometryIsValid", "[SLASupportGeneration]") {
    sla::PadConfig padcfg;
    
    // Add some wings to the pad to test the cavity
    padcfg.wall_height_mm = 1.;
    
    for (auto &fname : BELOW_PAD_TEST_OBJECTS) test_pad(fname, padcfg);
}

TEST_CASE("FlatPadAroundObjectIsValid", "[SLASupportGeneration]") {
    sla::PadConfig padcfg;
    
    // Add some wings to the pad to test the cavity
    padcfg.wall_height_mm = 0.;
    // padcfg.embed_object.stick_stride_mm = 0.;
    padcfg.embed_object.enabled = true;
    padcfg.embed_object.everywhere = true;
    
    for (auto &fname : AROUND_PAD_TEST_OBJECTS) test_pad(fname, padcfg);
}

TEST_CASE("WingedPadAroundObjectIsValid", "[SLASupportGeneration]") {
    sla::PadConfig padcfg;
    
    // Add some wings to the pad to test the cavity
    padcfg.wall_height_mm = 1.;
    padcfg.embed_object.enabled = true;
    padcfg.embed_object.everywhere = true;
    
    for (auto &fname : AROUND_PAD_TEST_OBJECTS) test_pad(fname, padcfg);
}

TEST_CASE("DefaultSupports::ElevatedSupportGeometryIsValid", "[SLASupportGeneration]") {
    sla::SupportTreeConfig supportcfg;
    supportcfg.object_elevation_mm = 10.;
    
    for (auto fname : SUPPORT_TEST_MODELS) test_supports(fname, supportcfg);
}

TEST_CASE("DefaultSupports::FloorSupportGeometryIsValid", "[SLASupportGeneration]") {
    sla::SupportTreeConfig supportcfg;
    supportcfg.object_elevation_mm = 0;
    
    for (auto &fname: SUPPORT_TEST_MODELS) test_supports(fname, supportcfg);
}

TEST_CASE("DefaultSupports::ElevatedSupportsDoNotPierceModel", "[SLASupportGeneration]") {
    sla::SupportTreeConfig supportcfg;
    supportcfg.object_elevation_mm = 10.;

    for (auto fname : SUPPORT_TEST_MODELS)
        test_support_model_collision(fname, supportcfg);
}

TEST_CASE("DefaultSupports::FloorSupportsDoNotPierceModel", "[SLASupportGeneration]") {
    
    sla::SupportTreeConfig supportcfg;
    supportcfg.object_elevation_mm = 0;
    
    for (auto fname : SUPPORT_TEST_MODELS)
        test_support_model_collision(fname, supportcfg);
}

//TEST_CASE("BranchingSupports::ElevatedSupportGeometryIsValid", "[SLASupportGeneration][Branching]") {
//    sla::SupportTreeConfig supportcfg;
//    supportcfg.object_elevation_mm = 10.;
//    supportcfg.tree_type = sla::SupportTreeType::Branching;

//    for (auto fname : SUPPORT_TEST_MODELS) test_supports(fname, supportcfg);
//}

//TEST_CASE("BranchingSupports::FloorSupportGeometryIsValid", "[SLASupportGeneration][Branching]") {
//    sla::SupportTreeConfig supportcfg;
//    supportcfg.object_elevation_mm = 0;
//    supportcfg.tree_type = sla::SupportTreeType::Branching;

//    for (auto &fname: SUPPORT_TEST_MODELS) test_supports(fname, supportcfg);
//}


TEST_CASE("BranchingSupports::ElevatedSupportsDoNotPierceModel", "[SLASupportGeneration][Branching]") {

    sla::SupportTreeConfig supportcfg;
    supportcfg.object_elevation_mm = 10.;
    supportcfg.tree_type = Domain::sla::SupportTreeType::Branching;

    for (auto fname : SUPPORT_TEST_MODELS)
        test_support_model_collision(fname, supportcfg);
}

TEST_CASE("BranchingSupports::FloorSupportsDoNotPierceModel", "[SLASupportGeneration][Branching]") {

    sla::SupportTreeConfig supportcfg;
    supportcfg.object_elevation_mm = 0;
    supportcfg.tree_type = Domain::sla::SupportTreeType::Branching;

    for (auto fname : SUPPORT_TEST_MODELS)
        test_support_model_collision(fname, supportcfg);
}

TEST_CASE("InitializedRasterShouldBeNONEmpty", "[SLARasterOutput]") {
    // Default Prusa SL1 display parameters
    sla::Resolution res{2560, 1440};
    sla::PixelDim   pixdim{120. / res.width_px, 68. / res.height_px};
    
    sla::RasterGrayscaleAAGammaPower raster(res, pixdim, {}, 1.);
    REQUIRE(raster.resolution().width_px == res.width_px);
    REQUIRE(raster.resolution().height_px == res.height_px);
    REQUIRE(raster.pixel_dimensions().w_mm == Approx(pixdim.w_mm));
    REQUIRE(raster.pixel_dimensions().h_mm == Approx(pixdim.h_mm));
}

TEST_CASE("MirroringShouldBeCorrect", "[SLARasterOutput]") {
    sla::RasterBase::TMirroring mirrorings[] = {sla::RasterBase::NoMirror,
                                                sla::RasterBase::MirrorX,
                                                sla::RasterBase::MirrorY,
                                                sla::RasterBase::MirrorXY};

    sla::RasterBase::Orientation orientations[] =
        {sla::RasterBase::roLandscape, sla::RasterBase::roPortrait};
    
    for (auto orientation : orientations)
        for (auto &mirror : mirrorings)
            check_raster_transformations(orientation, mirror);
}


TEST_CASE("RasterizedPolygonAreaShouldMatch", "[SLARasterOutput]") {
    double disp_w = 120., disp_h = 68.;
    sla::Resolution res{2560, 1440};
    sla::PixelDim pixdim{disp_w / res.width_px, disp_h / res.height_px};
    
    double gamma = 1.;
    sla::RasterGrayscaleAAGammaPower raster(res, pixdim, {}, gamma);
    auto bb = BoundingBox({0, 0}, {scaled(disp_w), scaled(disp_h)});
    
    ExPolygon poly = square_with_hole(10.);
    poly.translate(BB::center(bb).x(), BB::center(bb).y());
    raster.draw(poly);
    
    double a = poly.area() / (scaled<double>(1.) * scaled(1.));
    double ra = raster_white_area(raster);
    double diff = std::abs(a - ra);
    
    REQUIRE(diff <= predict_error(poly, pixdim));
    
    raster.clear();
    poly = square_with_hole(60.);
    poly.translate(BB::center(bb).x(), BB::center(bb).y());
    raster.draw(poly);
    
    a = poly.area() / (scaled<double>(1.) * scaled(1.));
    ra = raster_white_area(raster);
    diff = std::abs(a - ra);
    
    REQUIRE(diff <= predict_error(poly, pixdim));
    
    sla::RasterGrayscaleAA raster0(res, pixdim, {}, [](double) { return 0.; });
    REQUIRE(raster_pxsum(raster0) == 0);
    
    raster0.draw(poly);
    ra = raster_white_area(raster);
    REQUIRE(raster_pxsum(raster0) == 0);
}


TEST_CASE("halfcone test", "[halfcone]") {
    sla::DiffBridge br{Vec3d{1., 1., 1.}, Vec3d{10., 10., 10.}, 0.25, 0.5};

    indexed_triangle_set m = sla::get_mesh(br, 45);

    namespace triangle_mesh = Biz::Algorithms::TriangleMesh;
    triangle_mesh::its_merge_vertices(m);
    its_write_obj(m, "Halfcone.obj");
}

namespace {

// Slice a 20 mm cube with the given print preset and resin layer height, and return the
// distance between the first two slice levels.
float sliced_level_distance(double print_layer_height, double resin_layer_height)
{
    using namespace Slic3r;

    // Create a simple 20mm cube model
    Domain::TriangleMesh mesh = Biz::Algorithms::TriangleMesh::make_cube(20.0, 20.0, 20.0);
    Domain::Model model;
    Domain::ModelObject* object = model.add_object();
    object->name = "cube.stl";
    Biz::Algorithms::ModelObject::add_volume(object, mesh);
    object->add_instance();

    // Setup bed
    Domain::Bed model_bed;
    Domain::BedInstance bed_instance{model_bed};
    for (const Domain::ModelObject* obj : model.objects) {
        for (Domain::ModelInstance* inst : obj->instances) {
            bed_instance.model_instances.push_back(inst);
        }
    }

    // Config with an unset initial layer height, so the first layer falls back to the
    // effective (resin aware) layer height.
    Domain::ConfigPackSLA config;
    config.sla_print_settings.items.opt("layer_height").set<double>(print_layer_height);
    config.sla_material_settings.items.opt("initial_layer_height").set<double>(0.0);
    config.sla_material_settings.items.opt("resin_layer_height").set<double>(resin_layer_height);

    auto hw_config = Test::create_dummy_hw_config(1, 0, Domain::PrinterTechnology::SLA);
    auto preset_metadata = create_dummy_selected_preset_metadata(hw_config);
    auto metadata = Biz::Slicing::build_gcode_metadata({}, preset_metadata, config);

    SLAPrint print{[](Biz::Slicing::SLAResult&&) {}, [](const Biz::Slicing::Sla::Object&) {}};
    print.update(model, config, bed_instance, preset_metadata,
                 Biz::Slicing::build_metadata_serializer(metadata, preset_metadata, config));

    // Slice the model
    ThumbnailGenerator thumbnail_generator{};
    print.slice(Domain::SlicingId{0, 0}, thumbnail_generator, std::nullopt);

    // Get the slice levels from the first object
    REQUIRE(print.m_objects.size() == 1);
    const SLAPrint::PrintLayers& layers = print.print_layers();
    REQUIRE(layers.size() >= 2);
    REQUIRE(layers[0].slices().size() == 1);
    REQUIRE(layers[1].slices().size() == 1);

    float level0 = layers[0].slices().front().get().slice_level();
    float level1 = layers[1].slices().front().get().slice_level();
    return level1 - level0;
}

} // namespace

TEST_CASE("Initial layer height zero uses layer height", "[SLAInitialLayerHeight]") {
    REQUIRE(sliced_level_distance(0.03, 0.) == Approx(0.03).margin(1e-4));
}

TEST_CASE("Resin layer height overrides the print layer height", "[SLAResinLayerHeight]") {
    // The resin layer height wins on every SLA layer height read.
    REQUIRE(sliced_level_distance(0.05, 0.03) == Approx(0.03).margin(1e-4));
    // A resin without a layer height of its own falls back to the print preset.
    REQUIRE(sliced_level_distance(0.05, 0.) == Approx(0.05).margin(1e-4));
}
