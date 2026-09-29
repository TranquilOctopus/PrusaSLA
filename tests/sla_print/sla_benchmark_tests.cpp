// M0.13 benchmark harness (PLAN A6). Slices a set of models headlessly and writes a metrics
// JSON, so engine changes (support point generation, support tree, pad, slicing speed) can be
// compared before and after.
//
// Hidden test. Run it from the repository root:
//
//     build-default\tests\sla_print\Release\sla_print_tests.exe "[benchmark]"
//
// Models: the small set from tests/data plus every .stl/.obj in the folder named by
// SLA_BENCH_DIR (the M0.12 corpus lives outside the repo). Each model is benchmarked as loaded,
// only centred on the plate, with the default SLA config. SLA_BENCH_OUT names the output file,
// default sla_benchmark.json in the working directory.
//
//     set SLA_BENCH_DIR=D:\corpus & set SLA_BENCH_OUT=after.json
//     build-default\tests\sla_print\Release\sla_print_tests.exe "[benchmark]"
//     python doc/sla-fork/tools/bench_diff.py before.json after.json
//
// Every model runs the same pipeline: support points, support tree and pad through the support
// tool (SLASupportTool.hpp), then a full SLAPrint slice with those points. The key order is
// fixed so two runs can be diffed. t_total_ms covers the whole per model run, the island
// counting included; peak_working_set_bytes is process wide and never decreases.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#define SLA_BENCH_HAS_WORKING_SET 1
#endif

#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Format/OBJ.hpp"
#include "Slic3r/Biz/Format/STL.hpp"
#include "Slic3r/Biz/Slicing/BackgroundProcess.hpp"
#include "Slic3r/Domain/Bed.hpp"
#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "Slic3r/Domain/Preset/SelectedPreset.hpp"
#include "Slic3r/Domain/PrinterTechnology.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/Domain/SlicingId.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"
#include "Slic3r/TestUtils/HwConfigUtils.hpp"
#include "libslic3r/IThumbnailImageGenerator.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/SLASupportTool.hpp"
#include "libslic3r/SLA/IslandDetection.hpp"

#include "test_utils.hpp"

namespace {

using Slic3r::SLAPrint;
using Slic3r::SLAPrintObject;
using Slic3r::sla::SupportToolTree;
namespace BB = Slic3r::Biz::Algorithms::BoundingBox;

// The only setting the benchmark changes, everything else is the default SLA config. 0.05 mm is
// what the tests and the fixtures slice at; the SLA default (0.3 mm) is coarse enough to change
// what the support point generator sees.
constexpr double BENCH_LAYER_HEIGHT_MM = 0.05;
// Same threshold as the island coverage tests.
constexpr double ISLAND_MIN_AREA_MM2 = 0.1;
// Vertical distance within which a support point counts as covering an island.
constexpr double ISLAND_POINT_DZ_MM = 0.5;

constexpr const char* BENCH_MODELS[] = {
    "20mm_cube.obj",
    "A_upsidedown.obj",
    "V.obj",
    "frog_legs.obj",
    "pyramid.obj",
    "bridge.obj",
};

/// A JSON value with a fixed key order. Just enough for the metrics file, and it keeps the key
/// order of two runs identical, which is the whole point of the file.
class Json
{
public:
    static Json integer(long long value)
    {
        Json json;
        json.m_kind = Kind::Int;
        json.m_int  = value;
        return json;
    }

    static Json number(double value, int precision = 3)
    {
        // A NaN or an infinity would not be valid JSON, and a benchmark that produces one has
        // bigger problems than a zero in the file.
        if (!std::isfinite(value)) value = 0.;

        Json json;
        json.m_kind      = Kind::Double;
        json.m_double    = value;
        json.m_precision = precision;
        return json;
    }

    static Json string(std::string value)
    {
        Json json;
        json.m_kind   = Kind::String;
        json.m_string = std::move(value);
        return json;
    }

    static Json array()
    {
        Json json;
        json.m_kind = Kind::Array;
        return json;
    }

