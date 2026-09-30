// M2.30a: the automatic support points honour the facets the user painted on the model. A facet
// painted as a blocker takes the automatic points of its surface away, a region painted as an
// enforcer gets points at the configured density, also where the overhang rule would skip the
// surface. Island points are never taken away and a model with nothing painted gets exactly the
// points of the generator without painting.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

#include "libslic3r/SLASupportTool.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/SLA/SupportFacetPaint.hpp"
#include "libslic3r/SLA/SupportIslands/SampleConfig.hpp"
#include "libslic3r/SLA/SupportPointGenerator.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"
#include "libslic3r/libslic3r.h"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/ConfigBoxesSLA.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "Slic3r/Domain/PrinterTechnology.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"
#include "Slic3r/Domain/TriangleSelector.hpp"
#include "sla_test_utils.hpp"

namespace triangle_mesh = Slic3r::Biz::Algorithms::TriangleMesh;
using Slic3r::Domain::SLA::SupportPoint;
using Slic3r::Domain::SLA::SupportPointType;
using Slic3r::Domain::TriangleSelector::TriangleStateType;
using Slic3r::sla::LayerSupportPoints;

namespace {

// The tool API takes the raw config pointers and resolves the SLA object view itself, the same way
// the support tool does. No SLA option is declared with location == SLAConfigLocation::Object, so
// every key a test changes has to be set on the print box before the full config is built.
struct SlaConfig
{
    Slic3r::Domain::FullConfigSLAPtr          full;
    Slic3r::Domain::PartialObjectConfigSLAPtr object_settings;
};

SlaConfig make_sla_config(int density_percent = 100)
{
    Slic3r::Domain::ConfigPackSLA pack;
    pack.sla_print_settings.items.opt("supports_enable").set(true);
    pack.sla_print_settings.items.opt("pad_enable").set(true);
    pack.sla_print_settings.items.opt("support_object_elevation").set(10.0);
    // A coarse layer keeps the tests quick, none of the rules under test depends on it.
    pack.sla_print_settings.items.opt("layer_height").set(0.2);
    pack.sla_print_settings.items.opt("support_points_density_relative").set(density_percent);

    SlaConfig config;
    config.full = std::make_shared<const Slic3r::Domain::FullConfigSLA>(
        pack, Slic3r::Domain::Preset::HwPrinterConfig{.technology = Slic3r::Domain::PrinterTechnology::SLA});
    config.object_settings = std::make_shared<const Slic3r::Domain::PartialObjectConfigSLA>(
        Slic3r::Domain::SLAObjectSettings{}, config.full->hw_config());
    return config;
}

// A model with one object and one volume, and the painting of that volume, which is where the paint
// tool of the FFF supports leaves it (ModelVolume::supported_facets).
struct PaintedModel
{
    Slic3r::Domain::Model       model;
    Slic3r::Domain::ModelObject *object{nullptr};
    Slic3r::Domain::ModelVolume *volume{nullptr};

    explicit PaintedModel(const Slic3r::Domain::TriangleMesh &mesh)
    {
        object = model.add_object();
        volume = Slic3r::Biz::Algorithms::ModelObject::add_volume(object, mesh);
        object->add_instance();
    }

