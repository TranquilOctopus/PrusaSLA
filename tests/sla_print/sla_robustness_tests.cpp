// M6.1: robustness meshes. The SLA pipeline has to survive meshes it cannot print: an empty
// shell, a single triangle, flipped normals, a model 1500 mm away from the origin. None of them
// may crash the slicer and none of them may hang it.
//
// Every mesh here is built in code (no files from tests/data) and every case runs the same two
// stages: the support tool (points, then tree and raft) and a full SLAPrint slice.
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstddef>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "libslic3r/SLASupportTool.hpp"
#include "libslic3r/ConfigViews.hpp"
#include "libslic3r/IThumbnailImageGenerator.hpp"
#include "libslic3r/SLAPrint.hpp"

#include "Slic3r/Exception.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/Slicing/BackgroundProcess.hpp"
#include "Slic3r/Domain/Bed.hpp"
#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/Domain/ConfigBoxesSLA.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/PrinterTechnology.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "Slic3r/Domain/Preset/SelectedPreset.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/Domain/SlicingId.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/TestUtils/HwConfigUtils.hpp"

namespace {

// indexed_triangle_set comes from admesh/stl.h and lives in the global namespace.
using ::indexed_triangle_set;
using Slic3r::Domain::BoundingBox3d;
using Slic3r::Domain::Index3;
using Slic3r::Domain::TriangleMesh;
using Slic3r::Domain::Vec3f;
using Slic3r::Domain::Vec3d;

// The support generator has run for minutes on a mesh far from the origin (see the last case), so
// every support call gets a deadline instead of a hope. A call that runs out of time is reported
// as stopped, not as a failure: the deadline bounds the test, it does not assert a speed.
const std::chrono::seconds support_deadline{60};

// The slice has no stop function, so it runs on a worker thread with this budget instead. It is
// generous: nothing in this file is expected to take minutes.
const std::chrono::seconds slice_deadline{60};

// ---------------------------------------------------------------------------------------------
// Meshes, built in code
// ---------------------------------------------------------------------------------------------

// The eight corners of a box and its six faces as twelve outward oriented triangles. Written out
// instead of calling TriangleMesh::make_cube so that the cases below can drop, flip and duplicate
// single faces. Face order: bottom (-Z), top (+Z), front (-Y), right (+X), back (+Y), left (-X),
// two triangles each.
struct BoxGeometry
{
    static std::vector<Vec3f> corners(double x, double y, double z)
    {
        const float fx = float(x), fy = float(y), fz = float(z);
        return {{0.f, 0.f, 0.f}, {fx, 0.f, 0.f}, {fx, fy, 0.f}, {0.f, fy, 0.f},
                {0.f, 0.f, fz}, {fx, 0.f, fz}, {fx, fy, fz}, {0.f, fy, fz}};
    }

