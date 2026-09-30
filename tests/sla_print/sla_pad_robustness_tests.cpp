// M4.6a: the raft (pad) when the object is printed directly on the plate.
//
// Zero elevation is the mode where the raft has to work hardest. The object stands on z = 0, the
// raft is a frame around it, and the footprints that break a pad generator are the awkward ones:
// a thin ring, two parts close enough to be merged into one raft, a slot narrower than the object
// gap, a 1 mm speck, a C, and a ball that touches the plate at one point only, so the first plane
// the pad blueprint samples only grazes it.
//
// Every footprint runs the two stages the product runs: the support tool (SLASupportTool) and a
// full SLAPrint slice. What is asserted is that nothing crashes, that a raft which is generated is
// a closed solid, and that the object's bottom layer ends up on or inside it. The paths where no
// raft can be built are asserted as well, because they are documented outcomes and not crashes: an
// empty raft with a raft-around-the-object type that does not follow the object, NoPadGenerated
// when a raft is required but does not fit, and the cavity the pad logs and drops instead of
// failing.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/IThumbnailImageGenerator.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/SLASupportTool.hpp"
#include "libslic3r/SlicingStatus.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"

#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/Polygon.hpp"
#include "Slic3r/Biz/Algorithms/Tesselate.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/Slicing/BackgroundProcess.hpp"
#include "Slic3r/Domain/Bed.hpp"
#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/Domain/ConfigBoxesSLA.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
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

using Slic3r::ExPolygon;
using Slic3r::ExPolygons;
using Slic3r::Point;
using Slic3r::Points;
using Slic3r::Polygon;
using Slic3r::Polygons;
using Slic3r::scaled;
using Slic3r::unscaled;
using Slic3r::Domain::BoundingBox3d;
using Slic3r::Domain::Index3;
using Slic3r::Domain::Transform3d;
using Slic3r::Domain::TriangleMesh;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec3f;
using Catch::Approx;

namespace Tesselate = Slic3r::Biz::Algorithms::Tesselate;
namespace BizPolygon = Slic3r::Biz::Algorithms::Polygon;

// The slab of every configuration of this file unless a case says otherwise: 2 mm of solid raft
// and no cavity, so what is checked is the raft and not its recess.
const double wall_thickness_mm = 2.;

// Both stages of a case get a deadline. Nothing here is expected to take a minute, and a pad
// generator that hangs is a failing test rather than a test binary that never returns.
const std::chrono::seconds support_deadline{60};
const std::chrono::seconds slice_deadline{60};

// ---------------------------------------------------------------------------------------------
// Footprints, built in code
// ---------------------------------------------------------------------------------------------

// A footprint is a mesh standing on the plate and the support points a user would put on it, which
// are empty where there is no flat area to put them on.
struct Footprint
{
    std::string          name;
    indexed_triangle_set its;
    std::vector<Vec3d>   support_points;
};

ExPolygon footprint_of(const std::vector<std::pair<double, double>> &mm)
{
    ExPolygon poly;
    poly.contour.points.reserve(mm.size());
    for (const auto &p : mm)
        poly.contour.points.emplace_back(scaled<double>(p.first), scaled<double>(p.second));
    return poly;
}

Points circle(double cx, double cy, double r, int n = 64)
{
    Points pts;
    pts.reserve(n);
    for (int i = 0; i < n; ++i) {
        const double a = 2. * PI * i / n;
        pts.emplace_back(scaled<double>(cx + r * std::cos(a)), scaled<double>(cy + r * std::sin(a)));
    }
    return pts;
}

// Six times the volume of the mesh, positive when its faces point out of it. A slicer reads the
// outside of a mesh, so this is the one thing about a hand built mesh that is measured instead of
// reasoned about.
double signed_volume(const indexed_triangle_set &its)
{
    double volume = 0.;
    for (const Index3 &f : its.indices) {
        const Vec3f &a = its.vertices[f[0]];
        const Vec3f &b = its.vertices[f[1]];
        const Vec3f &c = its.vertices[f[2]];
        volume += double(a.x()) * (double(b.y()) * double(c.z()) - double(b.z()) * double(c.y())) -
                  double(a.y()) * (double(b.x()) * double(c.z()) - double(b.z()) * double(c.x())) +
                  double(a.z()) * (double(b.x()) * double(c.y()) - double(b.y()) * double(c.x()));
    }
    return volume / 6.;
}

