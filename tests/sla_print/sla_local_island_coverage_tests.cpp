// M4.3c: island coverage of the local benchmark corpus.
//
// The maintainer reported on 2026-09-28 that auto support on one of the benchmark models (a helmet)
// left an area unsupported that "should be the highest value layer to support". This test makes
// that measurable on the real models without committing them: it runs the pipeline the plater's
// auto support runs (sla::generate_support_points_for_tool with the default config) and then looks
// for the regions of the model that no support point reaches.
//
// Hidden and local only, so the corpus is never needed by the default run:
//
//     set SLA_LOCAL_SAMPLES=local-samples
//     set SLA_ISLAND_IDS=bm01,bm13
//     build-default\tests\sla_print\Release\sla_print_tests.exe "[.local]"
//
//   SLA_LOCAL_SAMPLES  the local-samples folder, which is gitignored and not part of the
//                      repository. The corpus is read from "<folder>/benchmark models/manifest.yaml",
//                      the manifest of M0.12 that the benchmark harness (M0.13) also reads.
//   SLA_ISLAND_IDS     comma separated manifest ids; unset or empty runs bm01,bm13, the two
//                      helmets of the report.
//
// The coverage rule is the one of sla_island_coverage_tests.cpp and of the benchmark harness: a
// support point covers a region when it is within 0.5 mm of the layer in Z and within
// sqrt(area_mm2) + 1 mm of the region centroid in XY, and a region below 0.1 mm2 is not a finding.
// Islands are the regions the M4.8d rule calls islands, the local minima are the rest of the
// uncovered areas whose surface opens upwards.
//
// A model is named by its manifest id and never by its file name or its folder: the corpus is not
// redistributable, so nothing here may print a path (the M0.13 rule).
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "Slic3r/Biz/Algorithms/ClipperUtils.hpp"
#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "Slic3r/Domain/ConfigCommon.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "Slic3r/Domain/PrinterTechnology.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/Domain/SlaLayerHeight.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "libslic3r/ConfigViews.hpp"
#include "libslic3r/SLA/IslandDetection.hpp"
#include "libslic3r/SLASupportTool.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"

#include "sla_bench_manifest.hpp"

