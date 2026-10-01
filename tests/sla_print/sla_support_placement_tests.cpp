// M2.22: the automatic support placement honours a minimal distance between the points it
// creates and an overhang angle threshold. Both defaults keep every point the algorithm made
// before, so the tests only look at a changed setting. Island points are never filtered out,
// which is what the second test case is about.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/MTUtils.hpp"
#include "libslic3r/SLA/SupportIslands/SampleConfig.hpp"
#include "libslic3r/SLA/SupportPointGenerator.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"
#include "libslic3r/libslic3r.h"
#include "sla_test_utils.hpp"

using Catch::Approx;
namespace triangle_mesh = Slic3r::Biz::Algorithms::TriangleMesh;
using Slic3r::Domain::SLA::SupportPointType;
using Slic3r::sla::LayerSupportPoint;
using Slic3r::sla::LayerSupportPoints;
using Slic3r::sla::PrepareSupportConfig;
using Slic3r::sla::SupportPointGeneratorConfig;

namespace {

// A cone that widens upwards, which is what has to be standing on the plate for the cases below to
// ask the generator about overhangs at all: every layer is a larger disc than the one below it, so
// the overhang outline runs along the whole circumference and the generator puts slope points all
// around it. The first layer is the island of the small footprint the tip stands on.
//
// `make_cone` builds the other cone: its wide base is on the plate and it narrows upwards, so every
// layer is a *smaller* disc than the one below it, the overhang outline is empty and the generator
// returns no slope point at all. The cone is mirrored in Z - which keeps the faces wound outwards,
// so the normals and the overhang rule read them as they are - and lifted so that the tip it now
// stands on clears the plate and its first layer is a real island.
//
// A wide and low cone (slope about 22 degrees) has a long overhang outline, a steep one (about 63
// degrees) is steeper than the threshold the angle test uses.
Slic3r::Domain::TriangleMesh cone_mesh(double radius, double height)
{
    Slic3r::Domain::TriangleMesh mesh = triangle_mesh::make_cone(radius, height);
    mesh.mirror(Slic3r::Domain::Axis::Z);
    // The mirrored cone hangs from z = 0 down to z = -height, tip at the bottom. Lifting it by half
    // its height again stands the tip that far above the plate, so its first layer is a disc of half
    // the radius to be the island of.
    mesh.translate(Slic3r::Domain::Vec3f{0.f, 0.f, float(1.5 * height)});

    return mesh;
}

// Slice the mesh and run the generator, but keep the points where the generator put them:
// move_on_mesh_surface would shift every one of them by up to a layer height, which is more
// than the distances measured below.
LayerSupportPoints generate_points(const Slic3r::Domain::TriangleMesh &mesh,
                                   const SupportPointGeneratorConfig  &cfg           = {},
                                   const PrepareSupportConfig          &prepare_cfg = {})
{
    using Slic3r::Biz::Algorithms::BoundingBox::cast;

    const Slic3r::Domain::BoundingBox3f bb = cast<float>(mesh.bounding_box());
    const std::vector<float>             heights{Slic3r::grid(bb.min.z(), bb.max.z(), 0.1f)};
    std::vector<Slic3r::ExPolygons>     slices{
        Slic3r::slice_mesh_ex(mesh.its, heights, CLOSING_RADIUS)};

    const Slic3r::sla::SupportPointGeneratorData data =
        Slic3r::sla::prepare_generator_data(std::move(slices), heights, prepare_cfg);

    return Slic3r::sla::generate_support_points(data, cfg);
}

std::vector<const LayerSupportPoint *> points_of_type(const LayerSupportPoints &points,
                                                       SupportPointType          type)
{
    std::vector<const LayerSupportPoint *> result;
    result.reserve(points.size());
    for (const LayerSupportPoint &point : points)
        if (point.type == type)
            result.push_back(&point);

    return result;
}

// The distance of the two closest overhang points, in the layer they were made on. The points
// are stored in scaled coordinates, so the difference is exact and so is the comparison with
// the configured distance.
double closest_overhang_distance(const LayerSupportPoints &points)
{
    const std::vector<const LayerSupportPoint *> overhangs =
        points_of_type(points, SupportPointType::slope);

    double closest_sq = std::numeric_limits<double>::max();
    for (size_t i = 0; i < overhangs.size(); ++i) {
        for (size_t j = i + 1; j < overhangs.size(); ++j) {
            const double diff_x = static_cast<double>(overhangs[i]->position_on_layer.x() -
                                                       overhangs[j]->position_on_layer.x());
            const double diff_y = static_cast<double>(overhangs[i]->position_on_layer.y() -
                                                       overhangs[j]->position_on_layer.y());
            closest_sq = std::min(closest_sq, diff_x * diff_x + diff_y * diff_y);
        }
    }

    return Slic3r::unscale<double>(std::sqrt(closest_sq));
}

} // namespace