// A prism standing on the plate over the given footprint, holes included.
indexed_triangle_set prism(const ExPolygon &footprint, double height)
{
    // The union gives the contour and its hole the orientations the tesselator and the wall strips
    // expect from each other.
    const ExPolygons canonical = Slic3r::union_ex(ExPolygons{footprint});
    if (canonical.empty())
        return {};

    const ExPolygon &fp = canonical.front();
    indexed_triangle_set its;
    Slic3r::Domain::its_merge(its, Tesselate::triangulate_expolygon_3d(fp, height, Tesselate::NORMALS_UP));
    Slic3r::Domain::its_merge(its, Tesselate::triangulate_expolygon_3d(fp, 0., Tesselate::NORMALS_DOWN));
    Slic3r::Domain::its_merge(its, Tesselate::wall_strip(fp.contour, 0., height));
    for (const Polygon &hole : fp.holes)
        Slic3r::Domain::its_merge(its, Tesselate::wall_strip(hole, 0., height));

    if (signed_volume(its) < 0.)
        for (Index3 &f : its.indices)
            std::swap(f[0], f[2]);

    return its;
}

// A washer: 2 mm of material in a 20 mm ring, so the offset of the raft wall collapses in the hole
// in the middle and the raft comes out in two pieces.
Footprint ring_footprint()
{
    // An ExPolygon hole runs the other way round from its contour, which is what tells the union
    // below that it is a hole.
    Points hole = circle(10., 10., 8.);
    std::reverse(hole.begin(), hole.end());

    ExPolygon poly;
    poly.contour.points = circle(10., 10., 10.);
    poly.holes.emplace_back(std::move(hole));

    Footprint fp;
    fp.name = "ring";
    fp.its = prism(poly, 6.);
    fp.support_points = {{19., 10., 0.}, {1., 10., 0.}, {10., 19., 0.}, {10., 1., 0.}};
    return fp;
}

// Two 10 x 10 blocks 5 mm apart. The rafts under them are close enough to become one.
Footprint two_blocks_footprint()
{
    const ExPolygon block = footprint_of({{0., 0.}, {10., 0.}, {10., 10.}, {0., 10.}});

    ExPolygon moved = block;
    for (Point &p : moved.contour.points)
        p.x() += scaled<double>(15.);

    Footprint fp;
    fp.name = "two-blocks";
    fp.its = prism(block, 6.);
    Slic3r::Domain::its_merge(fp.its, prism(moved, 6.));
    fp.support_points = {{2.5, 2.5, 0.}, {7.5, 7.5, 0.}, {17.5, 2.5, 0.}, {22.5, 7.5, 0.}};
    return fp;
}

// A 20 x 20 plate with a 3 mm wide slot cut into one side, narrower than twice the object gap, so
// the gap closes the slot up while the raft is being cut out of the hull.
Footprint slotted_plate_footprint()
{
    Footprint fp;
    fp.name = "slotted-plate";
    fp.its = prism(footprint_of({{0., 0.}, {20., 0.}, {20., 20.}, {11.5, 20.}, {11.5, 8.},
                                  {8.5, 8.}, {8.5, 20.}, {0., 20.}}),
                   4.);
    fp.support_points = {{4., 4., 0.}, {16., 4., 0.}, {4., 16., 0.}, {16., 16., 0.}};
    return fp;
}

// A 1 mm cube. A raft whose wall is thicker than the object itself cannot be built around it.
Footprint speck_footprint()
{
    Footprint fp;
    fp.name = "speck";
    fp.its = prism(footprint_of({{0., 0.}, {1., 0.}, {1., 1.}, {0., 1.}}), 1.);
    fp.support_points = {{0.25, 0.25, 0.}, {0.75, 0.25, 0.}, {0.25, 0.75, 0.}, {0.75, 0.75, 0.}};
    return fp;
}

// A 20 x 20 plate with a 10 x 12 bite taken out of one side: a C, whose raft is a C as well and
// whose two arms are only joined around the bite.
Footprint c_shape_footprint()
{
    Footprint fp;
    fp.name = "c-shape";
    fp.its = prism(footprint_of({{0., 0.}, {20., 0.}, {20., 4.}, {10., 4.}, {10., 16.}, {20., 16.},
                                  {20., 20.}, {0., 20.}}),
                   4.);
    fp.support_points = {{4., 4., 0.}, {4., 16., 0.}, {12., 2., 0.}, {12., 18., 0.}};
    return fp;
}

// A ball of radius 10 resting on the plate. Its lowest point is a single vertex on z = 0, which is
// exactly the first plane the pad blueprint samples, so that slice has no area in it at all.
Footprint ball_footprint()
{
    indexed_triangle_set its = Slic3r::Biz::Algorithms::TriangleMesh::its_make_sphere(10., 0.2);
    for (Vec3f &v : its.vertices)
        v.z() += 10.f;

    Footprint fp;
    fp.name = "ball";
    fp.its = std::move(its);
    // Nothing to put a support point on: the contact with the plate is a single point.
    fp.support_points = {};
    return fp;
}

