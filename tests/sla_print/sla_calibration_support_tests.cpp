// M7.4c: what our generator does with the models of the expert support dataset.
//
// The research dataset of ROADMAP M7 is a library of miniatures a studio supports by
// hand in another slicer: every model folder holds the plain model and the same model
// with the expert's supports as one STL, in print orientation. calibrate.py measures
// the expert side of that library (how many tips, which orientation), this test
// measures ours, and calibration_report.py merges the two into aggregate tables by
// category.
//
// Two orientations per pair, because the point of the calibration is to tell the
// orientation apart from the density:
//
//   as loaded             the plain STL, centred on the plate, so the generator sees
//                         the model the way it arrived
//   expert orientation    the same mesh rotated into the orientation the expert
//                         printed it, which calibrate.py writes under
//                         <manifest folder>/out/oriented/<id>.stl; absent when
//                         calibrate.py has not been run
//
// Both are centred on the plate before they are measured: where the expert happened
// to float a model above its raft moves no support point, so only the orientation is
// what this test compares.
//
// Hidden and local only, so the dataset is never needed by the default run:
//
//     set SLA_CALIB_MANIFEST=local-samples\supports\manifest_calib.yaml
//     set SLA_CALIB_IDS=cal001,cal002
//     build-default	ests\sla_print\Release\sla_print_tests.exe "[.local]" > calibration.log
//
//   SLA_CALIB_MANIFEST  the calibration manifest, a gitignored file naming one pair
//                       per model: an id, a category and the two STL paths. A path may
//                       be absolute, or relative to the manifest's own folder.
//   SLA_CALIB_IDS       comma separated ids; unset or empty runs every pair.
//
// One line per pair, in the form calibration_report.py reads:
//
//   [CalibSupport] id=cal001, category=head, points_as_loaded=1400, points_expert_orientation=900, islands=41, height_mm=35.0, height_expert_mm=35.0
//
// A negative points_expert_orientation and height_expert_mm mean calibrate.py wrote no
// oriented copy of that model. Islands are counted on the model as loaded, which is
// the same number in both orientations: it is the shape, not the placement, that
// decides how many islands there are.
//
// Nothing but the manifest id and the category is ever printed. The dataset is not
// redistributable, so a loader message is scrubbed of the paths it quotes before it
// reaches the console (the M0.13 rule), and nothing here names a file.
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Yaml/Yaml.hpp"
#include "Slic3r/Domain/ConfigCommon.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
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

namespace Calib {

// One pair of the calibration manifest. Every field but the id is optional, so a
// manifest that leaves the category out still runs; a pair with no id and no plain
// STL cannot be reported and is skipped.
struct PairEntry
{
    std::optional<std::string> id;
    std::optional<std::string> category;
    std::optional<std::string> supported_stl;
    std::optional<std::string> unsupported_stl;
};

// The manifest: a top level `pairs:` list, one item per model.
struct PairManifest
{
    std::vector<PairEntry> pairs;
};

} // namespace Calib

// The struct descriptions come before the first parse_struct_unwrap<PairManifest>
// below: a specialization may not be declared after it was instantiated.
STRUCT_DESC_SIMPLE(Calib::PairEntry, id, category, supported_stl, unsupported_stl);
STRUCT_DESC_SIMPLE(Calib::PairManifest, pairs);