    static Json object()
    {
        Json json;
        json.m_kind = Kind::Object;
        return json;
    }

    Json& push(Json value)
    {
        m_items.emplace_back(std::move(value));
        return *this;
    }

    Json& set(std::string key, Json value)
    {
        m_keys.emplace_back(std::move(key));
        m_items.emplace_back(std::move(value));
        return *this;
    }

    std::string dump(int level = 0) const
    {
        const std::string pad(size_t(level) * 2, ' ');
        const std::string pad_inner(size_t(level + 1) * 2, ' ');

        switch (m_kind) {
        case Kind::Null:   return "null";
        case Kind::Int:    return std::to_string(m_int);
        case Kind::Double: {
            std::ostringstream out;
            out << std::fixed << std::setprecision(m_precision) << m_double;
            return out.str();
        }
        case Kind::String: return quote(m_string);
        case Kind::Array: {
            if (m_items.empty()) return "[]";
            std::string out = "[\n";
            for (size_t i = 0; i < m_items.size(); ++i) {
                out += pad_inner + m_items[i].dump(level + 1);
                out += (i + 1 < m_items.size()) ? ",\n" : "\n";
            }
            return out + pad + "]";
        }
        case Kind::Object: {
            if (m_keys.empty()) return "{}";
            std::string out = "{\n";
            for (size_t i = 0; i < m_keys.size(); ++i) {
                out += pad_inner + quote(m_keys[i]) + ": " + m_items[i].dump(level + 1);
                out += (i + 1 < m_keys.size()) ? ",\n" : "\n";
            }
            return out + pad + "}";
        }
        }

        return "null";
    }

private:
    enum class Kind { Null, Int, Double, String, Array, Object };

    static std::string quote(const std::string& text)
    {
        std::string out = "\"";
        for (char c : text) {
            switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
            }
        }
        return out + "\"";
    }

    Kind                     m_kind      = Kind::Null;
    long long                m_int       = 0;
    double                   m_double    = 0.;
    int                      m_precision = 3;
    std::string              m_string;
    std::vector<std::string> m_keys; // object keys, parallel to m_items
    std::vector<Json>        m_items;
};

/// The per model numbers, in the order they are written to the JSON.
struct ModelMetrics
{
    std::string name;
    std::string source;
    std::string error;
    size_t   triangles                     = 0;
    size_t   support_points                = 0;
    size_t   support_tree_triangles        = 0;
    double   support_tree_volume_mm3       = 0.;
    double   pad_volume_mm3                = 0.;
    size_t   layer_count                   = 0;
    size_t   islands_detected              = 0;
    size_t   islands_without_support_point = 0;
    double   t_points_ms                   = 0.;
    double   t_tree_pad_ms                 = 0.;
    double   t_slice_ms                    = 0.;
    double   t_total_ms                    = 0.;
    std::optional<uint64_t> peak_working_set_bytes;
};

double ms_since(const std::chrono::steady_clock::time_point& start)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
}

/// Peak working set of this process so far. Process wide and never decreases, so the per model
/// value is the peak up to and including that model. Omitted where it is not cheap to get.
std::optional<uint64_t> peak_working_set_bytes()
{
#if defined(SLA_BENCH_HAS_WORKING_SET)
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, DWORD(sizeof(counters))))
        return static_cast<uint64_t>(counters.PeakWorkingSetSize);
#endif
    return std::nullopt;
}

/// The models are benchmarked as loaded, so the only placement is onto the plate.
void centre_on_plate(Slic3r::Domain::TriangleMesh& mesh)
{
    const auto bb = mesh.bounding_box();
    const auto c  = BB::center(bb);
    mesh.translate(Slic3r::Domain::Vec3f(float(-c.x()), float(-c.y()), float(-bb.min.z())));
}

Slic3r::Domain::ConfigPackSLA make_config()
{
    Slic3r::Domain::ConfigPackSLA config;
    config.sla_print_settings.items.opt("layer_height").set(BENCH_LAYER_HEIGHT_MM);
    config.sla_material_settings.items.opt("initial_layer_height").set(BENCH_LAYER_HEIGHT_MM);
    config.sla_print_settings.items.opt("supports_enable").set(true);
    return config;
}