    /// Paint every facet whose normal points up into the given half space, so that a whole face of
    /// the model is painted and not a piece of it. The bit stream of the painting is written the way
    /// the paint tool writes it: one state code per triangle, 1 for an enforcer and 2 for a blocker
    /// (see TriangleSelector::decode_leaf_state).
    void paint_faces_above(double min_normal_z, TriangleStateType state)
    {
        const indexed_triangle_set &its = volume->mesh().its;
        const char *state_code = state == TriangleStateType::ENFORCER ? "1" : "2";
        for (size_t facet = 0; facet < its.indices.size(); ++facet) {
            if (triangle_mesh::its_face_normal(its, int(facet)).z() < min_normal_z)
                continue;
            volume->supported_facets.set_triangle_from_string(int(facet), state_code);
        }
    }
};

Slic3r::Domain::SLA::SupportPoints generate(const PaintedModel &model, const SlaConfig &config)
{
    return Slic3r::sla::generate_support_points_for_tool(
        *model.object, Slic3r::Domain::Transform3d::Identity(), config.full, config.object_settings,
        [] { return false; });
}

template<typename PointType>
std::vector<PointType> points_of_type(const std::vector<PointType> &points, SupportPointType type)
{
    std::vector<PointType> result;
    for (const PointType &point : points)
        if (point.type == type)
            result.push_back(point);
    return result;
}

template<typename PointType>
std::vector<PointType> points_above(const std::vector<PointType> &points, float z)
{
    std::vector<PointType> result;
    for (const PointType &point : points)
        if (point.pos.z() > z)
            result.push_back(point);
    return result;
}

// The grid the generator works on for a mesh, the same one the helper of the placement tests builds.
std::vector<float> layer_heights(const Slic3r::Domain::TriangleMesh &mesh)
{
    using Slic3r::Biz::Algorithms::BoundingBox::cast;
    const Slic3r::Domain::BoundingBox3f bb = cast<float>(mesh.bounding_box());
    return std::vector<float>{Slic3r::grid(bb.min.z(), bb.max.z(), 0.2f)};
}

// The generator over a mesh, the way the placement tests drive it: the points stay where the
// generator put them, move_on_mesh_surface would shift every one of them by up to a layer height.
LayerSupportPoints generate_points(const Slic3r::Domain::TriangleMesh &mesh,
                                   const Slic3r::sla::SupportFacetPaint &paint)
{
    const std::vector<float>        heights = layer_heights(mesh);
    std::vector<Slic3r::ExPolygons> slices{Slic3r::slice_mesh_ex(mesh.its, heights, CLOSING_RADIUS)};
    const Slic3r::sla::SupportPointGeneratorData data =
        Slic3r::sla::prepare_generator_data(std::move(slices), heights, {}, []() {}, [](int) {}, paint);
    return Slic3r::sla::generate_support_points(data, {});
}

// A blocker over the whole model in every layer, written by hand so that the test does not depend on
// what the slicer makes of a painted facet. The island of the model lies inside the blocked region
// here, which is the case the test is about.
Slic3r::sla::SupportFacetPaint blocking_everywhere(size_t layer_count)
{
    const Slic3r::ExPolygon region{Slic3r::Polygon{
        Slic3r::Point{Slic3r::coord_t(scale_(-100.)), Slic3r::coord_t(scale_(-100.))},
        Slic3r::Point{Slic3r::coord_t(scale_(100.)), Slic3r::coord_t(scale_(-100.))},
        Slic3r::Point{Slic3r::coord_t(scale_(100.)), Slic3r::coord_t(scale_(100.))},
        Slic3r::Point{Slic3r::coord_t(scale_(-100.)), Slic3r::coord_t(scale_(100.))}}};

    Slic3r::sla::SupportFacetPaint paint;
    paint.layers.resize(layer_count);
    for (Slic3r::sla::SupportFacetPaint::Layer &layer : paint.layers)
        layer.blockers = {region};
    paint.has_blocker_regions = true;
    return paint;
}

} // namespace