namespace {

/// What one pair of the manifest contributes. The paths stay with the reader: only
/// the id and the category reach a report.
struct Pair
{
    std::string id;
    std::string category;
    std::filesystem::path unsupported;
    std::filesystem::path supported;
};

/// What one orientation of one pair measured.
struct Metrics
{
    size_t points    = 0;
    size_t islands   = 0;
    double height_mm = 0.;
};

/// The layers of a sliced model: one ExPolygons per layer height, in the order of the
/// heights.
using Layers = std::vector<Slic3r::Domain::ExPolygons>;

// What the tests and the benchmark harness slice at, and what the default config sets.
constexpr double LAYER_HEIGHT_MM = 0.05;
// Below this an area is a speck of the slicing, not something that can be supported.
constexpr double ISLAND_MIN_AREA_MM2 = 0.1;

std::filesystem::path resolve(const std::filesystem::path& folder, const std::string& path)
{
    // A manifest may name a file in its own folder or one that lives elsewhere.
    const std::filesystem::path candidate{path};
    return candidate.is_absolute() ? candidate : folder / candidate;
}

std::vector<Pair> read_pairs(const std::filesystem::path& manifest)
{
    namespace Yaml = Slic3r::Biz::Yaml;

    const std::filesystem::path folder = manifest.parent_path();
    Calib::PairManifest parsed;
    try {
        const Yaml::YamlAdapter::Document doc = Yaml::parse_file(manifest.string().c_str());
        parsed                                = Yaml::parse_struct_unwrap<Calib::PairManifest>(doc);
    } catch (const std::exception& e) {
        FAIL("the calibration manifest could not be read: " << e.what());
    }

    std::vector<Pair> pairs;
    pairs.reserve(parsed.pairs.size());
    for (const Calib::PairEntry& entry : parsed.pairs) {
        const std::string id = entry.id ? Bench::trim(*entry.id) : std::string{};
        const std::string unsupported =
            entry.unsupported_stl ? Bench::trim(*entry.unsupported_stl) : std::string{};
        if (id.empty() || unsupported.empty())
            continue;

        Pair pair;
        pair.id       = id;
        pair.category = entry.category ? Bench::lower(Bench::trim(*entry.category)) : std::string{};
        if (pair.category.empty())
            pair.category = "uncategorized";
        pair.unsupported = resolve(folder, unsupported);
        if (entry.supported_stl)
            pair.supported = resolve(folder, Bench::trim(*entry.supported_stl));
        pairs.push_back(std::move(pair));
    }
    return pairs;
}

/// The ids SLA_CALIB_IDS selects, lower cased so the comparison is forgiving. Empty is
/// every pair of the manifest.
std::set<std::string> read_ids()
{
    const char* env = std::getenv("SLA_CALIB_IDS");

    std::set<std::string> ids;
    if (!env || env[0] == '\0')
        return ids;

    std::stringstream list{env};
    std::string item;
    while (std::getline(list, item, ',')) {
        const std::string id = Bench::lower(Bench::trim(item));
        if (!id.empty())
            ids.insert(id);
    }
    return ids;
}

/// The dataset is not redistributable: a loader quotes the file it failed on, so both
/// paths and the manifest's own folder are replaced by the id before anything is
/// reported. What is left of a path must not reach the console, not even in the
/// message of a loader.
std::string scrub(const std::string& message, const Pair& pair, const std::filesystem::path& folder)
{
    std::string text = message;
    for (const std::filesystem::path& secret : {pair.unsupported, pair.supported, folder}) {
        const std::string raw = secret.string();
        if (raw.empty())
            continue;
        for (size_t pos = text.find(raw); pos != std::string::npos;
             pos        = text.find(raw, pos + pair.id.size()))
            text.replace(pos, raw.size(), pair.id);
    }
    return text;
}

/// Support points of one model, with the default SLA config, placed on the plate and
/// measured in the orientation it arrived in. Islands are counted when asked for:
/// they cost a slice of the model and the calibration needs them once per pair.
Metrics
measure_orientation(const std::filesystem::path& path, const std::string& id, bool with_islands)
{
    using Slic3r::Domain::Transform3d;

    Slic3r::Domain::TriangleMesh mesh = Bench::load_model_file(path);
    Bench::centre_on_plate(mesh);

    const auto bb = mesh.bounding_box();
    Metrics metrics;
    metrics.height_mm = double(bb.max.z() - bb.min.z());

    // One object, one instance, no transform: the model is measured in the orientation
    // it was loaded in, which is the point of this test.
    Slic3r::Domain::Model model;
    Slic3r::Domain::ModelObject* object = model.add_object();
    object->name                        = id;
    Slic3r::Biz::Algorithms::ModelObject::add_volume(object, mesh);
    object->add_instance();

    Slic3r::Domain::ConfigPackSLA config;
    config.sla_print_settings.items.opt("layer_height").set(LAYER_HEIGHT_MM);
    config.sla_material_settings.items.opt("initial_layer_height").set(LAYER_HEIGHT_MM);
    config.sla_print_settings.items.opt("supports_enable").set(true);

    const auto hw_config = Slic3r::Domain::Preset::HwPrinterConfig{
        .technology = Slic3r::Domain::PrinterTechnology::SLA
    };
    const Slic3r::Domain::FullConfigSLAPtr full_config{
        std::make_shared<const Slic3r::Domain::FullConfigSLA>(config, hw_config)
    };
    const Slic3r::Domain::PartialObjectConfigSLAPtr object_config{std::make_shared<
        const Slic3r::Domain::PartialObjectConfigSLA>(
        object->object_settings_sla,
        full_config->hw_config()
    )};

    const Transform3d object_to_world{Transform3d::Identity()};
    metrics.points =
        Slic3r::sla::generate_support_points_for_tool(
            *object,
            object_to_world,
            full_config,
            object_config,
            {}
        )
            .size();

    if (!with_islands)
        return metrics;

    // The model layers the way the generator sliced them: same heights, same closing
    // radius, same slicing mode. Slicing any other way would hide an island.
    const Slic3r::SLAPrintObjectConfigView cfg{full_config, object_config};
    const double layer_height = Slic3r::Domain::sla_effective_layer_height(cfg);
    if (layer_height <= 0.)
        return metrics; // a config that cannot slice, not a failure

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

    const Layers layers = Slic3r::slice_mesh_ex(mesh.its, heights, params);
    metrics.islands     = Slic3r::SLA::detect_islands(layers, ISLAND_MIN_AREA_MM2).size();
    return metrics;
}

/// The one line per pair that calibration_report.py reads. A negative points value is
/// how it learns that calibrate.py wrote no oriented copy of this model.
void report(const Pair& pair, const Metrics& as_loaded, const Metrics& expert, bool expert_missing)
{
    const long long expert_points = expert_missing ? -1 : static_cast<long long>(expert.points);
    const double expert_height    = expert_missing ? -1. : expert.height_mm;

    std::cout
        << "[CalibSupport] id="
        << pair.id
        << ", category="
        << pair.category
        << ", points_as_loaded="
        << as_loaded.points
        << ", points_expert_orientation="
        << expert_points
        << ", islands="
        << as_loaded.islands
        << ", height_mm="
        << as_loaded.height_mm
        << ", height_expert_mm="
        << expert_height
        << std::endl;
}

} // namespace