// Local stand-in for the fff_print test helper, which is not on this target's include path.
Slic3r::Domain::Preset::SelectedPresetMetadata
make_preset_metadata(const Slic3r::Domain::Preset::HwPrinterConfig& hw_config)
{
    return Slic3r::Domain::Preset::SelectedPresetMetadata{
        .hw_config = hw_config,
        .tools     = std::vector<Slic3r::Domain::Preset::EvaluatedPresetMetadata>{hw_config.tool_count},
        .materials = std::vector<Slic3r::Domain::Preset::EvaluatedPresetMetadata>{hw_config.material_slot_count()}};
}

// The harness never inspects thumbnails, so requests are dropped right away.
class NoopThumbnailGenerator : public Slic3r::Biz::Slicing::IThumbnailImageGenerator
{
    std::future<Slic3r::Biz::Slicing::ThumbnailImageResults> enqueue_thumbnail_requests(
        const Slic3r::Biz::Slicing::ThumbnailImageRequests&) override
    {
        std::promise<Slic3r::Biz::Slicing::ThumbnailImageResults> promise;
        promise.set_value(Slic3r::Biz::Slicing::ThumbnailImageResults{});
        return promise.get_future();
    }

    void handle_enqueued_requests() override {}
};

/// Islands of the sliced model layers, and how many of them have no support point near them.
/// The same rule as sla_island_coverage_tests.cpp: a point covers an island when it sits within
/// 0.5 mm of the layer and within sqrt(area) + 1 mm of the island centroid. The layers and the
/// support points are both in the un-lifted frame, so the z values are comparable.
void count_islands(const SLAPrint& print, const Slic3r::Domain::SLA::SupportPoints& points,
                   size_t& islands, size_t& uncovered)
{
    std::vector<Slic3r::Domain::ExPolygons> layers;
    std::vector<double>                     layer_z;

    for (const SLAPrint::PrintLayer& layer : print.print_layers()) {
        Slic3r::Domain::ExPolygons polys;
        double z = 0.;
        bool   first = true;
        for (const auto& record_ref : layer.slices()) {
            const SLAPrintObject::SliceRecord& record = record_ref.get();
            if (first) {
                z     = record.slice_level();
                first = false;
            }
            for (const Slic3r::Domain::ExPolygon& poly : record.get_slice(Slic3r::soModel))
                polys.push_back(poly);
        }
        layers.emplace_back(std::move(polys));
        layer_z.push_back(z);
    }

    const std::vector<Slic3r::SLA::IslandHit> hits =
        Slic3r::SLA::detect_islands(layers, ISLAND_MIN_AREA_MM2);
    islands = hits.size();

    for (const Slic3r::SLA::IslandHit& hit : hits) {
        if (hit.layer_index >= layer_z.size()) continue;

        const double z_hit       = layer_z[hit.layer_index];
        const double max_xy_dist = std::sqrt(hit.area_mm2) + 1.0;

        bool covered = false;
        for (const Slic3r::Domain::SLA::SupportPoint& point : points) {
            if (std::abs(double(point.pos.z()) - z_hit) > ISLAND_POINT_DZ_MM) continue;

            const double dx = double(point.pos.x()) - hit.centroid.x();
            const double dy = double(point.pos.y()) - hit.centroid.y();
            if (std::sqrt(dx * dx + dy * dy) <= max_xy_dist) {
                covered = true;
                break;
            }
        }

        if (!covered) ++uncovered;
    }
}