namespace {

namespace ExPoly = Slic3r::Biz::Algorithms::ExPolygon;
namespace Clipper = Slic3r::Biz::Algorithms::ClipperUtils;

using Slic3r::Domain::ExPolygon;
using Slic3r::Domain::ExPolygons;
using Slic3r::Domain::SLA::SupportPoints;
using Slic3r::Domain::Vec2d;

// The corpus of M0.12 lives in this folder of the local sample tree, next to the manifest.
constexpr const char* CORPUS_FOLDER = "benchmark models";
constexpr const char* MANIFEST_FILE = "manifest.yaml";
// The two models SLA_ISLAND_IDS runs when it is unset.
constexpr const char* DEFAULT_IDS = "bm01,bm13";
// What the tests and the benchmark harness slice at, and what the benchmark config sets.
constexpr double LAYER_HEIGHT_MM = 0.05;
// Below this an area is a speck of the slicing, not something that can be supported.
constexpr double MIN_AREA_MM2 = 0.1;
// A support point covers a region within this distance in Z ...
constexpr double COVER_DZ_MM = 0.5;
// ... and within sqrt(area_mm2) + this in XY.
constexpr double COVER_RADIUS_PAD_MM = 1.0;
// A local minimum has to keep opening upwards for at least this many layers, so a single bumpy
// layer is not reported as a finding.
constexpr int LOCAL_MIN_GROWTH_LAYERS = 2;
// How far upwards the walk after a local minimum goes. A model of thousands of layers would
// otherwise turn the report into minutes of clipping, and the rule only needs the first steps.
constexpr int LOCAL_MIN_MAX_WALK = 8;

constexpr double SF = Slic3r::Biz::Algorithms::Scaling::SCALING_FACTOR;
/// 1e-6 mm2, the overlap below which two regions do not touch. The same epsilon detect_islands()
/// uses for the M4.8d rule.
constexpr double OVERLAP_EPSILON = 1e-6 * SF * SF;

/// One region of a layer that no support point reaches.
struct UncoveredRegion
{
    size_t layer_index = 0;
    double z           = 0.; // [mm], the height the layer was sliced at
    double area_mm2    = 0.;
    Vec2d  centroid    = Vec2d::Zero();
    /// Local minima only: for how many layers above this one the surface keeps growing.
    int growth_layers = 0;
};

/// Everything the test found on one model.
struct Coverage
{
    size_t                        triangles = 0;
    size_t                        support_points = 0;
    size_t                        layer_count = 0;
    size_t                        island_count = 0;
    std::vector<UncoveredRegion> uncovered_islands;
    std::vector<UncoveredRegion> local_minima;
};

double area_mm2(const ExPolygon& region)
{
    return ExPoly::area(region) * SF * SF;
}

double area_mm2(const ExPolygons& regions)
{
    return ExPoly::area(regions) * SF * SF;
}

Vec2d centroid_of(const ExPolygon& region)
{
    const Slic3r::Domain::Point c = region.contour.centroid();
    return Vec2d(double(c.x()) * SF, double(c.y()) * SF);
}

/// The coverage rule of the island coverage tests: a point counts when it is near the layer in Z
/// and within the region's own radius in XY.
bool covered(const SupportPoints& points, double z, double area, const Vec2d& centroid)
{
    const double max_xy_dist = std::sqrt(area) + COVER_RADIUS_PAD_MM;
    for (const Slic3r::Domain::SLA::SupportPoint& point : points) {
        const double dz = std::abs(double(point.pos.z()) - z);
        if (dz > COVER_DZ_MM) continue;

        const double dx = double(point.pos.x()) - centroid.x();
        const double dy = double(point.pos.y()) - centroid.y();
        if (std::sqrt(dx * dx + dy * dy) <= max_xy_dist) return true;
    }
    return false;
}

/// The M4.8d rule: a region with no overlap with the layer below is an island.
bool touches_previous_layer(const ExPolygons& below, const ExPolygon& region)
{
    return ExPoly::area(Clipper::intersection_ex(ExPolygons{region}, below)) > OVERLAP_EPSILON;
}

/// For how many layers above a region the surface keeps growing, following the region above it
/// that overlaps it the most. Zero means the surface closes upwards at this layer, which is what
/// most of a model does.
int growth_layers(const ExPolygons& layers, size_t layer, const ExPolygon& region, double area)
{
    const ExPolygon* current = &region;
    auto             extents = ExPoly::get_extents(*current);
    int              growth  = 0;

    for (size_t up = layer + 1; up < layers.size() && growth < LOCAL_MIN_MAX_WALK; ++up) {
        const ExPolygon* above      = nullptr;
        double           overlap    = 0.;
        double           above_area = 0.;

        for (const ExPolygon& candidate : layers[up]) {
            if (!extents.overlap(ExPoly::get_extents(candidate))) continue;

            const double common =
                area_mm2(Clipper::intersection_ex(ExPolygons{*current}, ExPolygons{candidate}));
            if (common <= overlap) continue;

            overlap    = common;
            above      = &candidate;
            above_area = area_mm2(candidate);
        }

        if (above == nullptr) break;   // nothing above any more, the surface closes
        if (above_area <= area) break; // and it does not grow any further

        current = above;
        extents = ExPoly::get_extents(*current);
        area    = above_area;
        ++growth;
    }

    return growth;
}

/// Every island of the M4.8d rule that no support point covers, and how many islands there were.
std::vector<UncoveredRegion> find_uncovered_islands(const ExPolygons&        layers,
                                                    const std::vector<float>& heights,
                                                    const SupportPoints&      points,
                                                    size_t&                    island_count)
{
    const std::vector<Slic3r::SLA::IslandHit> hits =
        Slic3r::SLA::detect_islands(layers, MIN_AREA_MM2);
    island_count = hits.size();

    std::vector<UncoveredRegion> uncovered;
    for (const Slic3r::SLA::IslandHit& hit : hits) {
        if (hit.layer_index >= heights.size()) continue;
        if (covered(points, heights[hit.layer_index], hit.area_mm2, hit.centroid)) continue;

        uncovered.push_back(UncoveredRegion{hit.layer_index, heights[hit.layer_index], hit.area_mm2,
                                            hit.centroid, 0});
    }
    return uncovered;
}

/// The uncovered areas the M4.8d rule does not call islands, because they do overlap the layer
/// below, but which are the lowest point of a surface that opens upwards: a support point there is
/// the one a user would call the most valuable. Reported, never asserted on. Which of them a
/// support point should reach is a question for M4.4, not a pass or a fail of this test.
std::vector<UncoveredRegion> find_local_minima(const ExPolygons&        layers,
                                               const std::vector<float>& heights,
                                               const SupportPoints&      points)
{
    std::vector<UncoveredRegion> minima;
    for (size_t layer = 1; layer + 1 < layers.size(); ++layer) {
        for (const ExPolygon& region : layers[layer]) {
            const double area = area_mm2(region);
            if (area < MIN_AREA_MM2) continue;                               // a speck, not a finding
            if (!touches_previous_layer(layers[layer - 1], region)) continue; // an island, above

            const Vec2d centroid = centroid_of(region);
            if (covered(points, heights[layer], area, centroid)) continue;

            const int growth = growth_layers(layers, layer, region, area);
            if (growth < LOCAL_MIN_GROWTH_LAYERS) continue;

            minima.push_back(UncoveredRegion{layer, heights[layer], area, centroid, growth});
        }
    }
    return minima;
}

/// Support points, then the model layers, then what the points do not cover. The same pipeline and
/// the same config as sla_benchmark_tests.cpp: as loaded, only placed onto the plate, and the
/// default SLA config with the layer height of the tests.
Coverage analyse_model(const Bench::ModelSpec& spec)
{
    Coverage out;

    Slic3r::Domain::TriangleMesh mesh =
        Bench::load_model_file(std::filesystem::path(spec.folder) / spec.file_name);
    Bench::centre_on_plate(mesh);
    out.triangles = mesh.facets_count();

    Slic3r::Domain::Model        model;
    Slic3r::Domain::ModelObject* object = model.add_object();
    object->name = spec.id;
    Slic3r::Biz::Algorithms::ModelObject::add_volume(object, mesh);
    object->add_instance();

    Slic3r::Domain::ConfigPackSLA config;
    config.sla_print_settings.items.opt("layer_height").set(LAYER_HEIGHT_MM);
    config.sla_material_settings.items.opt("initial_layer_height").set(LAYER_HEIGHT_MM);
    config.sla_print_settings.items.opt("supports_enable").set(true);

    const auto hw_config = Slic3r::Domain::Preset::HwPrinterConfig{
        .technology = Slic3r::Domain::PrinterTechnology::SLA};
    const Slic3r::Domain::FullConfigSLAPtr full_config{
        std::make_shared<const Slic3r::Domain::FullConfigSLA>(config, hw_config)};
    const Slic3r::Domain::PartialObjectConfigSLAPtr object_config{
        std::make_shared<const Slic3r::Domain::PartialObjectConfigSLA>(object->object_settings_sla,
                                                                       full_config->hw_config())};

    const Slic3r::Domain::Transform3d object_to_world{Slic3r::Domain::Transform3d::Identity()};

    const SupportPoints points = Slic3r::sla::generate_support_points_for_tool(
        *object, object_to_world, full_config, object_config, {});
    out.support_points = points.size();

    // The model layers the way the generator sliced them: same heights, same closing radius, same
    // slicing mode. Slicing any other way would hide a region the generator could have supported.
    const Slic3r::SLAPrintObjectConfigView cfg{full_config, object_config};
    const double layer_height = Slic3r::Domain::sla_effective_layer_height(cfg);
    const auto   bb           = mesh.bounding_box();

    // No layer height, no layers. A plain return and not an assertion, so a config that cannot
    // slice never throws out of here into the catch below.
    if (layer_height <= 0.) return out;

    std::vector<float> heights;
    for (double z = bb.min.z() + layer_height * 0.5; z < bb.max.z(); z += layer_height)
        heights.push_back(float(z));

    Slic3r::MeshSlicingParamsEx params;
    params.closing_radius = float(cfg.get<double>("slice_closing_radius"));
    switch (cfg.get<Slic3r::Domain::SlicingMode>("slicing_mode")) {
        case Slic3r::Domain::SlicingMode::Regular:
            params.mode = Slic3r::MeshSlicingParams::SlicingMode::Regular;
            break;
        case Slic3r::Domain::SlicingMode::EvenOdd:
            params.mode = Slic3r::MeshSlicingParams::SlicingMode::EvenOdd;
            break;
        case Slic3r::Domain::SlicingMode::CloseHoles:
            params.mode = Slic3r::MeshSlicingParams::SlicingMode::Positive;
            break;
    }

    const ExPolygons layers = Slic3r::slice_mesh_ex(mesh.its, heights, params);
    out.layer_count        = layers.size();

    out.uncovered_islands = find_uncovered_islands(layers, heights, points, out.island_count);
    out.local_minima      = find_local_minima(layers, heights, points);
    return out;
}

/// The rows are the deliverable: id, layer, Z, area and centroid of every uncovered island, then
/// the local minima in a second list.
void report(const Bench::ModelSpec& spec, const Coverage& coverage)
{
    std::cout << "[LocalIslandCoverage] " << spec.id << ": triangles=" << coverage.triangles
              << ", support_points=" << coverage.support_points
              << ", layers=" << coverage.layer_count << ", islands=" << coverage.island_count
              << ", uncovered=" << coverage.uncovered_islands.size()
              << ", local_minima=" << coverage.local_minima.size() << std::endl;

    for (const UncoveredRegion& region : coverage.uncovered_islands)
        std::cout << "[LocalIslandCoverage] uncovered island: " << spec.id
                  << ", layer=" << region.layer_index << ", z=" << region.z << " mm"
                  << ", area=" << region.area_mm2 << " mm2, centroid=(" << region.centroid.x() << ", "
                  << region.centroid.y() << ")" << std::endl;

    for (const UncoveredRegion& region : coverage.local_minima)
        std::cout << "[LocalIslandCoverage] local minimum: " << spec.id
                  << ", layer=" << region.layer_index << ", z=" << region.z << " mm"
                  << ", area=" << region.area_mm2 << " mm2, centroid=(" << region.centroid.x() << ", "
                  << region.centroid.y() << "), grows for " << region.growth_layers
                  << " layer(s) up" << std::endl;
}

/// The ids SLA_ISLAND_IDS selects, lower cased so the comparison is forgiving.
std::set<std::string> read_ids()
{
    const char* env = std::getenv("SLA_ISLAND_IDS");
    const std::string list = (env && env[0] != '\0') ? env : DEFAULT_IDS;

    std::set<std::string> ids;
    std::stringstream     stream{list};
    std::string           item;
    while (std::getline(stream, item, ',')) {
        const std::string id = Bench::lower(Bench::trim(item));
        if (!id.empty()) ids.insert(id);
    }
    return ids;
}

} // namespace

