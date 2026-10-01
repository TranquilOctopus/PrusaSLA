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
    /// the paint tool writes it, one hexadecimal digit per nibble: the low two bits of a nibble are
    /// the number of sides the triangle is split along and the high two bits are the state of a leaf
    /// (see TriangleSelector::serialize and FacetsAnnotation::get_triangle_as_string), so an
    /// enforcer is 0b0100 ('4') and a blocker is 0b1000 ('8'). Writing the state itself ('1' and '2')
    /// asks for a triangle split along one or two sides, and the read of that walks off the bit stream
    /// into memory that is not ours.
    void paint_faces_above(double min_normal_z, TriangleStateType state)
    {
        const indexed_triangle_set &its = volume->mesh().its;
        const char *state_code = state == TriangleStateType::ENFORCER ? "4" : "8";
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

// The layer height the tests slice on: the one `make_sla_config()` configures the support tool with.
constexpr double layer_height_mm = 0.2;

// The heights the generator works on for a mesh: the middle of every layer of the mesh. That is the
// grid the support tool builds it from (`compute_slice_heights()` in SLASupportTool.cpp), the one the
// slice pipeline builds it from (`SLAPrint::Steps::slice_model()`) and the one the regression tests
// hand the generator, and a grid that starts on the bottom face of the model instead is not one the
// generator ever sees. It is not harmless either: the slicer does not slice a horizontal facet that
// faces down, so the first layer of a model that stands on the plate comes out empty, the island of
// that model lands one layer higher, and the painted floor of the model belongs to the layer below
// the island instead of the layer of it.
std::vector<float> layer_heights(const Slic3r::Domain::TriangleMesh &mesh)
{
    using Slic3r::Biz::Algorithms::BoundingBox::cast;
    const Slic3r::Domain::BoundingBox3f bb = cast<float>(mesh.bounding_box());

    std::vector<float> heights;
    for (double z = double(bb.min.z()) + 0.5 * layer_height_mm; z < double(bb.max.z());
         z += layer_height_mm)
        heights.push_back(float(z));

    return heights;
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

// A cone that widens upwards, so that the model has an overhang for the blocker to take away at
// all: every layer is a larger disc than the one below it, so the generator samples the overhang all
// around it, and the first layer is the island of the small footprint the tip stands on.
//
// `make_cone` builds the other cone: its wide base is on the plate and it narrows upwards, so every
// layer is a *smaller* disc than the one below it, there is no overhang anywhere and the generator
// returns no slope point to block. The cone is mirrored in Z - which keeps the faces wound outwards,
// so the normals the paint below reads are the outward ones - and lifted so that the tip it now
// stands on clears the plate and its first layer is a real island.
Slic3r::Domain::TriangleMesh widening_cone(double radius, double height)
{
    Slic3r::Domain::TriangleMesh mesh = triangle_mesh::make_cone(radius, height);
    mesh.mirror(Slic3r::Domain::Axis::Z);
    // The mirrored cone hangs from z = 0 down to z = -height, tip at the bottom. Lifting it by half
    // its height again stands the tip that far above the plate, so its first layer is a disc of half
    // the radius to be the island of.
    mesh.translate(Slic3r::Domain::Vec3f{0.f, 0.f, float(1.5 * height)});

    return mesh;
}

// A blocker over the whole model in every layer, written by hand so that the test does not depend on
// what the slicer makes of a painted facet. The island of the model lies inside the blocked region
// here, which is the case the test is about.
Slic3r::sla::SupportFacetPaint blocking_everywhere(size_t layer_count)
{
    const Slic3r::ExPolygon region{Slic3r::Polygon{
        Slic3r::Point{Slic3r::Domain::coord_t(scale_(-100.)), Slic3r::Domain::coord_t(scale_(-100.))},
        Slic3r::Point{Slic3r::Domain::coord_t(scale_(100.)), Slic3r::Domain::coord_t(scale_(-100.))},
        Slic3r::Point{Slic3r::Domain::coord_t(scale_(100.)), Slic3r::Domain::coord_t(scale_(100.))},
        Slic3r::Point{Slic3r::Domain::coord_t(scale_(-100.)), Slic3r::Domain::coord_t(scale_(100.))}}};

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
    // A wide cone: every layer is a disc a little larger than the one below it, so the generator
    // samples the overhang all around it. The first layer is the island of the tip it stands on.
    PaintedModel painted{widening_cone(25., 20.)};
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

        // Every facet of the cone, the wide disc on top included: the section is about the whole
        // overhang, so nothing is left out. The sides of a cone that widens upwards face outwards
        // and downwards - their normal z is about -0.78 for this cone - so a threshold above that
        // paints the top disc alone and leaves the overhang standing.
        painted.paint_faces_above(-1.1, TriangleStateType::BLOCKER);

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

    SECTION("an enforced region gets points where a point of a layer below already stands")
    {
        // A box is a prism, so the island points of its bottom layer are carried up through every
        // layer above it and the topmost layer of the grid holds points before the painting is even
        // read - at the very spots the enforced top face is sampled at, because both are sampled
        // from the same square. A point of a layer below holds the surface from underneath instead
        // of standing on it, and an enforced region is painted over exactly that, so the points
        // below may not take the samples of the region away.
        const Slic3r::Domain::TriangleMesh mesh = painted.volume->mesh();
        painted.paint_faces_above(0.5, TriangleStateType::ENFORCER);

        const Slic3r::sla::SupportFacetPaint paint = Slic3r::sla::support_facet_paint(
            Slic3r::sla::support_tool_model_mesh(*painted.object),
            Slic3r::Domain::Transform3d::Identity(), layer_heights(mesh), [] { return false; });
        REQUIRE(paint.has_enforcer_regions);

        // The generator leaves its points at the height of the layer it made them on, so the points
        // on the top face of the box are the ones above its middle and the island points of the
        // bottom face are not counted with them.
        const LayerSupportPoints plain      = generate_points(mesh, {});
        const LayerSupportPoints enforced   = generate_points(mesh, paint);
        const size_t             plain_top  = points_above(plain, 5.f).size();
        const size_t             forced_top = points_above(enforced, 5.f).size();
        INFO("Points on the top of the box: " << plain_top << " plain, " << forced_top << " enforced");
        CHECK(plain_top == 0);
        CHECK(forced_top > 0);
    }

    SECTION("a painted facet lands in the layer whose slab it is in")
    {
        // The layers the generator works on are the middles of the layers of the mesh, so the slab of
        // a layer is half a layer height around its own middle, and the top of the box lies in the
        // slab of the topmost layer of the grid. A region that is read into a layer the model does
        // not have is a region nobody reads, and these sections above see the region of the enforced
        // top or none of it, so the layer a painted facet is read in is what they are really asking
        // for.
        const Slic3r::Domain::TriangleMesh mesh    = painted.volume->mesh();
        const std::vector<float>          heights = layer_heights(mesh);

        painted.paint_faces_above(0.5, TriangleStateType::ENFORCER);

        const Slic3r::sla::SupportFacetPaint paint = Slic3r::sla::support_facet_paint(
            Slic3r::sla::support_tool_model_mesh(*painted.object),
            Slic3r::Domain::Transform3d::Identity(), heights, [] { return false; });
        REQUIRE(paint.has_enforcer_regions);
        REQUIRE(paint.layers.size() == heights.size());

        // The whole top face, in the topmost layer, and nowhere else: the sides of the box face
        // sideways, and a facet that faces sideways is projected into no slab at all.
        CHECK_FALSE(paint.enforcers(heights.size() - 1).empty());
        size_t layers_with_regions = 0;
        for (size_t layer_id = 0; layer_id < heights.size(); ++layer_id)
            if (!paint.enforcers(layer_id).empty())
                ++layers_with_regions;
        CHECK(layers_with_regions == 1);
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
                Slic3r::Domain::coord_t(scale_(island.pos.x())), Slic3r::Domain::coord_t(scale_(island.pos.y()))};
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