    static std::vector<Index3> faces()
    {
        return {{0, 3, 2}, {0, 2, 1},   // bottom
                {4, 5, 6}, {4, 6, 7},   // top
                {0, 1, 5}, {0, 5, 4},   // front
                {1, 2, 6}, {1, 6, 5},   // right
                {2, 3, 7}, {2, 7, 6},   // back
                {3, 0, 4}, {3, 4, 7}};  // left
    }
};

indexed_triangle_set box_its(double x, double y, double z)
{
    return {BoxGeometry::faces(), BoxGeometry::corners(x, y, z)};
}

// A box with the top face removed: an open shell with four open edges along the rim.
indexed_triangle_set open_box_its(double x, double y, double z)
{
    indexed_triangle_set its = box_its(x, y, z);
    its.indices.erase(its.indices.begin() + 2, its.indices.begin() + 4);
    return its;
}

// A box with the winding of one face reversed, so that face points into the volume.
indexed_triangle_set flipped_face_box_its(double x, double y, double z)
{
    indexed_triangle_set its = box_its(x, y, z);
    std::swap(its.indices[8][0], its.indices[8][2]); // the first triangle of the back face
    return its;
}

// Two cubes overlapping in the middle, merged into a single volume.
indexed_triangle_set overlapping_cubes_its(double size)
{
    indexed_triangle_set its = box_its(size, size, size);
    indexed_triangle_set other = box_its(size, size, size);
    const float half = float(size / 2.);
    for (Vec3f& v : other.vertices)
        v += Vec3f{half, half, half};
    Slic3r::Domain::its_merge(its, other);
    return its;
}

// A cube whose faces are each present twice.
indexed_triangle_set duplicated_faces_box_its(double x, double y, double z)
{
    indexed_triangle_set its = box_its(x, y, z);
    const std::vector<Index3> faces = its.indices;
    its.indices.insert(its.indices.end(), faces.begin(), faces.end());
    return its;
}

// One triangle in the XY plane, and one whose three points lie on a line.
indexed_triangle_set single_triangle_its()
{
    return {{{0, 1, 2}}, {{0.f, 0.f, 0.f}, {10.f, 0.f, 0.f}, {0.f, 10.f, 0.f}}};
}

indexed_triangle_set degenerate_triangle_its()
{
    return {{{0, 1, 2}}, {{0.f, 0.f, 0.f}, {5.f, 0.f, 0.f}, {10.f, 0.f, 0.f}}};
}

TriangleMesh make_mesh(indexed_triangle_set its)
{
    return Slic3r::Biz::Algorithms::TriangleMesh::construct(std::move(its));
}

// ---------------------------------------------------------------------------------------------
// Model and config
// ---------------------------------------------------------------------------------------------

// A model with one object holding `mesh`, standing on the plate and moved to `offset`.
struct MeshModel
{
    Slic3r::Domain::Model model;
    Slic3r::Domain::ModelObject* object{nullptr};

    MeshModel(TriangleMesh mesh, const Vec3d& offset = {0., 0., 0.})
    {
        const BoundingBox3d bb = mesh.bounding_box();
        const float dx = float(bb.max.x() - bb.min.x());
        const float dy = float(bb.max.y() - bb.min.y());

        object = model.add_object();
        object->name = "robustness.stl";

        // The slice takes the support points from the object instead of generating them, and an
        // object without points is sliced without supports. Put points on the bottom face so that
        // the tree and the raft are built for every case that has a footprint to put them on.
        if (dx > 0.f && dy > 0.f) {
            const float x0 = float(bb.min.x()), y0 = float(bb.min.y()), z = float(bb.min.z());
            const auto island = [](Vec3f p) {
                return Slic3r::Domain::SLA::SupportPoint{p, 0.2f,
                                                         Slic3r::Domain::SLA::SupportPointType::island};
            };
            object->sla_support_points = {
                island(Vec3f{x0 + dx / 4.f, y0 + dy / 4.f, z}),
                island(Vec3f{x0 + 3 * dx / 4.f, y0 + dy / 4.f, z}),
                island(Vec3f{x0 + dx / 4.f, y0 + 3 * dy / 4.f, z}),
                island(Vec3f{x0 + 3 * dx / 4.f, y0 + 3 * dy / 4.f, z})};
        }

        Slic3r::Biz::Algorithms::ModelObject::add_volume(object, mesh);
        object->add_instance()->set_offset(offset);
    }
};

// The support tool API takes the raw config pointers and resolves the SLA object view itself.
struct SlaConfig
{
    Slic3r::Domain::FullConfigSLAPtr full;
    Slic3r::Domain::PartialObjectConfigSLAPtr object_settings;
};

// Supports on, raft on (raft_type defaults to Full), a 0.05 mm layer.
SlaConfig make_sla_config()
{
    Slic3r::Domain::ConfigPackSLA pack;
    pack.sla_print_settings.items.opt("layer_height").set(0.05);
    pack.sla_print_settings.items.opt("supports_enable").set(true);
    pack.sla_print_settings.items.opt("pad_enable").set(true);
    pack.sla_print_settings.items.opt("support_object_elevation").set(10.0);

    SlaConfig cfg;
    cfg.full = std::make_shared<const Slic3r::Domain::FullConfigSLA>(
        pack,
        Slic3r::Domain::Preset::HwPrinterConfig{.technology = Slic3r::Domain::PrinterTechnology::SLA});
    cfg.object_settings = std::make_shared<const Slic3r::Domain::PartialObjectConfigSLA>(
        Slic3r::Domain::SLAObjectSettings{}, cfg.full->hw_config());
    return cfg;
}

Slic3r::Domain::Transform3d object_to_world(const Vec3d& offset)
{
    Slic3r::Domain::Transform3d trafo = Slic3r::Domain::Transform3d::Identity();
    trafo.translate(offset);
    return trafo;
}

// ---------------------------------------------------------------------------------------------
// Sanity checks
// ---------------------------------------------------------------------------------------------

// A support point has to be a point, not a NaN.
void check_finite(const Slic3r::Domain::SLA::SupportPoints& points, const char* what)
{
    for (size_t i = 0; i < points.size(); ++i) {
        const Vec3f& p = points[i].pos;
        INFO(what << " support point " << i);
        REQUIRE(std::isfinite(p.x()));
        REQUIRE(std::isfinite(p.y()));
        REQUIRE(std::isfinite(p.z()));
    }
}

// Generated geometry may not carry NaN or infinity into the slicer.
void check_finite(const TriangleMesh& mesh, const char* what)
{
    for (size_t i = 0; i < mesh.its.vertices.size(); ++i) {
        const Vec3f& v = mesh.its.vertices[i];
        INFO(what << " vertex " << i);
        REQUIRE(std::isfinite(v.x()));
        REQUIRE(std::isfinite(v.y()));
        REQUIRE(std::isfinite(v.z()));
    }
}

void check_finite(const Slic3r::sla::SupportToolTree& tree)
{
    if (tree.tree)
        check_finite(*tree.tree, "support tree");
    if (tree.pad)
        check_finite(*tree.pad, "raft");
}

// The same test without an assertion in it, for geometry that was produced on a worker thread:
// Catch2 assertions belong to the thread that runs the test case.
bool is_finite(const TriangleMesh& mesh)
{
    for (const Vec3f& v : mesh.its.vertices)
        if (!std::isfinite(v.x()) || !std::isfinite(v.y()) || !std::isfinite(v.z()))
            return false;
    return true;
}

// ---------------------------------------------------------------------------------------------
// Stage 1: the support tool
// ---------------------------------------------------------------------------------------------

// A stop function that fires once its budget has passed. Both support tool entry points poll it
// and return an empty result when it fires, so a stopped run is a normal outcome.
struct Deadline
{
    std::chrono::steady_clock::time_point start{std::chrono::steady_clock::now()};
    std::chrono::seconds budget{support_deadline};

