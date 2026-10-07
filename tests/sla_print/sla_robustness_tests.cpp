// M6.1: robustness meshes. The SLA pipeline has to survive meshes it cannot print: an empty
// shell, a single triangle, flipped normals, non-manifold edges, a vertex that is not a number,
// a model a hundred kilometres from the origin. None of them may crash the slicer and none of them
// may hang it.
//
// Every mesh here is built in code (no files from tests/data) and every case runs the same two
// stages: the support tool (points, then tree and raft) and a full SLAPrint slice. Both stages run
// on a worker thread under a watchdog. A stage that is still going when its budget runs out is
// asked to stop (the support tool has a stop function, the slice gets the stop token of the
// print), and a stage that neither finishes nor takes the stop fails the test instead of hanging
// the test binary.
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "libslic3r/SLASupportTool.hpp"
#include "libslic3r/CanceledException.hpp"
#include "libslic3r/ConfigViews.hpp"
#include "libslic3r/IThumbnailImageGenerator.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/SlicingStatus.hpp"
#include "jthread/JThread.hpp"

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
using Slic3r::Biz::JThread::StopSource;
using Slic3r::Biz::JThread::StopToken;
using Slic3r::Domain::BoundingBox3d;
using Slic3r::Domain::Index3;
using Slic3r::Domain::TriangleMesh;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec3f;

// The engine's own way of saying "this model cannot be sliced": a message for the bed. Nothing else
// may escape a stage.
using EngineRefusal = Slic3r::Biz::Slicing::Exception;

// The support generator has run for minutes on a mesh far from the origin (see the last case), so
// every support call gets a deadline instead of a hope. A call that runs out of time is reported
// as stopped, not as a failure: the deadline bounds the test, it does not assert a speed.
const std::chrono::seconds support_deadline{60};

// A few seconds of slack over the stop function, so a support call that ends on the deadline is
// seen as finished rather than as a hang.
const std::chrono::seconds support_slack{5};

// The slice has no deadline of its own, so it runs on a worker thread with this budget instead. It
// is generous: nothing in this file is expected to take minutes.
const std::chrono::seconds slice_deadline{60};

// How much longer a stage may run after it has been asked to stop. Unwinding a slice takes a
// moment: it stops at the next place that polls its stop token.
const std::chrono::seconds cancel_grace{30};

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

// A closed box with a square hole in the middle of its top face: the top is a ring of eight
// triangles around the hole, so the shell is closed but the surface has a hole in it.
indexed_triangle_set holed_box_its(double x, double y, double z)
{
    indexed_triangle_set its = open_box_its(x, y, z);

    // The rim of the hole, halfway between the corners and the middle of the top face.
    const float fx = float(x), fy = float(y), fz = float(z);
    const int h = int(its.vertices.size());
    its.vertices.insert(its.vertices.end(),
                        {{fx / 4.f, fy / 4.f, fz},
                         {3 * fx / 4.f, fy / 4.f, fz},
                         {3 * fx / 4.f, 3 * fy / 4.f, fz},
                         {fx / 4.f, 3 * fy / 4.f, fz}});
    its.indices.insert(its.indices.end(), {{4, 5, h + 1}, {4, h + 1, h},
                                          {5, 6, h + 2}, {5, h + 2, h + 1},
                                          {6, 7, h + 3}, {6, h + 3, h + 2},
                                          {7, 4, h}, {7, h, h + 3}});
    return its;
}

// A box with the winding of one face reversed, so that face points into the volume.
indexed_triangle_set flipped_face_box_its(double x, double y, double z)
{
    indexed_triangle_set its = box_its(x, y, z);
    std::swap(its.indices[8][0], its.indices[8][2]); // the first triangle of the back face
    return its;
}

// A box with every face turned inwards: the whole volume has a negative orientation.
indexed_triangle_set inverted_box_its(double x, double y, double z)
{
    indexed_triangle_set its = box_its(x, y, z);
    for (Index3& face : its.indices)
        std::swap(face[0], face[2]);
    return its;
}