/// Support points, support tree, pad and a full slice of one model.
ModelMetrics run_model(const std::string& name, const std::string& source,
                       Slic3r::Domain::TriangleMesh mesh)
{
    using Slic3r::Domain::Transform3d;

    ModelMetrics metrics;
    metrics.name   = name;
    metrics.source = source;

    const auto total_start = std::chrono::steady_clock::now();

    centre_on_plate(mesh);
    metrics.triangles = mesh.facets_count();

    // One object, one instance, no transform: the model is benchmarked in the orientation it
    // was loaded in.
    Slic3r::Domain::Model model;
    Slic3r::Domain::ModelObject* object = model.add_object();
    object->name = name;
    Slic3r::Biz::Algorithms::ModelObject::add_volume(object, mesh);
    object->add_instance();

    auto hw_config =
        Slic3r::Test::create_dummy_hw_config(1, 0, Slic3r::Domain::PrinterTechnology::SLA);
    Slic3r::Domain::ConfigPackSLA config = make_config();

    Slic3r::Domain::FullConfigSLAPtr full_config{
        std::make_shared<const Slic3r::Domain::FullConfigSLA>(config, hw_config)};
    // The same two config views the support tool gets in the plater: the print config and the
    // object's own (default) settings.
    Slic3r::Domain::PartialObjectConfigSLAPtr object_config{
        std::make_shared<const Slic3r::Domain::PartialObjectConfigSLA>(object->object_settings_sla,
                                                                       full_config->hw_config())};

    const Transform3d object_to_world{Transform3d::Identity()};

    const auto points_start = std::chrono::steady_clock::now();
    Slic3r::Domain::SLA::SupportPoints points = Slic3r::sla::generate_support_points_for_tool(
        *object, object_to_world, full_config, object_config, {});
    metrics.t_points_ms    = ms_since(points_start);
    metrics.support_points = points.size();

    const auto tree_start = std::chrono::steady_clock::now();
    // The tool builds the support tree and, with the default raft, the pad in one call, so the
    // two are timed together.
    const SupportToolTree tool_tree = Slic3r::sla::build_support_tree_for_tool(
        *object, object_to_world, points, full_config, object_config, {});
    metrics.t_tree_pad_ms = ms_since(tree_start);

    if (tool_tree.tree) {
        metrics.support_tree_triangles  = tool_tree.tree->facets_count();
        metrics.support_tree_volume_mm3 = Slic3r::Domain::its_volume(tool_tree.tree->its);
    }
    if (tool_tree.pad) metrics.pad_volume_mm3 = Slic3r::Domain::its_volume(tool_tree.pad->its);

    // Slice the same object with the same points, this time through the print pipeline.
    object->sla_support_points = points;

    Slic3r::Domain::Bed bed;
    Slic3r::Domain::BedInstance bed_instance{bed};
    for (const Slic3r::Domain::ModelObject* model_object : model.objects)
        for (const Slic3r::Domain::ModelInstance* instance : model_object->instances)
            bed_instance.model_instances.push_back(instance);

    auto preset_metadata = make_preset_metadata(hw_config);
    auto metadata         = Slic3r::Biz::Slicing::build_gcode_metadata({}, preset_metadata, config);

    SLAPrint print{[](Slic3r::Biz::Slicing::SLAResult&&) {}, [](const Slic3r::Biz::Slicing::Sla::Object&) {}};

    const auto slice_start = std::chrono::steady_clock::now();
    print.update(model, config, bed_instance, preset_metadata,
                 Slic3r::Biz::Slicing::build_metadata_serializer(metadata, preset_metadata, config));

    NoopThumbnailGenerator thumbnail_generator;
    print.slice(Slic3r::Domain::SlicingId{0, 0}, thumbnail_generator, std::nullopt);
    metrics.t_slice_ms = ms_since(slice_start);

    metrics.layer_count = print.print_layers().size();
    count_islands(print, points, metrics.islands_detected, metrics.islands_without_support_point);

    metrics.t_total_ms = ms_since(total_start);

    return metrics;
}