    bool expired() const { return std::chrono::steady_clock::now() - start > budget; }
    Slic3r::sla::SupportToolStop stop() { return [this] { return expired(); }; }
};

struct SupportResult
{
    Slic3r::Domain::SLA::SupportPoints points;
    Slic3r::sla::SupportToolTree tree;
    bool stopped{false};
};

// Generates the support points and builds the tree and the raft for `mesh` placed at `offset`.
SupportResult run_support_tool(TriangleMesh mesh, const Vec3d& offset = {0., 0., 0.})
{
    MeshModel model{std::move(mesh), offset};
    SlaConfig config = make_sla_config();
    const Slic3r::Domain::Transform3d trafo = object_to_world(offset);
    Deadline deadline;

    SupportResult result;
    result.points = Slic3r::sla::generate_support_points_for_tool(
        *model.object, trafo, config.full, config.object_settings, deadline.stop());

    // Out of time: the tree would only be built from what the generator managed to produce.
    if (deadline.expired()) {
        result.stopped = true;
        return result;
    }

    result.tree = Slic3r::sla::build_support_tree_for_tool(
        *model.object, trafo, result.points, config.full, config.object_settings, deadline.stop());
    result.stopped = deadline.expired();
    return result;
}

// The checks every case makes on the support tool: finite geometry, nothing out of nothing, and no
// empty mesh handed on as a tree or a raft.
void check_support_result(const SupportResult& result, bool expect_empty)
{
    if (result.stopped)
        WARN("the support tool ran out of time on this mesh");

    check_finite(result.points, "generated");
    check_finite(result.tree);

    if (expect_empty) {
        CHECK(result.points.empty());
        CHECK(result.tree.tree == nullptr);
        CHECK(result.tree.pad == nullptr);
        return;
    }

    if (result.tree.tree)
        CHECK_FALSE(result.tree.tree->empty());
    if (result.tree.pad)
        CHECK_FALSE(result.tree.pad->empty());
}

// ---------------------------------------------------------------------------------------------
// Stage 2: a full slice
// ---------------------------------------------------------------------------------------------

// Local stand-in for the fff_print test helper, which is not on this target's include path.
Slic3r::Domain::Preset::SelectedPresetMetadata
make_preset_metadata(const Slic3r::Domain::Preset::HwPrinterConfig& hw_config)
{
    return Slic3r::Domain::Preset::SelectedPresetMetadata{
        .hw_config = hw_config,
        .tools     = std::vector<Slic3r::Domain::Preset::EvaluatedPresetMetadata>{hw_config.tool_count},
        .materials = std::vector<Slic3r::Domain::Preset::EvaluatedPresetMetadata>{hw_config.material_slot_count()}
    };
}

// The tests never inspect thumbnails, so requests are dropped right away.
class NoopThumbnailGenerator : public Slic3r::Biz::Slicing::IThumbnailImageGenerator
{
public:
    std::future<Slic3r::Biz::Slicing::ThumbnailImageResults> enqueue_thumbnail_requests(
        const Slic3r::Biz::Slicing::ThumbnailImageRequests&) override
    {
        std::promise<Slic3r::Biz::Slicing::ThumbnailImageResults> promise;
        promise.set_value(Slic3r::Biz::Slicing::ThumbnailImageResults{});
        return promise.get_future();
    }