TEST_CASE("Calibration: expert support dataset", "[.local][SLA][Calibration]")
{
    const char* manifest_env = std::getenv("SLA_CALIB_MANIFEST");
    if (!manifest_env || manifest_env[0] == '\0') {
        SKIP("SLA_CALIB_MANIFEST is not set, so the calibration manifest is not available");
    }

    const std::filesystem::path manifest{manifest_env};
    REQUIRE(std::filesystem::is_regular_file(manifest));

    // calibrate.py writes the oriented copies next to the manifest it read.
    const std::filesystem::path folder          = manifest.parent_path();
    const std::filesystem::path oriented_folder = folder / "out" / "oriented";

    const std::vector<Pair> all        = read_pairs(manifest);
    const std::set<std::string> wanted = read_ids();

    std::vector<Pair> pairs;
    for (const Pair& pair : all)
        if (wanted.empty() || wanted.count(Bench::lower(pair.id)))
            pairs.push_back(pair);
    REQUIRE_FALSE(pairs.empty());

    size_t measured = 0;
    for (const Pair& pair : pairs) {
        std::cout
            << "[CalibSupport] "
            << pair.id
            << " ["
            << pair.category
            << "] starts"
            << std::endl;

        try {
            const Metrics as_loaded = measure_orientation(pair.unsupported, pair.id, true);

            const std::filesystem::path oriented = oriented_folder / (pair.id + ".stl");
            const bool missing                   = !std::filesystem::is_regular_file(oriented);
            const Metrics expert =
                missing ? Metrics{} : measure_orientation(oriented, pair.id, false);

            ++measured;
            report(pair, as_loaded, expert, missing);
        } catch (const std::exception& e) {
            // The dataset is not redistributable: the id and a scrubbed message are all
            // there is.
            WARN(pair.id << ": " << scrub(e.what(), pair, folder));
        } catch (...) {
            WARN(pair.id << ": unknown error");
        }
    }

    // A run that measured nothing has to fail visibly: the calibration would otherwise
    // look like a dataset with no supports in it.
    CHECK(measured > 0);
}