Json to_json(const ModelMetrics& metrics)
{
    Json json = Json::object();
    json.set("name", Json::string(metrics.name));
    json.set("source", Json::string(metrics.source));
    json.set("triangles", Json::integer((long long) metrics.triangles));
    json.set("support_points", Json::integer((long long) metrics.support_points));
    json.set("support_tree_triangles", Json::integer((long long) metrics.support_tree_triangles));
    json.set("support_tree_volume_mm3", Json::number(metrics.support_tree_volume_mm3));
    json.set("pad_volume_mm3", Json::number(metrics.pad_volume_mm3));
    json.set("layer_count", Json::integer((long long) metrics.layer_count));
    json.set("islands_detected", Json::integer((long long) metrics.islands_detected));
    json.set("islands_without_support_point",
             Json::integer((long long) metrics.islands_without_support_point));
    json.set("t_points_ms", Json::number(metrics.t_points_ms));
    json.set("t_tree_pad_ms", Json::number(metrics.t_tree_pad_ms));
    json.set("t_slice_ms", Json::number(metrics.t_slice_ms));
    json.set("t_total_ms", Json::number(metrics.t_total_ms));
    if (metrics.peak_working_set_bytes)
        json.set("peak_working_set_bytes",
                 Json::integer((long long) *metrics.peak_working_set_bytes));
    if (!metrics.error.empty()) json.set("error", Json::string(metrics.error));
    return json;
}

std::string lower_extension(const std::filesystem::path& path)
{
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return ext;
}

/// Every .stl and .obj in the folder, sorted by file name so the run is reproducible.
std::vector<std::filesystem::path> models_in(const std::filesystem::path& dir)
{
    std::vector<std::filesystem::path> paths;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        const std::string ext = lower_extension(entry.path());
        if (ext == ".stl" || ext == ".obj") paths.push_back(entry.path());
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

Slic3r::Domain::TriangleMesh load_model_file(const std::filesystem::path& path)
{
    Slic3r::Domain::TriangleMesh mesh;
    if (lower_extension(path) == ".stl") {
        auto loaded = Slic3r::Biz::load_stl(path.string());
        if (!loaded) throw std::runtime_error(path.string() + ": " + loaded.error());
        mesh = std::move(*loaded);
    } else {
        auto loaded = Slic3r::Biz::load_obj(path.string());
        if (!loaded) throw std::runtime_error(path.string() + ": " + loaded.error());
        mesh = std::move(*loaded);
    }

    if (mesh.empty()) throw std::runtime_error(path.string() + ": the mesh is empty");
    return mesh;
}

} // namespace