    void handle_enqueued_requests() override {}
};

struct SliceResult
{
    bool finished{false};        // the pipeline ran to the end
    bool reported_error{false}; // the engine refused the model, which the app shows on the bed
    bool geometry_finite{true};  // no NaN in the support or raft geometry it produced
    std::string error;
    size_t layers{0};
    size_t support_facets{0};
    size_t raft_facets{0};
};

// Slices `mesh` placed at `offset` with the standard SLA preset.
//
// A model the engine cannot slice is not a robustness failure: it throws a Slic3r::Exception, which
// BackgroundProcess turns into an error on the bed (BackgroundProcess.cpp:269). That is a handled
// outcome and is reported here. Anything else escaping the slice, or the slice never returning, is
// what this file is after - and it is not caught, so it takes the test down.
SliceResult run_slice(TriangleMesh mesh, const Vec3d& offset = {0., 0., 0.})
{
    SliceResult result;

    MeshModel model{std::move(mesh), offset};
    Slic3r::Domain::Bed bed;
    Slic3r::Domain::BedInstance bed_instance{bed};
    for (const Slic3r::Domain::ModelObject* object : model.model.objects) {
        for (Slic3r::Domain::ModelInstance* instance : object->instances)
            bed_instance.model_instances.push_back(instance);
    }

    Slic3r::Domain::ConfigPackSLA config;
    config.sla_print_settings.items.opt("layer_height").set(0.05);
    config.sla_material_settings.items.opt("initial_layer_height").set(0.05);
    config.sla_print_settings.items.opt("supports_enable").set(true);
    config.sla_print_settings.items.opt("pad_enable").set(true);

    const auto hw_config =
        Slic3r::Test::create_dummy_hw_config(1, 0, Slic3r::Domain::PrinterTechnology::SLA);
    const auto preset_metadata = make_preset_metadata(hw_config);
    const auto metadata = Slic3r::Biz::Slicing::build_gcode_metadata({}, preset_metadata, config);

    Slic3r::SLAPrint print{
        [](Slic3r::Biz::Slicing::SLAResult&&) {}, [](const Slic3r::Biz::Slicing::Sla::Object&) {}};

    try {
        print.update(model.model, config, bed_instance, preset_metadata,
                     Slic3r::Biz::Slicing::build_metadata_serializer(metadata, preset_metadata, config));

        NoopThumbnailGenerator thumbnail_generator;
        print.slice(Slic3r::Domain::SlicingId{0, 0}, thumbnail_generator, std::nullopt);
    } catch (const Slic3r::Exception& e) {
        result.reported_error = true;
        result.error = e.what();
        return result;
    }

    result.finished = true;
    result.layers = print.print_layers().size();
    for (const Slic3r::SLAPrintObject* object : print.objects()) {
        result.support_facets += object->support_mesh().facets_count();
        result.raft_facets += object->pad_mesh().facets_count();
        result.geometry_finite = is_finite(object->support_mesh()) && is_finite(object->pad_mesh());
    }
    return result;
}

// The checks every case makes on the slice: the pipeline came back, one way or the other, and the
// geometry it built is a set of coordinates.
void check_slice_result(const SliceResult& result)
{
    if (result.reported_error)
        INFO("the engine reported: " << result.error);
    CHECK(result.finished || result.reported_error);
    if (result.finished) {
        INFO("layers: " << result.layers << ", support facets: " << result.support_facets
                        << ", raft facets: " << result.raft_facets);
        CHECK(result.geometry_finite);
    }
}

// ---------------------------------------------------------------------------------------------
// A job on a worker thread, so that a hang is a failing test and not a test binary that never
// returns
// ---------------------------------------------------------------------------------------------

struct WorkerState
{
    std::mutex mutex;
    std::condition_variable done;
    bool finished{false};
};

// Runs `job` on a worker thread and waits for it. Returns false if the job was still running after
// `budget`, in which case the thread is detached and left to finish on its own: the test fails and
// the process still exits. The job has to own everything it touches, the thread outlives nothing
// this function owns.
template <typename Job>
bool run_on_worker(Job&& job, std::chrono::seconds budget)
{
    auto state = std::make_shared<WorkerState>();
    std::thread worker{[state, job = std::forward<Job>(job)]() mutable {
        try {
            job();
        } catch (...) {
            // The job records its own failures, anything else is left to the checks to notice.
        }
        {
            const std::lock_guard<std::mutex> lock{state->mutex};
            state->finished = true;
        }
        state->done.notify_all();
    }};

    bool finished = false;
    {
        std::unique_lock<std::mutex> lock{state->mutex};
        finished = state->done.wait_for(lock, budget, [&state] { return state->finished; });
    }

    if (finished)
        worker.join();
    else
        worker.detach();

    return finished;
}

// The result of both stages, in a block that outlives a worker thread that overran its budget.
struct BothStages
{
    SupportResult support;
    SliceResult slice;
};

} // namespace