TEST_CASE("Local island coverage: benchmark corpus", "[.local][SLA][IslandCoverage]")
{
    const char* samples_env = std::getenv("SLA_LOCAL_SAMPLES");
    if (!samples_env || samples_env[0] == '\0') {
        SKIP("SLA_LOCAL_SAMPLES is not set, so the local sample folder is not available");
    }

    const std::filesystem::path samples_dir{samples_env};
    REQUIRE(std::filesystem::is_directory(samples_dir));

    const std::filesystem::path corpus = samples_dir / CORPUS_FOLDER;
    REQUIRE(std::filesystem::is_directory(corpus));

    const std::filesystem::path manifest = corpus / MANIFEST_FILE;
    REQUIRE(std::filesystem::is_regular_file(manifest));

    const std::vector<Bench::ModelSpec> all = Bench::read_manifest(manifest, corpus.string());
    const std::set<std::string>         wanted = read_ids();

    std::vector<Bench::ModelSpec> models;
    for (const Bench::ModelSpec& spec : all)
        if (wanted.count(Bench::lower(spec.id))) models.push_back(spec);
    REQUIRE_FALSE(models.empty());

    size_t measured = 0;
    for (const Bench::ModelSpec& spec : models) {
        std::cout << "[LocalIslandCoverage] " << spec.id << " [" << spec.category << "] starts"
                  << std::endl;

        try {
            const Coverage coverage = analyse_model(spec);
            ++measured;
            report(spec, coverage);

            // A model that was not sliced at all would pass the island check below for the wrong
            // reason, so the measurement itself is checked first.
            CHECK(coverage.layer_count > 0);
            INFO(spec.id << ": " << coverage.uncovered_islands.size() << " of "
                         << coverage.island_count << " islands without a support point, "
                         << coverage.local_minima.size() << " local minima");
            CHECK(coverage.uncovered_islands.size() == size_t(0));
        } catch (const std::exception& e) {
            // The corpus is not redistributable: the id and a scrubbed message are all there is.
            WARN(spec.id << ": " << Bench::describe_error(e.what(), spec));
        } catch (...) {
            WARN(spec.id << ": unknown error");
        }
    }

    CHECK(measured > 0);
}
