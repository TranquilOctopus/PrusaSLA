// M4.4a: the four suspected causes of the auto support miss the maintainer reported on a helmet
// model, each one measured on a mesh built in code before anything was changed. The list is the
// one the result note of M4.3c leaves behind:
//
//   1. get_small_parts()/erase() of the support point generator drop a model part that fits into
//      a 0.2 mm sphere whole, so a speck that floats in the air gets no support point at all.
//   2. move_on_mesh_surface() replaces a point with the closest point of the mesh when the
//      vertical ray misses it by more than one layer height, which can slide it off the region it
//      was sampled for.
//   3. the zero elevation filter of SLASupportTool.cpp drops every point at the plate.
//   4. a region that is not an island is only sampled where sample_overhangs() finds an overhang
//      contour, so a shallow overhang under the sampling step can end up unsampled.
//
// Every case runs the pipeline the plater's auto support runs (sla::generate_support_points_for_tool
// on the default config, sliced at the layer height the benchmark harness uses) and then the island
// coverage rule of sla_island_coverage_tests.cpp: an island the M4.8d rule reports - a region of a
// layer with nothing below it and at least min_island_area_mm2 of area - has to have a support
// point within 0.5 mm of it in Z and within sqrt(area_mm2) + 1 mm of its centroid in XY, or it
// falls off during printing.
//
// The local minima of M4.3c are not asserted here, and deliberately so. A local minimum is a
// region that does overlap the layer below (so it is not an island) and whose area grows upwards: a
// valley in the outside of a dome, or the bottom of a cavity, both of which hang on the material
// below them. A support point there is resin in a self supporting corner, so that list is a list of
// places to look at, not a pass or a fail.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "Slic3r/Biz/Algorithms/AABBMesh.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/ConfigCommon.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
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
#include "libslic3r/SLA/SupportPointGenerator.hpp"
#include "libslic3r/SLASupportTool.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"

namespace {

// indexed_triangle_set comes from admesh/stl.h and lives in the global namespace.
using ::indexed_triangle_set;
using Slic3r::Domain::ExPolygons;
using Slic3r::Domain::TriangleMesh;
using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Vec2d;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::SLA::SupportPoint;
using Slic3r::Domain::SLA::SupportPoints;
using Slic3r::SLA::IslandHit;

// What the tests slice at. The default of the layer_height key is 0.3 mm, an FFF leftover, and the
// benchmark harness and the local island coverage test of M4.3c both slice at 0.05.
constexpr double LAYER_HEIGHT_MM = 0.05;
// A support point covers an island within this distance in Z ...
constexpr double COVER_DZ_MM = 0.5;
// ... and within the island's own radius plus this in XY.
constexpr double COVER_RADIUS_PAD_MM = 1.0;

struct SlaConfig
{
    Slic3r::Domain::FullConfigSLAPtr          full;
    Slic3r::Domain::PartialObjectConfigSLAPtr object_settings;
};

/// The default SLA config the cases run. A raft around the object is the zero elevation case: the
/// object is printed straight onto the raft, so the tool drops the points that would sit on the
/// plate.
SlaConfig make_config(bool zero_elevation)
{
    Slic3r::Domain::ConfigPackSLA pack;
    pack.sla_print_settings.items.opt("layer_height").set(LAYER_HEIGHT_MM);
    pack.sla_print_settings.items.opt("supports_enable").set(true);
    if (zero_elevation) {
        pack.sla_print_settings.items.opt("raft_type")
            .set(Slic3r::Domain::sla::RaftType::AroundObject);
        pack.sla_print_settings.items.opt("pad_around_object_everywhere").set(true);
    }

    const Slic3r::Domain::Preset::HwPrinterConfig hw_config{
        .technology = Slic3r::Domain::PrinterTechnology::SLA};

    SlaConfig cfg;
    cfg.full = std::make_shared<const Slic3r::Domain::FullConfigSLA>(pack, hw_config);
    cfg.object_settings = std::make_shared<const Slic3r::Domain::PartialObjectConfigSLA>(
        Slic3r::Domain::SLAObjectSettings{}, cfg.full->hw_config());
    return cfg;
}

/// The slicing parameters the support point generator itself uses, so the layers the coverage rule
/// reads are the layers the generator worked on.
Slic3r::MeshSlicingParamsEx slicing_params(const SlaConfig& config)
{
    const Slic3r::SLAPrintObjectConfigView cfg{config.full, config.object_settings};

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
    return params;
}

/// The slice heights the generator uses: the middle of every layer of the mesh, from its bottom.
std::vector<float> slice_heights(const TriangleMesh& mesh, const SlaConfig& config)
{
    const Slic3r::SLAPrintObjectConfigView cfg{config.full, config.object_settings};
    const double layer_height = Slic3r::Domain::sla_effective_layer_height(cfg);
    const auto   bb           = mesh.bounding_box();

    std::vector<float> heights;
    if (!(layer_height > 0.))
        return heights;
    for (double z = bb.min.z() + layer_height * 0.5; z < bb.max.z(); z += layer_height)
        heights.push_back(float(z));
    return heights;
}

/// The volumes of one mesh, as one model object with one instance and no transform, so the object
/// frame is the world frame and the points come back where they were made.
struct MeshModel
{
    Slic3r::Domain::Model        model;
    Slic3r::Domain::ModelObject* object = nullptr;