TEST_CASE("Robustness: an empty mesh gives no supports and nothing to slice", "[SLA][robustness]")
{
    SupportResult support;
    REQUIRE_NOTHROW(support = run_support_tool(make_mesh({})));
    check_support_result(support, /*expect_empty*/ true);

    SliceResult slice;
    REQUIRE_NOTHROW(slice = run_slice(make_mesh({})));
    check_slice_result(slice);
    // Nothing to support and nothing to raft.
    CHECK(slice.support_facets == 0);
    CHECK(slice.raft_facets == 0);
}

TEST_CASE("Robustness: a single triangle neither crashes nor hangs", "[SLA][robustness]")
{
    SupportResult support;
    REQUIRE_NOTHROW(support = run_support_tool(make_mesh(single_triangle_its())));
    check_support_result(support, /*expect_empty*/ false);

    SliceResult slice;
    REQUIRE_NOTHROW(slice = run_slice(make_mesh(single_triangle_its())));
    check_slice_result(slice);
}

TEST_CASE("Robustness: a degenerate triangle neither crashes nor hangs", "[SLA][robustness]")
{
    SupportResult support;
    REQUIRE_NOTHROW(support = run_support_tool(make_mesh(degenerate_triangle_its())));
    check_support_result(support, /*expect_empty*/ false);

    SliceResult slice;
    REQUIRE_NOTHROW(slice = run_slice(make_mesh(degenerate_triangle_its())));
    check_slice_result(slice);
}

TEST_CASE("Robustness: an open box neither crashes nor hangs", "[SLA][robustness]")
{
    SupportResult support;
    REQUIRE_NOTHROW(support = run_support_tool(make_mesh(open_box_its(20., 20., 20.))));
    check_support_result(support, /*expect_empty*/ false);

    SliceResult slice;
    REQUIRE_NOTHROW(slice = run_slice(make_mesh(open_box_its(20., 20., 20.))));
    check_slice_result(slice);
}