TEST_CASE("Auto support points keep the configured minimal distance", "[SupportPlacement]")
{
    // A wide cone: its overhang outline is over 150 mm long, so points of several millimetres
    // apart are left even with a large minimal distance.
    const Slic3r::Domain::TriangleMesh mesh = cone_mesh(25., 10.);

    SECTION("the default does not limit the distance")
    {
        const LayerSupportPoints points = generate_points(mesh);
        const size_t overhangs = points_of_type(points, SupportPointType::slope).size();
        REQUIRE(overhangs > 5);
    }

    SECTION("a bigger distance leaves no two overhang points closer")
    {
        size_t previous_count = std::numeric_limits<size_t>::max();
        for (const double min_distance : {2., 4., 8.}) {
            SupportPointGeneratorConfig cfg;
            cfg.minimal_point_distance = min_distance;

            const LayerSupportPoints points = generate_points(mesh, cfg);
            const size_t overhangs = points_of_type(points, SupportPointType::slope).size();

            INFO("Minimal distance: " << min_distance << " mm, " << overhangs << " overhang points");

            // The generator has to place the points, so the cone still gets its supports.
            REQUIRE(overhangs > 0);
            // And none of them may be nearer to another one than the configured distance.
            CHECK(closest_overhang_distance(points) >= min_distance);
            // A bigger distance cannot add points.
            CHECK(overhangs <= previous_count);
            previous_count = overhangs;
        }
    }
}

TEST_CASE("The overhang angle threshold drops the steep overhangs only", "[SupportPlacement]")
{
    // A steep cone: its whole surface is about 63 degrees from horizontal.
    const Slic3r::Domain::TriangleMesh mesh = cone_mesh(10., 20.);

    PrepareSupportConfig keep_all;
    keep_all.overhang_angle_threshold = 90.;

    PrepareSupportConfig keep_shallow;
    keep_shallow.overhang_angle_threshold = 30.;

    // The surface of the cone is steeper than 30 degrees from horizontal, so nothing but the
    // island of the first layer is left.
    const LayerSupportPoints with_threshold = generate_points(mesh, {}, keep_shallow);
    const LayerSupportPoints all_overhangs = generate_points(mesh, {}, keep_all);

    CHECK(points_of_type(with_threshold, SupportPointType::slope).empty());
    CHECK(points_of_type(all_overhangs, SupportPointType::slope).size() > 0);
    CHECK(with_threshold.size() < all_overhangs.size());

    // The island is not an overhang, so it keeps its points.
    const std::vector<const LayerSupportPoint *> island_with_threshold =
        points_of_type(with_threshold, SupportPointType::island);
    const std::vector<const LayerSupportPoint *> island_of_all =
        points_of_type(all_overhangs, SupportPointType::island);

    REQUIRE_FALSE(island_with_threshold.empty());
    CHECK(island_with_threshold.size() == island_of_all.size());
}

TEST_CASE("A lower overhang angle threshold never yields more points", "[SupportPlacement]")
{
    // A cone of about 45 degrees, so 75 keeps its overhangs and 30 drops them.
    const Slic3r::Domain::TriangleMesh mesh = cone_mesh(10., 10.);

    size_t previous_count = std::numeric_limits<size_t>::max();
    for (const double angle : {90., 75., 60., 30.}) {
        PrepareSupportConfig prepare_cfg;
        prepare_cfg.overhang_angle_threshold = angle;

        const LayerSupportPoints points = generate_points(mesh, {}, prepare_cfg);

        INFO("Overhang angle: " << angle << " degrees, " << points.size() << " points");
        CHECK(points.size() <= previous_count);
        previous_count = points.size();
    }
}