TEST_CASE("A painted blocker takes the automatic points of its overhang away", "[SupportFacetPaint]")
{
    // A wide cone: every layer is a disc a little smaller than the one below it, so the generator
    // samples the overhang all around it. The first layer is an island, and the bottom of the cone is
    // a downward facing facet which is left unpainted.
    PaintedModel painted{triangle_mesh::make_cone(25., 20.)};
    const SlaConfig config = make_sla_config();

    const Slic3r::Domain::SLA::SupportPoints unpainted = generate(painted, config);
    const size_t overhangs = points_of_type(unpainted, SupportPointType::slope).size();
    const size_t islands   = points_of_type(unpainted, SupportPointType::island).size();

    SECTION("the overhang of the cone has automatic points before the painting")
    {
        REQUIRE(overhangs > 0);
        REQUIRE(islands > 0);
    }

    SECTION("painting the whole overhang of the cone as a blocker takes them away")
    {
        REQUIRE(overhangs > 0);

        // Every facet of the cone but the bottom one, which is the island layer.
        painted.paint_faces_above(-0.5, TriangleStateType::BLOCKER);

        const Slic3r::Domain::SLA::SupportPoints blocked = generate(painted, config);
        INFO("Automatic points left: " << blocked.size());
        CHECK(points_of_type(blocked, SupportPointType::slope).empty());
        // The island is not an overhang, so the blocker leaves its points alone.
        CHECK(points_of_type(blocked, SupportPointType::island).size() == islands);
    }

    SECTION("a blocked region over the whole layer takes no island point away either")
    {
        REQUIRE(islands > 0);
        REQUIRE(overhangs > 0);

        const Slic3r::Domain::TriangleMesh mesh = painted.volume->mesh();
        const size_t layer_count = layer_heights(mesh).size();
        const LayerSupportPoints plain = generate_points(mesh, {});
        const LayerSupportPoints blocked = generate_points(mesh, blocking_everywhere(layer_count));

        CHECK(points_of_type(blocked, SupportPointType::slope).empty());
        // The island of the first layer is inside the blocked region, and it keeps its points.
        CHECK(points_of_type(blocked, SupportPointType::island).size() ==
              points_of_type(plain, SupportPointType::island).size());
    }
}

TEST_CASE("An enforced region gets support points where the overhang rule has none", "[SupportFacetPaint]")
{
    // A box has no overhang at all: every layer is the same square, so the only automatic points are
    // the ones of the island of its bottom layer. Its top face is flat, and the overhang rule has
    // nothing to say about it, which is the point of the test.
    PaintedModel painted{triangle_mesh::make_cube(20., 20., 10.)};

    SECTION("painting the top of the box as an enforcer puts points on it")
    {
        const SlaConfig config = make_sla_config();

        const Slic3r::Domain::SLA::SupportPoints unpainted = generate(painted, config);
        REQUIRE(unpainted.size() > 0);
        REQUIRE(points_above(unpainted, 5.f).empty());

        // The whole top face, both of its triangles.
        painted.paint_faces_above(0.5, TriangleStateType::ENFORCER);

        const Slic3r::Domain::SLA::SupportPoints enforced = generate(painted, config);
        const size_t on_top = points_above(enforced, 5.f).size();
        INFO("Support points on the enforced top: " << on_top);
        CHECK(on_top > 0);
        // The island of the bottom layer is still there.
        CHECK(points_of_type(enforced, SupportPointType::island).size() >=
              points_of_type(unpainted, SupportPointType::island).size());
    }

    SECTION("the enforced region follows the configured density")
    {
        // The points of an enforced region are sampled with the density of the islands, so a denser
        // print needs more of them on the same region.
        painted.paint_faces_above(0.5, TriangleStateType::ENFORCER);

        const Slic3r::Domain::SLA::SupportPoints sparse = generate(painted, make_sla_config(50));
        const Slic3r::Domain::SLA::SupportPoints dense  = generate(painted, make_sla_config(200));

        const size_t sparse_on_top = points_above(sparse, 5.f).size();
        const size_t dense_on_top  = points_above(dense, 5.f).size();
        INFO("Points on the enforced top: " << sparse_on_top << " at 50 %, " << dense_on_top << " at 200 %");
        REQUIRE(sparse_on_top > 0);
        CHECK(dense_on_top > sparse_on_top);
    }
}

TEST_CASE("A model with nothing painted gets the points of the generator without painting", "[SupportFacetPaint]")
{
    PaintedModel painted{triangle_mesh::make_cube(20., 20., 10.)};
    const SlaConfig config = make_sla_config();

    SECTION("the paint of a model with no painted facet is empty")
    {
        const std::vector<float> heights = layer_heights(painted.volume->mesh());
        const Slic3r::sla::SupportFacetPaint paint = Slic3r::sla::support_facet_paint(
            Slic3r::sla::support_tool_model_mesh(*painted.object),
            Slic3r::Domain::Transform3d::Identity(), heights, [] { return false; });

        CHECK(paint.empty());
        CHECK(paint.layers.empty());
        CHECK_FALSE(paint.has_enforcer_regions);
        CHECK_FALSE(paint.has_blocker_regions);
    }

    SECTION("painting a surface the generator never puts a point on changes nothing")
    {
        const Slic3r::Domain::SLA::SupportPoints unpainted = generate(painted, config);

        // The top of the box is not an overhang and not an island, so the blocker below never has a
        // point to take away.
        painted.paint_faces_above(0.5, TriangleStateType::BLOCKER);

        const Slic3r::Domain::SLA::SupportPoints blocked = generate(painted, config);
        REQUIRE(blocked.size() == unpainted.size());
        for (size_t i = 0; i < blocked.size(); ++i) {
            INFO("Support point " << i);
            const bool same = blocked[i] == unpainted[i];
            CHECK(same);
        }
    }
}