TEST_CASE("Robustness: a box with a flipped face neither crashes nor hangs", "[SLA][robustness]")
{
    SupportResult support;
    REQUIRE_NOTHROW(support = run_support_tool(make_mesh(flipped_face_box_its(20., 20., 20.))));
    check_support_result(support, /*expect_empty*/ false);

    SliceResult slice;
    REQUIRE_NOTHROW(slice = run_slice(make_mesh(flipped_face_box_its(20., 20., 20.))));
    check_slice_result(slice);
}

TEST_CASE("Robustness: two overlapping cubes in one volume neither crash nor hang", "[SLA][robustness]")
{
    SupportResult support;
    REQUIRE_NOTHROW(support = run_support_tool(make_mesh(overlapping_cubes_its(20.))));
    check_support_result(support, /*expect_empty*/ false);

    SliceResult slice;
    REQUIRE_NOTHROW(slice = run_slice(make_mesh(overlapping_cubes_its(20.))));
    check_slice_result(slice);
}

TEST_CASE("Robustness: a cube with duplicated faces neither crashes nor hangs", "[SLA][robustness]")
{
    SupportResult support;
    REQUIRE_NOTHROW(support = run_support_tool(make_mesh(duplicated_faces_box_its(20., 20., 20.))));
    check_support_result(support, /*expect_empty*/ false);

    SliceResult slice;
    REQUIRE_NOTHROW(slice = run_slice(make_mesh(duplicated_faces_box_its(20., 20., 20.))));
    check_slice_result(slice);
}

TEST_CASE("Robustness: a tiny 0.05 mm cube neither crashes nor hangs", "[SLA][robustness]")
{
    SupportResult support;
    REQUIRE_NOTHROW(support = run_support_tool(make_mesh(box_its(0.05, 0.05, 0.05))));
    check_support_result(support, /*expect_empty*/ false);

    SliceResult slice;
    REQUIRE_NOTHROW(slice = run_slice(make_mesh(box_its(0.05, 0.05, 0.05))));
    check_slice_result(slice);
}

TEST_CASE("Robustness: a 0.02 mm thin plate neither crashes nor hangs", "[SLA][robustness]")
{
    SupportResult support;
    REQUIRE_NOTHROW(support = run_support_tool(make_mesh(box_its(20., 20., 0.02))));
    check_support_result(support, /*expect_empty*/ false);

    SliceResult slice;
    REQUIRE_NOTHROW(slice = run_slice(make_mesh(box_its(20., 20., 0.02))));
    check_slice_result(slice);
}

TEST_CASE("Robustness: a tall thin needle neither crashes nor hangs", "[SLA][robustness]")
{
    // 150 mm is about the maximum print height of the machines this slicer drives.
    SupportResult support;
    REQUIRE_NOTHROW(support = run_support_tool(make_mesh(box_its(0.5, 0.5, 150.))));
    check_support_result(support, /*expect_empty*/ false);

    SliceResult slice;
    REQUIRE_NOTHROW(slice = run_slice(make_mesh(box_its(0.5, 0.5, 150.))));
    check_slice_result(slice);
}

TEST_CASE("Robustness: a cube 1500 mm from the origin neither crashes nor hangs", "[SLA][robustness]")
{
    // The support generator has taken minutes on a mesh with coordinates around 1364 mm
    // (tests/data/overhang.obj, hidden in the island coverage test). Both stages therefore run on
    // a worker thread: the support one with a stop function that fires after the deadline, the
    // slice one with a deadline of its own, since the slice has no stop function.
    const Vec3d offset{1500., 0., 0.};
    const TriangleMesh mesh = make_mesh(box_its(20., 20., 20.));

    auto stages = std::make_shared<BothStages>();

    // A few seconds of slack over the stop function, so a run that ends on the deadline is still
    // seen as finished rather than as a hang.
    const bool support_finished = run_on_worker(
        [stages, mesh, offset] { stages->support = run_support_tool(mesh, offset); },
        support_deadline + std::chrono::seconds{5});

    REQUIRE(support_finished);
    REQUIRE_NOTHROW(check_support_result(stages->support, /*expect_empty*/ false));

    const bool slice_finished = run_on_worker(
        [stages, mesh, offset] { stages->slice = run_slice(mesh, offset); }, slice_deadline);

    REQUIRE(slice_finished);
    REQUIRE_NOTHROW(check_slice_result(stages->slice));
}