std::vector<Footprint> all_footprints()
{
    std::vector<Footprint> out;
    out.push_back(ring_footprint());
    out.push_back(two_blocks_footprint());
    out.push_back(slotted_plate_footprint());
    out.push_back(speck_footprint());
    out.push_back(c_shape_footprint());
    out.push_back(ball_footprint());
    return out;
}

// ---------------------------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------------------------

// One combination of the things that decide what the raft is: its type, whether supports are on,
// how far the object is lifted off the plate by them, how deep the edge taper bites in, and
// whether the raft follows the object where the supports do not reach. The rest of the knobs are
// set so that a raft really is built around all six footprints instead of being skipped for not
// fitting its own wall.
struct RaftCase
{
    Slic3r::Domain::sla::RaftType raft = Slic3r::Domain::sla::RaftType::Full;
    bool                         supports = true;
    double                       elevation_mm = 0.;
    double                       edge_taper_mm = 0.;
    bool                         everywhere = true;
    double                       slope_deg = 90.;
    double                       thickness_mm = wall_thickness_mm;
    double                       cavity_mm = 0.;
    std::string                  name;
};

Slic3r::Domain::ConfigPackSLA make_pack(const RaftCase &rc)
{
    Slic3r::Domain::ConfigPackSLA pack;
    pack.sla_printer_settings.items.opt("sla_archive_format").set(std::string("SL1"));
    pack.sla_print_settings.items.opt("layer_height").set(0.05);
    pack.sla_material_settings.items.opt("initial_layer_height").set(0.05);

    pack.sla_print_settings.items.opt("supports_enable").set(rc.supports);
    pack.sla_print_settings.items.opt("support_object_elevation").set(rc.elevation_mm);

    pack.sla_print_settings.items.opt("raft_type").set(rc.raft);
    pack.sla_print_settings.items.opt("pad_enable").set(true);
    pack.sla_print_settings.items.opt("raft_edge_taper").set(rc.edge_taper_mm);
    pack.sla_print_settings.items.opt("pad_around_object_everywhere").set(rc.everywhere);
    pack.sla_print_settings.items.opt("pad_wall_thickness").set(rc.thickness_mm);
    pack.sla_print_settings.items.opt("pad_wall_height").set(rc.cavity_mm);
    pack.sla_print_settings.items.opt("pad_wall_slope").set(rc.slope_deg);
    pack.sla_print_settings.items.opt("pad_brim_size").set(4.0);
    pack.sla_print_settings.items.opt("pad_object_gap").set(0.5);
    return pack;
}

// The support tool API takes the raw config pointers and resolves the SLA object view itself.
struct ToolConfig
{
    Slic3r::Domain::FullConfigSLAPtr         full;
    Slic3r::Domain::PartialObjectConfigSLAPtr object_settings;
};

ToolConfig make_tool_config(const Slic3r::Domain::ConfigPackSLA &pack)
{
    ToolConfig cfg;
    cfg.full = std::make_shared<const Slic3r::Domain::FullConfigSLA>(
        pack, Slic3r::Domain::Preset::HwPrinterConfig{.technology = Slic3r::Domain::PrinterTechnology::SLA});
    cfg.object_settings = std::make_shared<const Slic3r::Domain::PartialObjectConfigSLA>(
        Slic3r::Domain::SLAObjectSettings{}, cfg.full->hw_config());
    return cfg;
}

void fill_model(const Footprint &fp, Slic3r::Domain::Model &model)
{
    Slic3r::Domain::ModelObject *object = model.add_object();
    object->name = fp.name + ".stl";
    Slic3r::Biz::Algorithms::ModelObject::add_volume(
        object, TriangleMesh{Slic3r::Biz::Algorithms::TriangleMesh::construct(fp.its)});
    object->add_instance();

    for (const Vec3d &p : fp.support_points)
        object->sla_support_points.emplace_back(Slic3r::Domain::SLA::SupportPoint{
            Vec3f{float(p.x()), float(p.y()), float(p.z())},
            0.2f,
            Slic3r::Domain::SLA::SupportPointType::island});
}

// ---------------------------------------------------------------------------------------------
// Stage 1: the support tool
// ---------------------------------------------------------------------------------------------

