// M4.13: which settings may move the layer hash, and which may not.
//
// The benchmark harness (sla_benchmark_tests.cpp) writes an FNV-1a hash over the sliced layers into
// its metrics file, and a difference between two runs is how a geometry change gets noticed. That
// only works if the hash moves for the settings that change geometry and stays put for the settings
// that do not, so this test pins both directions with the same construction the harness uses.
//
// PLAN B8: "raster hash changes are intentional, documented and tested". The raster is built after
// the layers, so gamma_correction and the exposure times must not show up here; the printer
// corrections that rewrite or delete polygons must.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <future>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/Slicing/BackgroundProcess.hpp"
#include "Slic3r/Domain/Bed.hpp"
#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/Preset/SelectedPreset.hpp"
#include "Slic3r/Domain/PrinterTechnology.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/Domain/SlicingId.hpp"
#include "Slic3r/TestUtils/HwConfigUtils.hpp"

#include "libslic3r/IThumbnailImageGenerator.hpp"
#include "libslic3r/SLAPrint.hpp"

namespace {

// FNV-1a, 64 bit, the same construction as the benchmark harness: integers only, no raw struct
// bytes, so the value does not depend on padding, endianness or the allocator.
class Fnv1a
{
public:
    template <typename T>
        requires std::is_integral_v<T>
    void feed(T value)
    {
        feed_bytes(uint64_t(value));
    }

    uint64_t value() const { return m_hash; }

private:
    void feed_bytes(uint64_t value)
    {
        for (int byte = 0; byte < 8; ++byte) {
            m_hash ^= (value >> (byte * 8)) & 0xffu;
            m_hash *= 0x100000001b3ull;
        }
    }

    uint64_t m_hash = 0xcbf29ce484222325ull;
};

void collect_points(const Slic3r::Domain::ExPolygon& poly, std::vector<std::pair<int64_t, int64_t>>& out)
{
    for (const Slic3r::Domain::Point& p : poly.contour) out.emplace_back(int64_t(p.x()), int64_t(p.y()));
    for (const Slic3r::Domain::Polygon& hole : poly.holes)
        for (const Slic3r::Domain::Point& p : hole) out.emplace_back(int64_t(p.x()), int64_t(p.y()));
}

/// The FNV over the model slices of every print layer, built exactly as sla_benchmark_tests.cpp
/// builds it: the layer count, then per layer its level, its polygon count, its point count and
/// all of its points, the points sorted so no parallel run can reorder them. A support or pad
/// change would not show up here, which is the point: the benchmark hashes the model slices.
uint64_t layer_hash(const Slic3r::SLAPrint& print)
{
    Fnv1a hasher;
    hasher.feed(uint64_t(print.print_layers().size()));

    for (const Slic3r::SLAPrint::PrintLayer& layer : print.print_layers()) {
        size_t                                   polys = 0;
        std::vector<std::pair<int64_t, int64_t>> flat;

        for (const auto& record_ref : layer.slices()) {
            const Slic3r::SliceRecord& record = record_ref.get();
            if (record.print_obj() == nullptr) continue;
            for (const Slic3r::Domain::ExPolygon& poly : record.get_slice(Slic3r::soModel)) {
                ++polys;
                collect_points(poly, flat);
            }
        }

        hasher.feed(int64_t(layer.level()));
        hasher.feed(uint64_t(polys));
        hasher.feed(uint64_t(flat.size()));
        std::sort(flat.begin(), flat.end());
        for (const auto& point : flat) {
            hasher.feed(point.first);
            hasher.feed(point.second);
        }
    }

    return hasher.value();
}

// The benchmark never inspects thumbnails, so requests are dropped right away.
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

// A 20 mm cube with four support points on its bottom face, so a support tree and a raft under
// the lifted object are generated the way a real print has them.
struct CubeWithSupports {
    Slic3r::Domain::Model model;

    CubeWithSupports()
    {
        using Slic3r::Domain::SLA::SupportPoint;
        using Slic3r::Domain::SLA::SupportPointType;
        using Slic3r::Domain::Vec3f;

        Slic3r::Domain::ModelObject* object = model.add_object();
        object->name                         = "cube.stl";
        Slic3r::Biz::Algorithms::ModelObject::add_volume(
            object, Slic3r::Biz::Algorithms::TriangleMesh::make_cube(20., 20., 20.));
        object->add_instance();
        object->sla_support_points = {
            SupportPoint{Vec3f{5.f, 5.f, 0.f}, 0.2f, SupportPointType::island},
            SupportPoint{Vec3f{15.f, 5.f, 0.f}, 0.2f, SupportPointType::island},
            SupportPoint{Vec3f{5.f, 15.f, 0.f}, 0.2f, SupportPointType::island},
            SupportPoint{Vec3f{15.f, 15.f, 0.f}, 0.2f, SupportPointType::island},
        };
    }
};

Slic3r::Domain::Preset::SelectedPresetMetadata
make_preset_metadata(const Slic3r::Domain::Preset::HwPrinterConfig& hw_config)
{
    return Slic3r::Domain::Preset::SelectedPresetMetadata{
        .hw_config = hw_config,
        .tools     = std::vector<Slic3r::Domain::Preset::EvaluatedPresetMetadata>{hw_config.tool_count},
        .materials = std::vector<Slic3r::Domain::Preset::EvaluatedPresetMetadata>{hw_config.material_slot_count()}};
}

// The BedInstance refers to the bed, so both have to outlive the slice.
struct BedWithInstances {
    Slic3r::Domain::Bed        bed;
    Slic3r::Domain::BedInstance bed_instance{bed};