    explicit MeshModel(const std::vector<indexed_triangle_set>& volumes)
    {
        object = model.add_object();
        for (const indexed_triangle_set& its : volumes)
            Slic3r::Biz::Algorithms::ModelObject::add_volume(
                object, Slic3r::Biz::Algorithms::TriangleMesh::construct(its));
        object->add_instance();
    }

    TriangleMesh mesh() const
    {
        indexed_triangle_set its;
        for (const Slic3r::Domain::ModelVolume* vol : object->volumes) {
            indexed_triangle_set vol_mesh = vol->mesh().its;
            Slic3r::Domain::its_merge(its, std::move(vol_mesh));
        }
        return Slic3r::Biz::Algorithms::TriangleMesh::construct(std::move(its));
    }
};

/// A box of the given size, its lower corner at the given place [in mm].
indexed_triangle_set box(double x, double y, double z, const Vec3d& at = Vec3d::Zero())
{
    indexed_triangle_set its = Slic3r::Biz::Algorithms::TriangleMesh::its_make_cube(x, y, z);
    for (Slic3r::Domain::Vec3f& v : its.vertices)
        v += at.cast<float>();
    return its;
}

/// A cylinder of the given radius and height, its lower face at the given place [in mm].
indexed_triangle_set disc(double r, double h, const Vec3d& at = Vec3d::Zero())
{
    indexed_triangle_set its = Slic3r::Biz::Algorithms::TriangleMesh::its_make_cylinder(r, h);
    for (Slic3r::Domain::Vec3f& v : its.vertices)
        v += at.cast<float>();
    return its;
}

/// Does a support point cover the island, by the rule of sla_island_coverage_tests.cpp?
bool covered(const SupportPoints& points, double z, const IslandHit& island)
{
    const double max_xy_dist = std::sqrt(island.area_mm2) + COVER_RADIUS_PAD_MM;
    for (const SupportPoint& point : points) {
        const double dz = std::abs(double(point.pos.z()) - z);
        if (dz > COVER_DZ_MM)
            continue;

        const double dx = double(point.pos.x()) - island.centroid.x();
        const double dy = double(point.pos.y()) - island.centroid.y();
        if (std::sqrt(dx * dx + dy * dy) <= max_xy_dist)
            return true;
    }
    return false;
}

/// The support points of one model and what the island rule says about them.
struct Coverage
{
    SupportPoints          points;
    std::vector<IslandHit> islands;
    /// The islands of islands[] that no support point reaches.
    std::vector<size_t> uncovered;
    std::vector<float>     heights;
};

Coverage analyse(const MeshModel& model, const SlaConfig& config)
{
    Coverage           out;
    const TriangleMesh mesh = model.mesh();

    const Transform3d object_to_world{Transform3d::Identity()};
    out.points = Slic3r::sla::generate_support_points_for_tool(
        *model.object, object_to_world, config.full, config.object_settings, {});

    out.heights = slice_heights(mesh, config);
    if (out.heights.empty())
        return out;

    const std::vector<ExPolygons> layers =
        Slic3r::slice_mesh_ex(mesh.its, out.heights, slicing_params(config));
    out.islands = Slic3r::SLA::detect_islands(layers, Slic3r::SLA::min_island_area_mm2);
    for (size_t i = 0; i < out.islands.size(); ++i) {
        const IslandHit& island = out.islands[i];
        if (island.layer_index >= out.heights.size())
            continue;
        if (!covered(out.points, double(out.heights[island.layer_index]), island))
            out.uncovered.push_back(i);
    }
    return out;
}

/// What a case has to say about its mesh when a check of it fails: the points the tool made, the
/// islands the rule found and the ones that no point reaches.
std::string summary(const Coverage& coverage, const std::string& what)
{
    std::string out = what + ": " + std::to_string(coverage.points.size()) + " support points, " +
                      std::to_string(coverage.islands.size()) + " islands, " +
                      std::to_string(coverage.uncovered.size()) + " of them without a point";
    for (const size_t i : coverage.uncovered) {
        const IslandHit& island = coverage.islands[i];
        out += "; uncovered island at layer " + std::to_string(island.layer_index) + ", z " +
               std::to_string(coverage.heights[island.layer_index]) + " mm, " +
               std::to_string(island.area_mm2) + " mm2, centroid (" +
               std::to_string(island.centroid.x()) + ", " + std::to_string(island.centroid.y()) +
               ")";
    }
    return out;
}

/// The generator's own steps, so a case can see what move_on_mesh_surface() did to the points the
/// sampling made. The default constructed SupportPointGeneratorConfig is what SLASupportTool.cpp
/// builds for the default config: density 100 %, no minimal point distance, a 0.4 mm head and the
/// island configuration of that head.
struct MovedPoints
{
    Slic3r::sla::LayerSupportPoints generated;
    SupportPoints                       moved;