// A stop function that fires once its budget has passed. Both support tool entry points poll it
// and return an empty result when it fires, so a stopped run is an outcome, not a failure.
struct Deadline
{
    std::chrono::steady_clock::time_point start{std::chrono::steady_clock::now()};
    std::chrono::seconds budget{support_deadline};

    bool expired() const { return std::chrono::steady_clock::now() - start > budget; }
    Slic3r::sla::SupportToolStop stop() { return [this] { return expired(); }; }
};

struct ToolResult
{
    bool         stopped = false;
    bool         has_tree = false;
    bool         has_pad = false;
    TriangleMesh tree;
    TriangleMesh pad;
};

ToolResult run_support_tool(const Footprint &fp, const RaftCase &rc)
{
    Slic3r::Domain::Model model;
    fill_model(fp, model);
    Slic3r::Domain::ModelObject *object = model.objects.front();
    const ToolConfig cfg = make_tool_config(make_pack(rc));
    const Transform3d trafo = Transform3d::Identity();

    Deadline deadline;
    ToolResult out;

    const Slic3r::Domain::SLA::SupportPoints points = Slic3r::sla::generate_support_points_for_tool(
        *object, trafo, cfg.full, cfg.object_settings, deadline.stop());
    out.stopped = deadline.expired();
    if (out.stopped)
        return out;

    const Slic3r::sla::SupportToolTree tree = Slic3r::sla::build_support_tree_for_tool(
        *object, trafo, points, cfg.full, cfg.object_settings, deadline.stop());
    out.stopped = deadline.expired();

    if (tree.tree) {
        out.has_tree = true;
        out.tree = *tree.tree;
    }
    if (tree.pad) {
        out.has_pad = true;
        out.pad = *tree.pad;
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// Stage 2: a full slice
// ---------------------------------------------------------------------------------------------

// Local stand-in for the fff_print test helper, which is not on this target's include path.
Slic3r::Domain::Preset::SelectedPresetMetadata
make_preset_metadata(const Slic3r::Domain::Preset::HwPrinterConfig &hw_config)
{
    return Slic3r::Domain::Preset::SelectedPresetMetadata{
        .hw_config = hw_config,
        .tools     = std::vector<Slic3r::Domain::Preset::EvaluatedPresetMetadata>{hw_config.tool_count},
        .materials =
            std::vector<Slic3r::Domain::Preset::EvaluatedPresetMetadata>{hw_config.material_slot_count()}};
}

// The tests never look at a thumbnail, so the requests are dropped right away.
class NoopThumbnailGenerator : public Slic3r::Biz::Slicing::IThumbnailImageGenerator
{
public:
    std::future<Slic3r::Biz::Slicing::ThumbnailImageResults> enqueue_thumbnail_requests(
        const Slic3r::Biz::Slicing::ThumbnailImageRequests &) override
    {
        std::promise<Slic3r::Biz::Slicing::ThumbnailImageResults> promise;
        promise.set_value(Slic3r::Biz::Slicing::ThumbnailImageResults{});
        return promise.get_future();
    }

    void handle_enqueued_requests() override {}
};

struct SliceResult
{
    bool         finished = false; // the pipeline ran to the end
    bool         refused = false;  // the engine said no raft can be generated
    std::string  error;
    bool         has_pad = false;
    TriangleMesh pad;
    double       elevation = 0.;
    size_t       layers = 0;
};

SliceResult slice_once(const Footprint &fp, const RaftCase &rc)
{
    SliceResult out;

    Slic3r::Domain::Model model;
    fill_model(fp, model);
    Slic3r::Domain::Bed bed;
    Slic3r::Domain::BedInstance bed_instance{bed};
    for (const Slic3r::Domain::ModelObject *object : model.objects)
        for (Slic3r::Domain::ModelInstance *instance : object->instances)
            bed_instance.model_instances.push_back(instance);

    Slic3r::Domain::ConfigPackSLA config = make_pack(rc);
    const auto hw_config =
        Slic3r::Test::create_dummy_hw_config(1, 0, Slic3r::Domain::PrinterTechnology::SLA);
    const auto preset_metadata = make_preset_metadata(hw_config);
    const auto metadata = Slic3r::Biz::Slicing::build_gcode_metadata({}, preset_metadata, config);

    Slic3r::SLAPrint print{[](Slic3r::Biz::Slicing::SLAResult &&) {},
                           [](const Slic3r::Biz::Slicing::Sla::Object &) {}};

    try {
        print.update(model, config, bed_instance, preset_metadata,
                     Slic3r::Biz::Slicing::build_metadata_serializer(metadata, preset_metadata, config));

        NoopThumbnailGenerator thumbnail_generator;
        print.slice(Slic3r::Domain::SlicingId{0, 0}, thumbnail_generator, std::nullopt);
    } catch (const Slic3r::Biz::Slicing::Exception &e) {
        // A model the engine cannot raft is a handled outcome: this is the error the bed shows.
        out.refused = e.error().code == Slic3r::Biz::Slicing::ErrorCode::NoPadGenerated;
        out.error = e.what();
        return out;
    }

    out.finished = true;
    out.layers = print.print_layers().size();
    if (print.objects().empty())
        return out;

    const Slic3r::SLAPrintObject &po = *print.objects().front();
    out.elevation = po.get_elevation();
    if (po.pad_mesh().facets_count() > 0) {
        out.has_pad = true;
        out.pad = po.pad_mesh();
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// A job on a worker thread, so that a hang is a failing test and not a test binary that never
// returns
// ---------------------------------------------------------------------------------------------

struct WorkerState
{
    std::mutex              mutex;
    std::condition_variable done;
    bool                    finished = false;
};

// Runs `job` on a worker thread and waits for it. Returns false if the job was still running after
// `budget`, in which case the thread is detached and left to finish on its own: the test fails and
// the process still exits. The job has to own everything it touches.
template <typename Job> bool run_with_deadline(Job &&job, std::chrono::seconds budget)
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

// ---------------------------------------------------------------------------------------------
// What is checked on a raft
// ---------------------------------------------------------------------------------------------

// Is the raft a closed solid? Every edge of it has to be shared by exactly two faces, one of each
// winding: a raft with a hole in it is resin that never sets.
bool is_closed(const indexed_triangle_set &its)
{
    std::map<std::pair<size_t, size_t>, int> balance;
    for (const Index3 &f : its.indices) {
        for (int e = 0; e < 3; ++e) {
            const size_t a = size_t(f[e]);
            const size_t b = size_t(f[(e + 1) % 3]);
            if (a == b)
                continue; // a degenerate edge of a degenerate face bounds no surface
            balance[{std::min(a, b), std::max(a, b)}] += a < b ? 1 : -1;
        }
    }

    for (const auto &edge : balance)
        if (edge.second != 0)
            return false;
    return true;
}

double area_of(const ExPolygons &polys)
{
    double area = 0.;
    for (const ExPolygon &poly : polys)
        area += poly.area();
    return area;
}

double area_of(const Polygons &polys)
{
    return BizPolygon::area(polys);
}

// The cross section of a mesh at one height, empty if the mesh is not there.
ExPolygons cross_section(const indexed_triangle_set &its, double z)
{
    const std::vector<ExPolygons> slices = Slic3r::slice_mesh_ex(its, {float(z)}, 0.f);
    return slices.empty() ? ExPolygons{} : slices.front();
}

// The XY extent of a set of scaled polygons, in mm.
struct Extent
{
    double min_x = 1e30;
    double max_x = -1e30;
    double min_y = 1e30;
    double max_y = -1e30;
};

Extent xy_extent(const ExPolygons &polys)
{
    Extent extent;
    const auto take = [&extent](const Points &pts) {
        for (const Point &p : pts) {
            extent.min_x = std::min(extent.min_x, double(unscaled(p.x())));
            extent.max_x = std::max(extent.max_x, double(unscaled(p.x())));
            extent.min_y = std::min(extent.min_y, double(unscaled(p.y())));
            extent.max_y = std::max(extent.max_y, double(unscaled(p.y())));
        }
    };

    for (const ExPolygon &poly : polys) {
        take(poly.contour.points);
        for (const Polygon &hole : poly.holes)
            take(hole.points);
    }
    return extent;
}

// The support points as small squares, for asking what stands where.
ExPolygons support_squares(const Footprint &fp, double half = 0.2)
{
    ExPolygons out;
    for (const Vec3d &p : fp.support_points)
        out.emplace_back(footprint_of({{p.x() - half, p.y() - half},
                                        {p.x() + half, p.y() - half},
                                        {p.x() + half, p.y() + half},
                                        {p.x() - half, p.y() + half}}));
    return out;
}

// Does the raft hug the object (raft around the object) or stand under the supports that hold it up
// (full plate)?
bool is_around(const Slic3r::Domain::sla::RaftType raft)
{
    return raft == Slic3r::Domain::sla::RaftType::AroundObject ||
           raft == Slic3r::Domain::sla::RaftType::Skate;
}

// A raft is expected for every raft type that prints one, as long as something holds it up: a raft
// under the object needs supports to stand on, a raft around the object does not.
bool raft_expected(const RaftCase &rc, const Footprint &fp)
{
    if (rc.raft == Slic3r::Domain::sla::RaftType::None)
        return false;
    if (is_around(rc.raft))
        return true;
    return rc.supports && !fp.support_points.empty();
}

// Everything the raft has to be for the object to be printable on it. Every footprint of this file
// stands on z = 0, which is where the object is printed from unless supports lift it.
void check_raft(const RaftCase &rc, const Footprint &fp, const TriangleMesh &pad)
{
    REQUIRE_FALSE(pad.empty());
    const BoundingBox3d bb = pad.bounding_box();

    INFO("raft " << bb.min.x() << "," << bb.min.y() << " .. " << bb.max.x() << "," << bb.max.y()
                 << ", z " << bb.min.z() << " .. " << bb.max.z() << ", " << pad.facets_count()
                 << " facets");

    // A raft is a solid, not a shell: resin only sets where the light reaches both ways.
    CHECK(is_closed(pad.its));

    // The cross section in the lower quarter of the raft, where the infill and the taper are not.
    const double z = bb.min.z() + 0.25 * (bb.max.z() - bb.min.z());
    const ExPolygons section = cross_section(pad.its, z);
    REQUIRE_FALSE(section.empty());

    if (is_around(rc.raft)) {
        // A raft around the object stands on the plate and reaches up to its wall. A cavity is cut
        // into it from below, so it hangs that far below the plate.
        CHECK(bb.max.z() == Approx(rc.thickness_mm).margin(0.05));
        CHECK(bb.min.z() == Approx(-rc.cavity_mm).margin(0.05));

        // The object's bottom layer is inside the raft's outline: the raft is built around the
        // object, so its footprint reaches past the object on every side.
        const ExPolygons bottom_layer = cross_section(fp.its, 0.025);
        REQUIRE_FALSE(bottom_layer.empty());
        const Extent object = xy_extent(bottom_layer);
        CHECK(bb.min.x() <= object.min_x + 0.05);
        CHECK(bb.max.x() >= object.max_x - 0.05);
        CHECK(bb.min.y() <= object.min_y + 0.05);
        CHECK(bb.max.y() >= object.max_y - 0.05);

        // And the raft does not bury it: the object stands in the hole of the raft and only the
        // connector fingers the generator cuts along the outline reach under it.
        const double object_area = area_of(bottom_layer);
        const double buried = area_of(Slic3r::intersection(bottom_layer, section));
        INFO("the raft section at z " << z << " buries " << buried << " mm2 of " << object_area
                                      << " mm2");
        CHECK(buried < 0.2 * object_area);
        return;
    }

    // A raft under the object stands on the plate at the foot of the pillars that hold the object
    // up, so it is that much below the object's bottom face and nothing is buried in it.
    CHECK(bb.max.z() == Approx(-rc.elevation_mm).margin(0.05));
    CHECK(bb.min.z() == Approx(-rc.elevation_mm - rc.thickness_mm - rc.cavity_mm).margin(0.05));
    CHECK(bb.max.z() < 0.);

    // The raft is under the supports, or it is holding nothing up.
    const ExPolygons points = support_squares(fp);
    REQUIRE_FALSE(points.empty());
    CHECK(area_of(Slic3r::intersection(section, points)) > 0.);
}

// Both stages of one combination: no crash, and a raft that is a closed solid with the object on
// or inside it.
void check_footprint(const Footprint &fp, const RaftCase &rc)
{
    INFO("footprint " << fp.name << ", " << rc.name);
    CAPTURE(fp.name, rc.name);

    ToolResult tool;
    REQUIRE(run_with_deadline([&tool, &fp, &rc] { tool = run_support_tool(fp, rc); }, support_deadline));
    if (tool.stopped)
        WARN("the support tool ran out of time on this footprint");

    SliceResult slice;
    REQUIRE(run_with_deadline([&slice, &fp, &rc] { slice = slice_once(fp, rc); }, slice_deadline));

    if (slice.refused) {
        // A documented outcome, not a crash: the engine told us it cannot build that raft.
        INFO("the engine reported: " << slice.error);
        CHECK(slice.layers == 0);
        return;
    }

    CHECK(slice.finished);
    CHECK(slice.layers > 0);

    if (!raft_expected(rc, fp)) {
        // The documented no-raft cases: this raft type asks for none, or there is nothing to hold
        // a raft under the object up.
        CHECK_FALSE(slice.has_pad);
        return;
    }

    REQUIRE(slice.has_pad);
    check_raft(rc, fp, slice.pad);

    // A raft around the object is printed in place, a raft under the object is as high as the
    // supports and the raft under them.
    if (is_around(rc.raft))
        CHECK(slice.elevation == Approx(0.));
    else
        CHECK(slice.elevation == Approx(rc.elevation_mm + rc.thickness_mm).margin(0.01));

    // The support tool generates its own points, so it may find none on a footprint that has no
    // room for any. What it builds when it finds some has to be the same raft as the slice's.
    if (!tool.has_pad || tool.pad.empty())
        return;
    check_raft(rc, fp, tool.pad);
}

} // namespace

TEST_CASE("A raft on the plate survives an awkward footprint", "[SLA][PadRobustness]")
{
    using Slic3r::Domain::sla::RaftType;

    // Every raft type with the edge taper off and on. The two types that raft around the object
    // print with the object on the plate, with and without supports; a raft under the object
    // needs the supports to be lifted by something, so those cases ask for 5 mm of elevation.
    // (A raft under an object that stands on the plate has its own test case below.)
    std::vector<RaftCase> cases;
    for (const RaftType raft : {RaftType::None, RaftType::Full, RaftType::AroundObject, RaftType::Skate}) {
        for (const double taper : {0., 1.}) {
            const std::string tag = "raft " + std::to_string(int(raft)) + ", taper " + std::to_string(taper);
            cases.push_back(RaftCase{.raft           = raft,
                                     .elevation_mm   = is_around(raft) ? 0. : (raft == RaftType::Full ? 5. : 0.),
                                     .edge_taper_mm  = taper,
                                     .name           = tag});
            if (is_around(raft) && taper == 0.)
                cases.push_back(RaftCase{.raft = raft, .supports = false, .name = tag + ", no supports"});
        }
    }

    for (const Footprint &fp : all_footprints())
        for (const RaftCase &rc : cases)
            check_footprint(fp, rc);
}

TEST_CASE("Two footprints close together get one raft over the gap between them",
          "[SLA][PadRobustness]")
{
    using Slic3r::Domain::sla::RaftType;

    const Footprint fp = two_blocks_footprint();
    const RaftCase rc{.raft = RaftType::AroundObject, .name = "merged"};

    ToolResult tool;
    REQUIRE(run_with_deadline([&tool, &fp, &rc] { tool = run_support_tool(fp, rc); }, support_deadline));
    REQUIRE_FALSE(tool.stopped);
    REQUIRE(tool.has_pad);
    REQUIRE_FALSE(tool.pad.empty());
    check_raft(rc, fp, tool.pad);

    // The two rafts are one raft, so the gap between the blocks is raft and not a gap of resin
    // that never sets. The middle of the gap, in the lower quarter of the raft, is solid.
    const BoundingBox3d bb = tool.pad.bounding_box();
    const double z = bb.min.z() + 0.25 * (bb.max.z() - bb.min.z());
    const ExPolygons section = cross_section(tool.pad.its, z);
    REQUIRE_FALSE(section.empty());

    const ExPolygons probe{footprint_of({{12.4, 4.9}, {12.6, 4.9}, {12.6, 5.1}, {12.4, 5.1}})};
    CHECK(area_of(Slic3r::intersection(section, probe)) > 0.);
}

TEST_CASE("A raft under an object that stands on the plate is refused, not crashed",
          "[SLA][PadRobustness]")
{
    using Slic3r::Domain::sla::RaftType;

    // A raft under the object is a slab at the foot of the pillars, so it needs the object to be
    // lifted. With no elevation there are no pillars: nothing holds the raft up, and the engine
    // says so instead of printing resin that never sets.
    const Footprint fp = c_shape_footprint();
    const RaftCase rc{.raft = RaftType::Full, .elevation_mm = 0., .name = "full plate, no elevation"};

    SECTION("with support points there is nothing to build the raft on")
    {
        REQUIRE_FALSE(fp.support_points.empty());

        SliceResult slice;
        REQUIRE(run_with_deadline([&slice, &fp, &rc] { slice = slice_once(fp, rc); }, slice_deadline));
        CHECK(slice.refused);
        CHECK(slice.layers == 0);

        ToolResult tool;
        REQUIRE(run_with_deadline([&tool, &fp, &rc] { tool = run_support_tool(fp, rc); },
                                  support_deadline));
        CHECK_FALSE(tool.has_pad);
    }

    SECTION("without support points the object is printed standing on the plate")
    {
        const Footprint bare = ball_footprint();

        SliceResult slice;
        REQUIRE(run_with_deadline([&slice, &bare, &rc] { slice = slice_once(bare, rc); }, slice_deadline));
        CHECK(slice.finished);
        CHECK(slice.layers > 0);
        CHECK_FALSE(slice.has_pad);
        CHECK(slice.elevation == Approx(0.));
    }
}

TEST_CASE("A raft that does not fit is a clean error, not a crash", "[SLA][PadRobustness]")
{
    using Slic3r::Domain::sla::RaftType;

    // A raft around the object with a wall that is thicker than the whole raft: the wall cannot be
    // offset inwards, so there is no raft at all.
    const Footprint fp = speck_footprint();
    const RaftCase impossible{.raft         = RaftType::AroundObject,
                              .slope_deg    = 45.,
                              .thickness_mm = 6.,
                              .name         = "wall thicker than the raft"};

    SECTION("required and impossible: the engine says so")
    {
        SliceResult slice;
        REQUIRE(run_with_deadline([&slice, &fp, &impossible] { slice = slice_once(fp, impossible); },
                                  slice_deadline));
        CHECK(slice.refused);
        CHECK(slice.layers == 0);

        ToolResult tool;
        REQUIRE(run_with_deadline([&tool, &fp, &impossible] { tool = run_support_tool(fp, impossible); },
                                  support_deadline));
        CHECK_FALSE(tool.has_pad);
    }

    SECTION("the same raft optional: the object is printed without one")
    {
        RaftCase optional = impossible;
        optional.everywhere = false;

        SliceResult slice;
        REQUIRE(run_with_deadline([&slice, &fp, &optional] { slice = slice_once(fp, optional); },
                                  slice_deadline));
        CHECK(slice.finished);
        CHECK(slice.layers > 0);
        CHECK_FALSE(slice.has_pad);
    }
}

TEST_CASE("A raft around the object on the plate needs the everywhere option", "[SLA][PadRobustness]")
{
    using Slic3r::Domain::sla::RaftType;

    // On the plate the supports are heads on the object and reach nothing, so every piece of a
    // raft around the object is redundant with them and the raft comes out empty. That is not an
    // error: the object is printed standing on the plate.
    const Footprint fp = c_shape_footprint();
    const RaftCase everywhere_off{.raft = RaftType::AroundObject, .everywhere = false, .name = "no everywhere"};
    const RaftCase everywhere_on{.raft = RaftType::AroundObject, .everywhere = true, .name = "everywhere"};

    SECTION("without it the raft is empty and nothing fails")
    {
        ToolResult tool;
        REQUIRE(run_with_deadline([&tool, &fp, &everywhere_off] { tool = run_support_tool(fp, everywhere_off); },
                                  support_deadline));
        CHECK_FALSE(tool.stopped);
        // The tool hands the empty raft over rather than none at all: a raft around the object
        // which is not required everywhere is allowed to be empty.
        CHECK(tool.has_pad);
        CHECK(tool.pad.empty());

        SliceResult slice;
        REQUIRE(run_with_deadline([&slice, &fp, &everywhere_off] { slice = slice_once(fp, everywhere_off); },
                                  slice_deadline));
        CHECK(slice.finished);
        CHECK(slice.layers > 0);
        CHECK_FALSE(slice.has_pad);
    }

    SECTION("with it the raft is built around the object")
    {
        ToolResult tool;
        REQUIRE(run_with_deadline([&tool, &fp, &everywhere_on] { tool = run_support_tool(fp, everywhere_on); },
                                  support_deadline));
        REQUIRE_FALSE(tool.stopped);
        REQUIRE(tool.has_pad);
        REQUIRE_FALSE(tool.pad.empty());
        check_raft(everywhere_on, fp, tool.pad);
    }
}

TEST_CASE("A cavity the raft cannot hold is dropped and the raft is not", "[SLA][PadRobustness]")
{
    using Slic3r::Domain::sla::RaftType;

    // A raft with a cavity is a slab with a recess in it. A raft too small for the recess is
    // logged ("Could not create pad cavity") and printed as a flat raft, which is the documented
    // way out of it and not a hole in the raft. A 1 mm cube under a 6 mm wall has no room for a
    // 1 mm deep recess.
    const Footprint fp = speck_footprint();
    const RaftCase rc{.raft         = RaftType::AroundObject,
                      .thickness_mm = 6.,
                      .cavity_mm    = 1.,
                      .name         = "cavity does not fit"};

    SliceResult slice;
    REQUIRE(run_with_deadline([&slice, &fp, &rc] { slice = slice_once(fp, rc); }, slice_deadline));
    CHECK(slice.finished);
    REQUIRE(slice.has_pad);
    check_raft(rc, fp, slice.pad);
}