    explicit BedWithInstances(Slic3r::Domain::Model& model)
    {
        for (Slic3r::Domain::ModelObject* object : model.objects)
            for (Slic3r::Domain::ModelInstance* instance : object->instances)
                bed_instance.model_instances.push_back(instance);
    }
};

/// Slice the cube with the default test config after `tweak` had its say, and return the layer hash.
uint64_t hash_with(std::function<void(Slic3r::Domain::ConfigPackSLA&)> tweak)
{
    CubeWithSupports cube;
    BedWithInstances bed{ cube.model };

    Slic3r::Domain::ConfigPackSLA config;
    config.sla_printer_settings.items.opt("sla_archive_format").set(std::string("SL1"));
    config.sla_print_settings.items.opt("layer_height").set(0.5);
    config.sla_material_settings.items.opt("initial_layer_height").set(0.5);
    config.sla_print_settings.items.opt("supports_enable").set(true);
    // The raft is named rather than asked for with pad_enable: raft_type is Auto by default since
    // M7.8.4, and Auto would read the underside of this cube (and find no pocket in it) instead of
    // building the full plate raft this fixture is about.
    config.sla_print_settings.items.opt("raft_type")
        .set(Slic3r::Domain::sla::RaftType::Full);
    // The elephant foot compensation is ramped over the first faded_layers layers OF THE PRINT,
    // and apply_printer_corrections() walks po.m_slice_index, which starts an elevation below the
    // object: the default support_object_elevation (5 mm) plus the 2 mm raft wall puts the cube's
    // own first layer at layer 14 of a 0.5 mm print, so the default ramp of 10 layers stops at
    // layer 9 and rewrites raft polygons only, and this hash reads the model slices as the
    // benchmark's does. Raised to the maximum the key allows, 20, the ramp reaches the cube, which
    // is what the case below measures. The raft, the tree and the elevation are as they are.
    config.sla_print_settings.items.opt("faded_layers").set(20);
    tweak(config);

    auto hw_config = Slic3r::Test::create_dummy_hw_config(1, 0, Slic3r::Domain::PrinterTechnology::SLA);
    auto preset_metadata = make_preset_metadata(hw_config);
    auto metadata         = Slic3r::Biz::Slicing::build_gcode_metadata({}, preset_metadata, config);

    Slic3r::SLAPrint print{[](Slic3r::Biz::Slicing::SLAResult&&) {},
                           [](const Slic3r::Biz::Slicing::Sla::Object&) {}};
    print.update(cube.model, config, bed.bed_instance, preset_metadata,
                 Slic3r::Biz::Slicing::build_metadata_serializer(metadata, preset_metadata, config));

    NoopThumbnailGenerator thumbnail_generator;
    print.slice(Slic3r::Domain::SlicingId{0, 0}, thumbnail_generator, std::nullopt);

    REQUIRE(print.objects().size() == 1);
    REQUIRE_FALSE(print.print_layers().empty());
    return layer_hash(print);
}

} // namespace

TEST_CASE("A setting that cannot move geometry leaves the layer hash alone", "[SLA][LayerHash]")
{
    // The exposure time is how long the light stays on, not a shape: the engine reads it in
    // merge_slices_and_eval_stats, after the polygons exist. Two runs that differ only in it must
    // hash the same, or every benchmark diff is noise.
    const uint64_t baseline = hash_with([](Slic3r::Domain::ConfigPackSLA&) {});
    const uint64_t slower   = hash_with([](Slic3r::Domain::ConfigPackSLA& config) {
        config.sla_material_settings.items.opt("exposure_time").set(25.0);
    });
    CHECK(slower == baseline);

    // The first layer's exposure is just as much a time as the others.
    const uint64_t longer_first = hash_with([](Slic3r::Domain::ConfigPackSLA& config) {
        config.sla_material_settings.items.opt("initial_exposure_time").set(60.0);
    });
    CHECK(longer_first == baseline);

    // gamma_correction belongs to the rasterizer, which runs after the layers are hashed, so it
    // cannot move the hash either. It is worth pinning separately: it invalidates every step
    // (SLAPrint.cpp), which is exactly the kind of thing that makes a hash look unstable.
    const uint64_t thresholded = hash_with([](Slic3r::Domain::ConfigPackSLA& config) {
        config.sla_printer_settings.items.opt("gamma_correction").set(0.0);
    });
    CHECK(thresholded == baseline);
}

TEST_CASE("A setting that rewrites the first layers moves the layer hash", "[SLA][LayerHash]")
{
    // elefant_foot_compensation shrinks the first faded_layers layers by up to its value, in
    // SLAPrint::Steps::apply_printer_corrections. A 20 mm cube is far above the 0.2 mm
    // elefant_foot_min_width below which nothing is compensated, so the first layer must change.
    // It is a ramp over the print's first layers and this hash reads the model slices, so the
    // print is built with a ramp long enough for the cube to be inside it: see hash_with.
    const uint64_t baseline = hash_with([](Slic3r::Domain::ConfigPackSLA&) {});
    const uint64_t shrunk   = hash_with([](Slic3r::Domain::ConfigPackSLA& config) {
        config.sla_printer_settings.items.opt("elefant_foot_compensation").set(0.5);
    });
    CHECK(shrunk != baseline);

    // absolute_correction offsets every layer, so it has to move the hash too.
    const uint64_t offset = hash_with([](Slic3r::Domain::ConfigPackSLA& config) {
        config.sla_printer_settings.items.opt("absolute_correction").set(0.2);
    });
    CHECK(offset != baseline);
}

TEST_CASE("The layer hash is stable across two runs of the same config", "[SLA][LayerHash]")
{
    // Without this the two tests above would pass on a hash that is simply different every time.
    const uint64_t first  = hash_with([](Slic3r::Domain::ConfigPackSLA&) {});
    const uint64_t second = hash_with([](Slic3r::Domain::ConfigPackSLA&) {});
    CHECK(first == second);
}