    /// The furthest a point was carried away from where the sampling left it [in mm].
    double max_move() const
    {
        double worst = 0.;
        for (size_t i = 0; i < moved.size() && i < generated.size(); ++i) {
            const Vec3d from = generated[i].pos.cast<double>();
            const Vec3d to   = moved[i].pos.cast<double>();
            worst            = std::max(worst, (to - from).norm());
        }
        return worst;
    }
};

MovedPoints run_generator(const TriangleMesh& mesh, const SlaConfig& config)
{
    MovedPoints out;
    const std::vector<float> heights = slice_heights(mesh, config);
    if (heights.size() < 2)
        return out;

    std::vector<ExPolygons> slices = Slic3r::slice_mesh_ex(mesh.its, heights, slicing_params(config));

    Slic3r::sla::PrepareSupportConfig       prepare;
    Slic3r::sla::SupportPointGeneratorData data =
        Slic3r::sla::prepare_generator_data(std::move(slices), heights, prepare);

    out.generated =
        Slic3r::sla::generate_support_points(data, Slic3r::sla::SupportPointGeneratorConfig{});

    // One layer height, the same allowed move the tool passes.
    const double allowed_move = double(heights[1] - heights[0]) + std::numeric_limits<float>::epsilon();
    const Slic3r::AABBMesh emesh{mesh};
    out.moved = Slic3r::sla::move_on_mesh_surface(out.generated, emesh, allowed_move);
    return out;
}

/// The support points standing in the given rectangle of the plate [in mm].
std::vector<const SupportPoint*> points_in_column(const SupportPoints& points, double x0, double y0,
                                                   double x1, double y1)
{
    std::vector<const SupportPoint*> out;
    for (const SupportPoint& point : points) {
        if (double(point.pos.x()) < x0 || double(point.pos.x()) > x1)
            continue;
        if (double(point.pos.y()) < y0 || double(point.pos.y()) > y1)
            continue;
        out.push_back(&point);
    }
    return out;
}

/// The furthest a point on the given polyline is from the nearest support point of the given
/// layer, in XY [in mm]. Used for an overhang, which the island rule does not see.
double max_gap_on_edge(const SupportPoints& points, const std::vector<Vec2d>& edge, double z,
                       double dz_tolerance)
{
    double worst = 0.;
    for (const Vec2d& at : edge) {
        double nearest = std::numeric_limits<double>::max();
        for (const SupportPoint& point : points) {
            if (std::abs(double(point.pos.z()) - z) > dz_tolerance)
                continue;
            const double dx = double(point.pos.x()) - at.x();
            const double dy = double(point.pos.y()) - at.y();
            nearest          = std::min(nearest, std::sqrt(dx * dx + dy * dy));
        }
        worst = std::max(worst, nearest);
    }
    return worst;
}

/// The outline of the box between the given corners, walked in steps of the given size [in mm].
std::vector<Vec2d> box_outline(double x0, double y0, double x1, double y1, double step)
{
    std::vector<Vec2d> edge;
    for (double x = x0; x < x1; x += step)
        edge.push_back(Vec2d(x, y0));
    for (double y = y0; y < y1; y += step)
        edge.push_back(Vec2d(x1, y));
    for (double x = x1; x > x0; x -= step)
        edge.push_back(Vec2d(x, y1));
    for (double y = y1; y > y0; y -= step)
        edge.push_back(Vec2d(x0, y));
    return edge;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Cause 1: the small parts erase of the generator (reproduced, fixed)
// ---------------------------------------------------------------------------------------------
TEST_CASE("M4.4a: a floating part smaller than the support head keeps its support point",
          "[SLA][SupportRegression]")
{
    // A 10 x 10 x 20 mm block on the plate and, 5 mm to the side of it and 4 mm up, a disc of
    // 0.38 mm diameter and 0.15 mm height that touches nothing. The disc is smaller than the
    // support head (0.4 mm), so get_small_parts() collects it and erase() dropped it whole: the
    // tool made no point under it at all. It is also a region of a layer with nothing below it and
    // 0.11 mm2 of area, which is an island the app reports and tells the user can fall off.
    MeshModel model({box(10., 10., 20.), disc(0.19, 0.15, Vec3d{15., 0., 4.0})});

    const Coverage coverage = analyse(model, make_config(false));
    INFO(summary(coverage, "a block and a floating disc of 0.38 mm diameter"));

    // The disc is the one island of this mesh, and it is the finding the erase lost.
    REQUIRE(coverage.islands.size() == 1);
    CHECK(coverage.uncovered.empty());
}

TEST_CASE("M4.4a: a small part connected to a brim is not erased", "[SLA][SupportRegression]")
{
    // The other half of cause 1, which the code gets right and this keeps that way. A 0.4 x 0.4
    // mm spike hanging under a brim is a candidate for the erase: its first layer is an island
    // and its bounding box is exactly the 0.4 mm the erase looks for. It is not erased because it
    // is connected: the flood fill of create_small_part() walks up the spike and out into the
    // brim, which does not fit into the 0.2 mm sphere, so no small part is found. The spike is
    // 0.16 mm2 of island, the app reports it, and it gets its point from the island sampling.
    MeshModel model({box(10., 10., 8.), disc(7., 1.5, Vec3d{5., 5., 7.9}),
                     box(0.4, 0.4, 0.6, Vec3d{11., 4.8, 7.5})});

    const Coverage coverage = analyse(model, make_config(false));
    INFO(summary(coverage, "a block with a brim and a small spike hanging under it"));

    // The tip of the spike is the one island of this mesh, and it is covered.
    REQUIRE(coverage.islands.size() == 1);
    CHECK(coverage.uncovered.empty());
}

// ---------------------------------------------------------------------------------------------
// Cause 2: the move on mesh surface of the generator (not reproduced)
// ---------------------------------------------------------------------------------------------
TEST_CASE("M4.4a: a part under a brim keeps the point the surface move could slide away",
          "[SLA][SupportRegression]")
{
    // A 10 x 10 x 10 mm block, a brim of 30 mm diameter over it, and a 4 x 4 x 0.3 mm plate
    // floating 1.7 mm under the brim and clear of the block. The point the sampling makes for the
    // plate's first layer is 1.975 mm under the brim: the vertical ray up misses it by far more
    // than a layer height, which is what the report M4.3c quotes would send to the brim. The ray
    // down finds the plate's own bottom face 0.025 mm below, so the point keeps its place. The
    // underside of the brim is an overhang of its own, like the plate, so the overhang sampling
    // puts points on that face at z = 10 and some of them stand in the plate's footprint; the two
    // sets of points share the column below and are told apart by their height.
    MeshModel model({box(10., 10., 10.), disc(15., 1., Vec3d{5., 5., 10.}),
                     box(4., 4., 0.3, Vec3d{10.5, 3., 8.0})});

    const SlaConfig config   = make_config(false);
    const Coverage  coverage = analyse(model, config);
    INFO(summary(coverage, "a brim and a plate floating under it"));

    // The plate's first layer is an island and the point made for it holds it.
    REQUIRE(coverage.islands.size() >= 1);
    CHECK(coverage.uncovered.empty());

    // The same question asked of the points themselves, in the column the plate stands in: the
    // column is the plate's own footprint, because the point that holds it is kept at least the
    // head radius (0.2 mm for the 0.4 mm head) inside the outline and a tighter rectangle would
    // miss the very point this case is about. It is the brim's own footprint where the brim hangs
    // over the plate, so the column also holds the points the overhang sampling makes on the
    // bottom face of the brim at z = 10. Those are the brim's, and they hold the brim, so the
    // column is not asked to be all at the height of the plate: what the case is about is the
    // plate's own point, and whether anything was carried off the plate on the way.
    const std::vector<const SupportPoint*> column =
        points_in_column(coverage.points, 10.5, 3., 14.5, 7.);
    INFO("support points in the column of the plate: " << column.size());
    REQUIRE_FALSE(column.empty());

    // The point that holds the plate stands at the plate's own bottom face, 8.0 mm, so one point of
    // the column has to be there: 7.9 to 8.2 mm is the bottom of the plate within a layer of the
    // 0.05 mm this slices at.
    std::string holds;
    std::string carried;
    for (const SupportPoint* point : column) {
        const std::string at = "(" + std::to_string(point->pos.x()) + ", " +
                               std::to_string(point->pos.y()) + ", " +
                               std::to_string(point->pos.z()) + ")";
        if (point->pos.z() >= 7.9f && point->pos.z() <= 8.2f)
            holds += " " + at;
        if (point->pos.z() > 8.3f && point->pos.z() < 9.9f)
            carried += " " + at;
    }

    // Nothing of the plate was carried off towards the brim: move_on_mesh_surface() replaces a
    // point with the closest point of the mesh when the vertical ray misses it by more than a
    // layer height, and a point slid off the plate would come to rest on the bottom face of the
    // brim, in the 1.7 mm gap between the two, holding neither. A point at the height of the brim
    // itself is where the brim wants it and is left alone.
    INFO("points of the column at the height of the plate: " << holds);
    CHECK_FALSE(holds.empty());
    INFO("points of the column in the gap between the plate and the brim: " << carried);
    CHECK(carried.empty());

    // How far the move step carried anything, for the record: it puts the points that are inside
    // the 1 mm thick brim onto its surface and it leaves the rest where the sampling put them.
    const MovedPoints moved = run_generator(model.mesh(), config);
    INFO("the move step carried a point " << moved.max_move() << " mm");
    REQUIRE(moved.generated.size() == moved.moved.size());
}

// ---------------------------------------------------------------------------------------------
// Cause 3: the zero elevation filter of the tool (not reproduced)
// ---------------------------------------------------------------------------------------------
TEST_CASE("M4.4a: a feature right at the plate is supported with a raft around the object",
          "[SLA][SupportRegression]")
{
    // The filter drops every point at the model's z = 0 when the raft hugs the object, because a
    // support that starts on the plate is resin and not a support. A 1.5 x 1.5 x 0.5 mm plate
    // hovering 0.5 mm above the plate, 4 mm to the side of a block and clear of its brim, has its
    // point on the plate 0.5 mm above it, so the filter's epsilon is 5000 times below it and
    // never takes it away.
    MeshModel model({box(10., 10., 10.), disc(7., 1., Vec3d{5., 5., 2.}),
                     box(1.5, 1.5, 0.5, Vec3d{14.25, 0., 0.5})});

    const Coverage coverage = analyse(model, make_config(true));
    INFO(summary(coverage, "zero elevation, a block, a brim and a plate above the plate"));

    // The first layer of the plate is an island and no filter takes its point away.
    REQUIRE(coverage.islands.size() >= 1);
    CHECK(coverage.uncovered.empty());
}

// ---------------------------------------------------------------------------------------------
// Cause 4: the overhang sampling of the generator (not reproduced)
// ---------------------------------------------------------------------------------------------
TEST_CASE("M4.4a: a shallow overhang under the sampling step gets its support points",
          "[SLA][SupportRegression]")
{
    // A 2 x 2 x 10 mm post on the plate with a 14 x 14 x 2 mm plate on top of it: the underside
    // of that plate overhangs 6 mm on every side, and the part is not an island, so its points
    // come from sample_overhangs() and not from the island sampling. The overhang contour is
    // walked in steps of discretize_overhang_step (2 mm) and a sample that lands within the
    // influence radius (3.2 mm) of a point already made is dropped, so the edge ends up covered
    // by points about 4 mm apart. Nothing of that is a width an overhang can fall through: an
    // overhang of any width is sampled along its whole contour, and min_part_length and the
    // outline distances of SampleConfig belong to the island sampling, which an overhang never
    // reaches.
    MeshModel model({box(2., 2., 10.), box(14., 14., 2., Vec3d{-6., -6., 10.})});

    const SlaConfig config   = make_config(false);
    const Coverage  coverage = analyse(model, config);
    INFO(summary(coverage, "a post with a wide plate on top"));
    REQUIRE_FALSE(coverage.points.empty());

    // The overhang is not an island, so the island rule has nothing to say about this mesh: it is
    // reported, not asserted, and the check below is the one that measures the overhang. Points
    // are placed along the edge of the overhang every 2 mm of contour (sample_overhangs) and every
    // 4.87 mm of outline (sample_outline, for the peninsula the same part is), and the ones that
    // fall inside the influence radius of a point already made are dropped, so the edge ends up
    // with a point every 3 mm or so. Unsampled, the nearest point would be the one under the post,
    // 8 mm away.
    const double gap = max_gap_on_edge(coverage.points, box_outline(-6., -6., 8., 8., 0.5),
                                       10. + LAYER_HEIGHT_MM * 0.5, COVER_DZ_MM);
    INFO("the furthest point of the overhang edge is " << gap << " mm from a support point");
    CHECK(gap <= 3.0);
}