TEST_CASE("SLA benchmark harness", "[.][SLA][benchmark]")
{
    const char* out_env = std::getenv("SLA_BENCH_OUT");
    const std::filesystem::path out_path = (out_env && out_env[0] != '\0')
                                               ? std::filesystem::path(out_env)
                                               : std::filesystem::path("sla_benchmark.json");

    const char* dir_env = std::getenv("SLA_BENCH_DIR");

    // The model list: tests/data first, then the optional corpus folder.
    std::vector<std::pair<std::string, std::string>> models; // (file name, source folder)

    for (const char* model : BENCH_MODELS) models.emplace_back(model, "tests/data");

    if (dir_env && dir_env[0] != '\0') {
        const std::filesystem::path dir{dir_env};
        INFO("SLA_BENCH_DIR: " << dir.string());
        REQUIRE(std::filesystem::is_directory(dir));
        for (const std::filesystem::path& path : models_in(dir))
            models.emplace_back(path.filename().string(), dir.string());
    }

    REQUIRE_FALSE(models.empty());

    std::vector<ModelMetrics> results;
    results.reserve(models.size());

    for (const auto& model : models) {
        const std::string& name   = model.first;
        const std::string& source = model.second;

        ModelMetrics metrics;
        metrics.name   = name;
        metrics.source = source;

        try {
            Slic3r::Domain::TriangleMesh mesh =
                source == "tests/data" ? load_model(name)
                                       : load_model_file(std::filesystem::path(source) / name);
            metrics = run_model(name, source, std::move(mesh));
        } catch (const std::exception& e) {
            metrics.error = e.what();
        } catch (...) {
            metrics.error = "unknown error";
        }

        metrics.peak_working_set_bytes = peak_working_set_bytes();

        if (!metrics.error.empty()) {
            WARN("Benchmark failed for " << name << ": " << metrics.error);
        } else {
            std::cout << "[Benchmark] " << name
                      << ": triangles=" << metrics.triangles
                      << ", support_points=" << metrics.support_points
                      << ", support_tree_triangles=" << metrics.support_tree_triangles
                      << ", support_tree_volume_mm3=" << metrics.support_tree_volume_mm3
                      << ", pad_volume_mm3=" << metrics.pad_volume_mm3
                      << ", layers=" << metrics.layer_count
                      << ", islands=" << metrics.islands_detected
                      << ", islands_without_support_point=" << metrics.islands_without_support_point
                      << ", points=" << metrics.t_points_ms << " ms"
                      << ", tree+pad=" << metrics.t_tree_pad_ms << " ms"
                      << ", slice=" << metrics.t_slice_ms << " ms" << std::endl;
        }

        results.push_back(std::move(metrics));
    }

    // The summary sums the same keys over the models that ran, in the same order, so the diff
    // tool prints both records with one code path. The t_* keys are measurements and keep three
    // decimals, the rest are counts.
    constexpr std::array<const char*, 12> summary_keys{
        "triangles",
        "support_points",
        "support_tree_triangles",
        "support_tree_volume_mm3",
        "pad_volume_mm3",
        "layer_count",
        "islands_detected",
        "islands_without_support_point",
        "t_points_ms",
        "t_tree_pad_ms",
        "t_slice_ms",
        "t_total_ms"};

    std::array<double, summary_keys.size()> summary_values{};

    for (const ModelMetrics& metrics : results) {
        if (!metrics.error.empty()) continue;
        const std::array<double, summary_keys.size()> values{
            double(metrics.triangles),
            double(metrics.support_points),
            double(metrics.support_tree_triangles),
            metrics.support_tree_volume_mm3,
            metrics.pad_volume_mm3,
            double(metrics.layer_count),
            double(metrics.islands_detected),
            double(metrics.islands_without_support_point),
            metrics.t_points_ms,
            metrics.t_tree_pad_ms,
            metrics.t_slice_ms,
            metrics.t_total_ms,
        };
        for (size_t i = 0; i < summary_values.size(); ++i) summary_values[i] += values[i];
    }

    Json summary = Json::object();
    summary.set("model_count", Json::integer((long long) results.size()));
    for (size_t i = 0; i < summary_keys.size(); ++i) {
        const std::string key = summary_keys[i];
        const bool is_measure = key.rfind("t_", 0) == 0;
        summary.set(key, is_measure ? Json::number(summary_values[i])
                                    : Json::integer((long long) summary_values[i]));
    }

    std::optional<uint64_t> peak;
    for (const ModelMetrics& metrics : results) {
        if (!metrics.peak_working_set_bytes) continue;
        peak = peak ? std::max(*peak, *metrics.peak_working_set_bytes)
                    : *metrics.peak_working_set_bytes;
    }
    if (peak) summary.set("peak_working_set_bytes", Json::integer((long long) *peak));

    size_t measured = 0;
    for (const ModelMetrics& metrics : results)
        if (metrics.error.empty()) ++measured;

    Json document = Json::object();
    document.set("schema", Json::integer(1));
    document.set("layer_height_mm", Json::number(BENCH_LAYER_HEIGHT_MM));
    document.set("island_min_area_mm2", Json::number(ISLAND_MIN_AREA_MM2));

    Json model_array = Json::array();
    for (const ModelMetrics& metrics : results) model_array.push(to_json(metrics));
    document.set("models", std::move(model_array));
    document.set("summary", std::move(summary));

    if (!out_path.parent_path().empty())
        std::filesystem::create_directories(out_path.parent_path());

    std::ofstream out(out_path);
    REQUIRE(out.is_open());
    out << document.dump() << "\n";
    out.close();

    std::cout << "[Benchmark] " << measured << "/" << results.size() << " models measured, wrote "
              << out_path.string() << std::endl;

    CHECK(measured > 0);
}