TEST_CASE("An island keeps its support point inside a blocked region", "[SupportFacetPaint]")
{
    // The rule of the block brush: blocking takes the automatic points of an overhang away, never the
    // points of an island. An island is a region of a layer with nothing solid below it, the region
    // the island detection of M4.8d reports and names because it can fall off the build, and a
    // support point is what holds it up. It is the one rule the user cannot paint away, so the hint
    // of the block brush says so.
    //
    // A box has no overhang at all, so its only automatic points are the island points of its bottom
    // layer. Painting every facet of it as a blocker therefore paints the island itself.
    PaintedModel painted{triangle_mesh::make_cube(20., 20., 10.)};
    const SlaConfig config = make_sla_config();

    SECTION("painting the whole box as a blocker leaves its island points alone")
    {
        const Slic3r::Domain::SLA::SupportPoints unpainted = generate(painted, config);
        const size_t islands = points_of_type(unpainted, SupportPointType::island).size();
        REQUIRE(islands > 0);

        // Every facet, the bottom one included: the island lies inside the painted surface now.
        painted.paint_faces_above(-1.1, TriangleStateType::BLOCKER);

        const Slic3r::Domain::SLA::SupportPoints blocked = generate(painted, config);
        INFO("Automatic points of the unpainted box: " << unpainted.size());
        INFO("Automatic points of the blocked box: " << blocked.size());
        CHECK(points_of_type(blocked, SupportPointType::island).size() == islands);
    }

    SECTION("the island lies inside the blocked region and keeps its points")
    {
        painted.paint_faces_above(-1.1, TriangleStateType::BLOCKER);

        const Slic3r::Domain::TriangleMesh mesh    = painted.volume->mesh();
        const std::vector<float>          heights = layer_heights(mesh);
        const Slic3r::sla::SupportFacetPaint paint = Slic3r::sla::support_facet_paint(
            Slic3r::sla::support_tool_model_mesh(*painted.object),
            Slic3r::Domain::Transform3d::Identity(), heights, [] { return false; });
        REQUIRE(paint.has_blocker_regions);

        const LayerSupportPoints plain   = generate_points(mesh, {});
        const LayerSupportPoints blocked = generate_points(mesh, paint);
        const auto islands = points_of_type(plain, SupportPointType::island);
        REQUIRE(islands.size() > 0);

        // Every island point of the plain run that lies inside a blocked region of its own layer is a
        // point the block brush could not take away. That the blocked region really covers the island
        // is the point of the test: the points kept below are points inside a blocked region.
        size_t inside_blocked_region = 0;
        for (const auto &island : islands) {
            // A support point sits at the middle of the layer it was made on, which is the height of
            // that layer, so a point names its own layer.
            const auto height = std::find(heights.begin(), heights.end(), island.pos.z());
            INFO("Island point at z " << island.pos.z());
            REQUIRE(height != heights.end());
            const size_t layer_id = static_cast<size_t>(std::distance(heights.begin(), height));

            const Slic3r::Point where{
                Slic3r::coord_t(scale_(island.pos.x())), Slic3r::coord_t(scale_(island.pos.y()))};
            if (paint.is_blocked(layer_id, where))
                ++inside_blocked_region;
        }
        INFO("Island points inside a blocked region: " << inside_blocked_region);
        REQUIRE(inside_blocked_region > 0);

        // And the run with the blockers has all of them: the island sampling is the same in both runs,
        // because an island point is made of the shape of a part and not of a sample of an overhang.
        CHECK(points_of_type(blocked, SupportPointType::island).size() == islands.size());
    }
}