// A box with a fin glued to the edge between the corners 0 and 1. That edge now belongs to three
// faces, which no closed surface can do.
indexed_triangle_set non_manifold_box_its(double x, double y, double z)
{
    indexed_triangle_set its = box_its(x, y, z);
    const int tip = int(its.vertices.size());
    its.vertices.push_back(Vec3f{float(x) / 2.f, -float(y), float(z)});
    its.indices.push_back(Index3{0, 1, tip});
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

// A box with one of its triangles repeated at the very same coordinates, with a second, identical
// set of vertices: two coincident faces, as a mesh from a broken exporter carries.
indexed_triangle_set duplicate_triangle_its(double x, double y, double z)
{
    indexed_triangle_set its = box_its(x, y, z);
    const Index3 face = its.indices.front();
    const Vec3f a = its.vertices[face[0]], b = its.vertices[face[1]], c = its.vertices[face[2]];
    const int base = int(its.vertices.size());
    its.vertices.insert(its.vertices.end(), {a, b, c});
    its.indices.push_back(Index3{base, base + 1, base + 2});
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

// Two cubes that pass through each other: a vertical bar and a horizontal one, both centred. Every
// face of either shell is crossed by the other shell, and no vertex is shared.
indexed_triangle_set crossing_cubes_its(double size)
{
    const float third = float(size) / 3.f;
    indexed_triangle_set vertical = box_its(third, third, size);
    for (Vec3f& v : vertical.vertices)
        v += Vec3f{third, third, 0.f};
    indexed_triangle_set horizontal = box_its(size, third, third);
    for (Vec3f& v : horizontal.vertices)
        v += Vec3f{0.f, third, third};
    Slic3r::Domain::its_merge(vertical, horizontal);
    return vertical;
}

// Two cubes that only touch, along a whole face. Welding them makes every edge of the shared face
// an edge of four triangles.
indexed_triangle_set touching_cubes_its(double size)
{
    indexed_triangle_set first = box_its(size, size, size);
    indexed_triangle_set second = box_its(size, size, size);
    for (Vec3f& v : second.vertices)
        v += Vec3f{float(size), 0.f, 0.f};
    Slic3r::Domain::its_merge(first, second);
    return first;
}

// One triangle in the XY plane.
indexed_triangle_set single_triangle_its()
{
    return {{{0, 1, 2}}, {{0.f, 0.f, 0.f}, {10.f, 0.f, 0.f}, {0.f, 10.f, 0.f}}};
}

// One triangle whose three points lie on a line: it covers no area at all.
indexed_triangle_set degenerate_triangle_its()
{
    return {{{0, 1, 2}}, {{0.f, 0.f, 0.f}, {5.f, 0.f, 0.f}, {10.f, 0.f, 0.f}}};
}

// A box with a vertex that is not a coordinate: one NaN and one infinity.
indexed_triangle_set non_finite_its(double x, double y, double z)
{
    indexed_triangle_set its = box_its(x, y, z);
    its.vertices[1][0] = std::numeric_limits<float>::quiet_NaN();
    its.vertices[6][2] = std::numeric_limits<float>::infinity();
    return its;
}

TriangleMesh make_mesh(indexed_triangle_set its)
{
    return Slic3r::Biz::Algorithms::TriangleMesh::construct(std::move(its));
}

// ---------------------------------------------------------------------------------------------
// One mesh in the robustness set
// ---------------------------------------------------------------------------------------------

struct RobustnessCase
{
    const char* name;                               // what is wrong with the mesh
    std::function<indexed_triangle_set()> build;    // the mesh itself

    Slic3r::Domain::Vec3d offset{0., 0., 0.};       // where the object stands
    double layer_height{0.05};                      // mm
    bool supports{true};                            // supports and raft on or off
    bool must_have_no_points{false};                // nothing to generate from this mesh
    bool must_be_rejected{false};                   // the engine has to refuse it cleanly
    std::chrono::seconds support_budget{support_deadline}; // how long the support tool may take
    std::chrono::seconds slice_budget{slice_deadline};
};

RobustnessCase robustness_case(const char* name, std::function<indexed_triangle_set()> build)
{
    return RobustnessCase{name, std::move(build)};
}

// ---------------------------------------------------------------------------------------------
// Model and config
// ---------------------------------------------------------------------------------------------

// A model with one object holding `mesh`, moved to `offset`.
struct MeshModel
{
    Slic3r::Domain::Model model;
    Slic3r::Domain::ModelObject* object{nullptr};

    MeshModel(TriangleMesh mesh, const Vec3d& offset = {0., 0., 0.})
    {
        const BoundingBox3d bb = mesh.bounding_box();
        const float dx = float(bb.max.x() - bb.min.x());
        const float dy = float(bb.max.y() - bb.min.y());
        const bool has_footprint = std::isfinite(dx) && std::isfinite(dy) && dx > 0.f && dy > 0.f;

        object = model.add_object();
        object->name = "robustness.stl";

        // The slice takes the support points from the object instead of generating them, and an
        // object without points is sliced without supports. Put points on the bottom face so that
        // the tree and the raft are built for every case that has a footprint to put them on. A
        // box that does not measure anything (or measures NaN) gets none.
        if (has_footprint) {
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

// Supports on and a raft under the object (a full plate one, so the pillars stand on it; the raft
// type is named explicitly (M7.8.4b changed the default to None, which would build no raft), a
// 0.05 mm layer.
SlaConfig make_sla_config(double layer_height, bool supports)
{
    Slic3r::Domain::ConfigPackSLA pack;
    pack.sla_print_settings.items.opt("layer_height").set(layer_height);
    pack.sla_print_settings.items.opt("supports_enable").set(supports);
    pack.sla_print_settings.items.opt("raft_type")
        .set(supports ? Slic3r::Domain::sla::RaftType::Full : Slic3r::Domain::sla::RaftType::None);
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

// Generates the support points and builds the tree and the raft for the mesh of `test_case`.
SupportResult run_support_tool(const TriangleMesh& mesh, const RobustnessCase& test_case)
{
    MeshModel model{mesh, test_case.offset};
    SlaConfig config = make_sla_config(test_case.layer_height, test_case.supports);
    const Slic3r::Domain::Transform3d trafo = object_to_world(test_case.offset);
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

// The stop token of a print running on a worker thread. The pipeline polls it (PrintBase throws
// CanceledException at the next place that checks), so it is how the watchdog asks a slice that is
// past its deadline to stop.
struct PrintStop
{
    StopSource source{std::make_unique<std::atomic<bool>>(false)};

    void request_stop() { source->store(true); }
    StopToken token() { return StopToken{source}; }
};

struct SliceResult
{
    bool finished{false};              // the pipeline ran to the end
    bool canceled{false};              // the watchdog stopped it
    bool reported_error{false};        // the engine refused the model, as it shows on the bed
    bool unexpected_exception{false};  // something that is not an engine refusal got out
    bool geometry_finite{true};        // no NaN in the support or raft geometry it produced
    std::string error;
    size_t layers{0};
    size_t support_facets{0};
    size_t raft_facets{0};
};

// Slices the mesh of `test_case` with the standard SLA preset.
//
// A model the engine cannot slice is not a robustness failure: it throws a Slic3r::RuntimeError or
// a Slic3r::Biz::Slicing::Exception, which BackgroundProcess turns into an error on the bed
// (BackgroundProcess.cpp:269). That is a handled outcome and is reported here. Anything else
// escaping the slice is recorded as an unexpected exception, and the slice never returning is what
// the watchdog above is for.
SliceResult run_slice(const TriangleMesh& mesh, const RobustnessCase& test_case,
                      const std::shared_ptr<PrintStop>& stop)
{
    SliceResult result;

    MeshModel model{mesh, test_case.offset};
    Slic3r::Domain::Bed bed;
    Slic3r::Domain::BedInstance bed_instance{bed};
    for (const Slic3r::Domain::ModelObject* object : model.model.objects) {
        for (Slic3r::Domain::ModelInstance* instance : object->instances)
            bed_instance.model_instances.push_back(instance);
    }

    Slic3r::Domain::ConfigPackSLA config;
    config.sla_print_settings.items.opt("layer_height").set(test_case.layer_height);
    config.sla_material_settings.items.opt("initial_layer_height").set(test_case.layer_height);
    config.sla_print_settings.items.opt("supports_enable").set(test_case.supports);
    config.sla_print_settings.items.opt("raft_type")
        .set(test_case.supports ? Slic3r::Domain::sla::RaftType::Full
                                : Slic3r::Domain::sla::RaftType::None);

    const auto hw_config =
        Slic3r::Test::create_dummy_hw_config(1, 0, Slic3r::Domain::PrinterTechnology::SLA);
    const auto preset_metadata = make_preset_metadata(hw_config);
    const auto metadata = Slic3r::Biz::Slicing::build_gcode_metadata({}, preset_metadata, config);

    Slic3r::SLAPrint print{
        [](Slic3r::Biz::Slicing::SLAResult&&) {}, [](const Slic3r::Biz::Slicing::Sla::Object&) {}};
    print.stop_token = stop->token();

    try {
        print.update(model.model, config, bed_instance, preset_metadata,
                     Slic3r::Biz::Slicing::build_metadata_serializer(metadata, preset_metadata, config));

        NoopThumbnailGenerator thumbnail_generator;
        print.slice(Slic3r::Domain::SlicingId{0, 0}, thumbnail_generator, std::nullopt);
    } catch (const Slic3r::Biz::Slicing::CanceledException&) {
        result.canceled = true;
        return result;
    } catch (const Slic3r::RuntimeError& e) {
        result.reported_error = true;
        result.error = e.what();
        return result;
    } catch (const EngineRefusal& e) {
        // The engine's own error carries the reason in its Error, not in what().
        std::ostringstream message;
        message << e.what() << ": " << e.error();
        result.reported_error = true;
        result.error = message.str();
        return result;
    } catch (const std::exception& e) {
        result.unexpected_exception = true;
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

// The checks every case makes on the slice: the pipeline came back, one way or the other, a refusal
// carries a message for the user, and nothing but an engine refusal got out.
void check_slice_result(const SliceResult& result, bool must_be_rejected, bool deadline_fired)
{
    INFO("layers: " << result.layers << ", support facets: " << result.support_facets
                    << ", raft facets: " << result.raft_facets);
    if (result.unexpected_exception)
        INFO("an exception that is not an engine refusal got out: " << result.error);
    CHECK_FALSE(result.unexpected_exception);

    if (result.reported_error) {
        INFO("the engine reported: " << result.error);
        // A refusal without a message leaves the user with nothing to act on.
        CHECK_FALSE(result.error.empty());
    }

    if (result.canceled) {
        // Nothing cancels a slice but the watchdog.
        CHECK(deadline_fired);
    }

    if (must_be_rejected) {
        CHECK(result.reported_error);
        return;
    }

    if (!result.finished && !result.reported_error && !result.canceled)
        INFO("the pipeline came back without finishing and without an error");

    // Catch2 decomposes the expression itself, so the disjunction has to be parenthesized.
    CHECK((result.finished || result.reported_error || result.canceled));
    if (result.finished)
        CHECK(result.geometry_finite);
}

// ---------------------------------------------------------------------------------------------
// A stage on a worker thread, so that a hang is a failing test and not a test binary that never
// returns
// ---------------------------------------------------------------------------------------------

struct WorkerState
{
    std::mutex mutex;
    std::condition_variable done;
    bool finished{false};
};

// Runs `job` on a worker thread and waits `budget` for it. Returns true if the job finished inside
// the budget. If it did not, `on_timeout` is called (that is where a running slice is asked to
// stop) and the job is given `cancel_grace` more to unwind. False means the job is still running
// after the cancel: a hang, and a failing test.
//
// The job owns everything it touches and the worker state is shared, so a thread that outlives this
// function has nothing left to write to.
template <typename Job>
bool run_with_watchdog(Job&& job, std::chrono::seconds budget,
                       const std::function<void()>& on_timeout)
{
    auto state = std::make_shared<WorkerState>();
    const auto work = [state, job = std::forward<Job>(job)]() mutable {
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
    };
    std::future<void> future = std::async(std::launch::async, work);

    bool finished = false;
    {
        std::unique_lock<std::mutex> lock{state->mutex};
        finished = state->done.wait_for(lock, budget, [&state] { return state->finished; });
    }

    if (finished)
        return true;

    // Only a stage with something to ask gets a callback: the support tool is given its own stop
    // function with its own deadline, so nothing is asked of it here, and an empty callback must
    // not be called (that is a std::bad_function_call, which would leave the test case with an
    // exception instead of the hang the message below is about).
    if (on_timeout)
        on_timeout();

    {
        std::unique_lock<std::mutex> lock{state->mutex};
        finished = state->done.wait_for(lock, cancel_grace, [&state] { return state->finished; });
    }

    if (!finished) {
        // The future of std::async waits for its task in the destructor, which is exactly what must
        // not happen for a job that does not stop. The task holds nothing but its own state, so the
        // future is leaked and the thread ends when the process does.
        new std::future<void>{std::move(future)};
    }

    return finished;
}

// ---------------------------------------------------------------------------------------------
// One case: both stages, each under its own watchdog
// ---------------------------------------------------------------------------------------------

struct CaseResults
{
    SupportResult support;
    SliceResult slice;
};

CaseResults run_case(const RobustnessCase& test_case)
{
    // The job copies what it needs: a thread that outlives this function must not read the
    // caller's storage.
    const RobustnessCase c{test_case};
    const TriangleMesh mesh = make_mesh(c.build());

    auto support = std::make_shared<SupportResult>();
    const bool support_finished = run_with_watchdog(
        [support, mesh, c] { *support = run_support_tool(mesh, c); },
        c.support_budget + support_slack, {});

    if (!support_finished)
        FAIL("the support tool is still running " << (c.support_budget + support_slack).count()
             << " s after it started, although it was given a stop function");
    check_support_result(*support, c.must_have_no_points);

    auto slice = std::make_shared<SliceResult>();
    auto stop = std::make_shared<PrintStop>();
    auto deadline_fired = std::make_shared<std::atomic<bool>>(false);
    const bool slice_finished = run_with_watchdog(
        [slice, stop, mesh, c] { *slice = run_slice(mesh, c, stop); },
        c.slice_budget,
        [stop, deadline_fired] {
            deadline_fired->store(true);
            stop->request_stop();
        });

    if (!slice_finished)
        FAIL("the slice is still running " << c.slice_budget.count() << " s after it started, "
             << "and it did not stop when it was asked to");

    check_slice_result(*slice, c.must_be_rejected, deadline_fired->load());
    return CaseResults{*support, *slice};
}

} // namespace

TEST_CASE("Robustness: an empty mesh gives no supports and nothing to slice", "[SLA][robustness]")
{
    RobustnessCase test_case =
        robustness_case("an empty mesh", [] { return indexed_triangle_set{}; });
    test_case.must_have_no_points = true;
    INFO("mesh: " << test_case.name);

    const CaseResults results = run_case(test_case);
    // Nothing to support and nothing to raft.
    CHECK(results.slice.support_facets == 0);
    CHECK(results.slice.raft_facets == 0);
}

TEST_CASE("Robustness: a single triangle neither crashes nor hangs", "[SLA][robustness]")
{
    RobustnessCase test_case =
        robustness_case("a single triangle", single_triangle_its);
    INFO("mesh: " << test_case.name);
    run_case(test_case);
}

TEST_CASE("Robustness: a degenerate triangle neither crashes nor hangs", "[SLA][robustness]")
{
    RobustnessCase test_case =
        robustness_case("a degenerate, zero area triangle", degenerate_triangle_its);
    INFO("mesh: " << test_case.name);
    run_case(test_case);
}

TEST_CASE("Robustness: an open box neither crashes nor hangs", "[SLA][robustness]")
{
    RobustnessCase test_case = robustness_case(
        "a box with its top face missing", [] { return open_box_its(20., 20., 20.); });
    INFO("mesh: " << test_case.name);
    run_case(test_case);
}

TEST_CASE("Robustness: a box with a hole in a face neither crashes nor hangs", "[SLA][robustness]")
{
    RobustnessCase test_case = robustness_case(
        "a box with a square hole in its top face", [] { return holed_box_its(20., 20., 20.); });
    INFO("mesh: " << test_case.name);
    run_case(test_case);
}

TEST_CASE("Robustness: a box with a flipped face neither crashes nor hangs", "[SLA][robustness]")
{
    RobustnessCase test_case = robustness_case(
        "a box with one face turned inwards", [] { return flipped_face_box_its(20., 20., 20.); });
    INFO("mesh: " << test_case.name);
    run_case(test_case);
}

TEST_CASE("Robustness: a box with every normal inverted neither crashes nor hangs", "[SLA][robustness]")
{
    RobustnessCase test_case = robustness_case(
        "a box with every face turned inwards", [] { return inverted_box_its(20., 20., 20.); });
    INFO("mesh: " << test_case.name);
    run_case(test_case);
}

TEST_CASE("Robustness: a mesh with a non-manifold edge neither crashes nor hangs", "[SLA][robustness]")
{
    RobustnessCase test_case = robustness_case(
        "a box with three faces on one edge", [] { return non_manifold_box_its(20., 20., 20.); });
    INFO("mesh: " << test_case.name);
    run_case(test_case);
}

TEST_CASE("Robustness: a cube with duplicated faces neither crashes nor hangs", "[SLA][robustness]")
{
    RobustnessCase test_case = robustness_case(
        "a cube with its faces twice", [] { return duplicated_faces_box_its(20., 20., 20.); });
    INFO("mesh: " << test_case.name);
    run_case(test_case);
}

TEST_CASE("Robustness: a mesh with a duplicate triangle neither crashes nor hangs", "[SLA][robustness]")
{
    RobustnessCase test_case =
        robustness_case("a box with one triangle twice at the same coordinates",
                        [] { return duplicate_triangle_its(20., 20., 20.); });
    INFO("mesh: " << test_case.name);
    run_case(test_case);
}

TEST_CASE("Robustness: two overlapping cubes in one volume neither crash nor hang", "[SLA][robustness]")
{
    RobustnessCase test_case = robustness_case(
        "two cubes merged into one overlapping volume", [] { return overlapping_cubes_its(20.); });
    INFO("mesh: " << test_case.name);
    run_case(test_case);
}

TEST_CASE("Robustness: two crossing shells neither crash nor hang", "[SLA][robustness]")
{
    RobustnessCase test_case =
        robustness_case("two crossing cubes", [] { return crossing_cubes_its(40.); });
    INFO("mesh: " << test_case.name);
    run_case(test_case);
}

TEST_CASE("Robustness: two cubes touching at a face neither crash nor hang", "[SLA][robustness]")
{
    RobustnessCase test_case =
        robustness_case("two cubes touching along a face", [] { return touching_cubes_its(20.); });
    INFO("mesh: " << test_case.name);
    run_case(test_case);
}

TEST_CASE("Robustness: a tiny 0.05 mm cube neither crashes nor hangs", "[SLA][robustness]")
{
    RobustnessCase test_case =
        robustness_case("a 0.05 mm cube", [] { return box_its(0.05, 0.05, 0.05); });
    INFO("mesh: " << test_case.name);
    run_case(test_case);
}

TEST_CASE("Robustness: a 0.02 mm thin plate neither crashes nor hangs", "[SLA][robustness]")
{
    RobustnessCase test_case =
        robustness_case("a 20 x 20 x 0.02 mm plate", [] { return box_its(20., 20., 0.02); });
    INFO("mesh: " << test_case.name);
    run_case(test_case);
}

TEST_CASE("Robustness: a 0.1 mm needle neither crashes nor hangs", "[SLA][robustness]")
{
    // 150 mm is about the maximum print height of the machines this slicer drives.
    RobustnessCase test_case =
        robustness_case("a 0.1 x 0.1 x 150 mm needle", [] { return box_its(0.1, 0.1, 150.); });
    INFO("mesh: " << test_case.name);
    run_case(test_case);
}

TEST_CASE("Robustness: a one metre mesh neither crashes nor hangs", "[SLA][robustness]")
{
    // A metre cube at a 0.05 mm layer height is twenty thousand layers of a plate the size of the
    // bed. The mesh is what is being tested here, not the layer count, so it is sliced with a layer
    // height that keeps the number of layers in the tens. What the generator costs is the area of
    // the layers it samples and a metre wide layer is a million times the area of a 20 mm cube, so
    // both stages get the longer budget the case below asks for instead of the file's 60 s. The
    // its_convex_hull errors of the log are this mesh too: qhull cannot hull coordinates that far
    // out, which leaves the volume with an empty cached hull.
    RobustnessCase test_case =
        robustness_case("a one metre cube", [] { return box_its(1000., 1000., 1000.); });
    test_case.layer_height = 100.;
    test_case.support_budget = std::chrono::seconds{180};
    test_case.slice_budget = std::chrono::seconds{180};
    INFO("mesh: " << test_case.name);
    run_case(test_case);
}

TEST_CASE("Robustness: a cube far from the origin neither crashes nor hangs", "[SLA][robustness]")
{
    // The support generator has taken minutes on a mesh with coordinates around 1364 mm
    // (tests/data/overhang.obj, hidden in the island coverage test), and this one is a hundred
    // times further out. Both stages therefore get a longer budget, and the watchdog cancels the
    // slice if it does not come back on its own.
    RobustnessCase test_case =
        robustness_case("a cube 100 km from the origin", [] { return box_its(20., 20., 20.); });
    test_case.offset = Vec3d{100000., 0., 0.};
    test_case.support_budget = std::chrono::seconds{180};
    test_case.slice_budget = std::chrono::seconds{180};
    INFO("mesh: " << test_case.name);
    run_case(test_case);
}

TEST_CASE("Robustness: a model floating above the plate without supports neither crashes nor hangs",
          "[SLA][robustness]")
{
    // Nothing holds the object up and nothing was generated to hold it up: the layers start in
    // mid-air, well above the plate.
    RobustnessCase test_case = robustness_case(
        "a cube 50 mm above the plate, supports off", [] { return box_its(20., 20., 20.); });
    test_case.offset = Vec3d{0., 0., 50.};
    test_case.supports = false;
    INFO("mesh: " << test_case.name);
    run_case(test_case);
}

TEST_CASE("Robustness: a mesh with a vertex that is not a number is refused", "[SLA][robustness]")
{
    // A NaN and an infinity among the vertices. Nothing about this mesh can be sliced: the engine
    // has to say so with a message instead of slicing garbage, and the support tool has to come
    // back with nothing rather than with a point at infinity.
    RobustnessCase test_case =
        robustness_case("a box with a NaN and an infinity among its vertices",
                        [] { return non_finite_its(20., 20., 20.); });
    test_case.must_have_no_points = true;
    test_case.must_be_rejected = true;
    INFO("mesh: " << test_case.name);
    run_case(test_case);
}