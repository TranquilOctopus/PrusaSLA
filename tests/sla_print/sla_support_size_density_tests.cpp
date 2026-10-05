// M7.8.7: the density of the automatic support scales with the size of the part.
//
// The radius one support point may hold was a constant of the program: the curve of
// create_default_support_curve() grows up to 6 mm, which is right for a plate and which swallows a
// 9 mm miniature head whole in one or two points. The studio that supports such models by hand in
// another slicer puts 17 to 24 contact tips on a head of that size and the generator placed 4 and 9
// on the two calibration heads of doc/sla-fork/supports/calibration.md. The radius a point may hold
// is now the smaller of the maximum of that curve and support_curve_size_factor times the size of
// the part, and a part big enough for the factor not to reach its curve keeps the points it had.
// SupportPointGenerator.hpp holds the constant and what it was calibrated on.
//
// Measured here, on shapes written from scratch so that every number is known:
// - the synthetic head, about the size of the calibration heads, gets a point count inside the 15
// to 25 the studio sits in, and the same shape gives the same points twice
// - a 40 mm cube, a 100 mm plate and a cone 40 mm across keep the points they had, measured over
// their own layers twice: once with the curve of before and once with the curve the size
// scaling asks for
// - the curve itself: the size of a part scales every radius by one factor, leaves the heights
// alone, and leaves the curve of a part that is big enough alone
//
// The head runs through the support tool, the big parts through the generator, because the
// comparison of before and after needs the curve of before and the tool has already replaced it.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <numbers>
#include <utility>
#include <vector>

#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/Axis.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "Slic3r/Domain/PrinterTechnology.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "libslic3r/SLA/SupportPointGenerator.hpp"
#include "libslic3r/SLASupportTool.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"

using Catch::Approx;
namespace triangle_mesh = Slic3r::Biz::Algorithms::TriangleMesh;

using Slic3r::indexed_triangle_set;
using Slic3r::Domain::SLA::SupportPoint;
using Slic3r::Domain::SLA::SupportPoints;
using Slic3r::sla::SupportPointGeneratorConfig;
using Slic3r::sla::SupportPointGeneratorData;

namespace {

// The default of the layer_height key is 0.3 mm, an FFF leftover. The calibration test and the
// benchmark harness both slice at 0.05, and so does this one.
constexpr double LAYER_HEIGHT_MM = 0.05;
// The closing radius the support tool slices with, the value sla_test_utils.hpp uses as well.
constexpr float CLOSING_RADIUS_MM = 0.005f;

// The studio places 17 to 24 contact tips on a head of this size (calibration.md, five studio
// supported miniature heads of 8.4 to 9.7 mm). The window is that with room on both sides.
constexpr size_t HEAD_POINTS_MIN = 15;
constexpr size_t HEAD_POINTS_MAX = 25;

/// The tool API takes the raw config pointers and resolves the SLA object view itself.
struct SlaConfig
{
    Slic3r::Domain::FullConfigSLAPtr full;
    Slic3r::Domain::PartialObjectConfigSLAPtr object_settings;
};

// No SLA option is declared with location == SLAConfigLocation::Object, so every key this file
// tweaks is set on the print box before the full config is built (sla_support_roles_tests.cpp builds
// the same config). The elevation is what lifts the object off the plate, so the point of the
// lowest layer is a support point and not a point dropped as one on the plate.
SlaConfig make_sla_config()
{
    Slic3r::Domain::ConfigPackSLA pack;
    pack.sla_print_settings.items.opt("layer_height").set(LAYER_HEIGHT_MM);
    pack.sla_print_settings.items.opt("supports_enable").set(true);
    pack.sla_print_settings.items.opt("pad_enable").set(true);
    pack.sla_print_settings.items.opt("support_object_elevation").set(10.0);

    SlaConfig cfg;
    cfg.full = std::make_shared<const Slic3r::Domain::FullConfigSLA>(
        pack,
        Slic3r::Domain::Preset::
            HwPrinterConfig{.technology = Slic3r::Domain::PrinterTechnology::SLA}
    );
    cfg.object_settings = std::make_shared<const Slic3r::Domain::PartialObjectConfigSLA>(
        Slic3r::Domain::SLAObjectSettings{},
        cfg.full->hw_config()
    );
    return cfg;
}

/// The support points of a shape, generated the way the support tool generates them: the slice, the
/// generator, the points on the mesh surface and then the roles (M7.8.2).
SupportPoints tool_points(const indexed_triangle_set& its)
{
    Slic3r::Domain::Model model;
    Slic3r::Domain::ModelObject* object = model.add_object();
    Slic3r::Biz::Algorithms::ModelObject::add_volume(object, triangle_mesh::construct(its));
    object->add_instance();

    const SlaConfig cfg = make_sla_config();
    return Slic3r::sla::generate_support_points_for_tool(
        *object,
        Slic3r::Domain::Transform3d::Identity(),
        cfg.full,
        cfg.object_settings,
        [] { return false; }
    );
}

/// The layers of a shape, prepared the way the generator wants them: one ExPolygons per layer
/// height, in the order of the heights.
///
/// The prepared data holds pointers into its own slices, so it is used where it is built and never
/// copied: both the return here and the const object of the caller are the same object, as the copy
/// elision of C++17 and later guarantees.
SupportPointGeneratorData prepare(const indexed_triangle_set& its)
{
    const Slic3r::Domain::TriangleMesh mesh = triangle_mesh::construct(its);

    const double min_z = mesh.bounding_box().min.z();
    const double max_z = mesh.bounding_box().max.z();

    std::vector<float> heights;
    for (double z = min_z + 0.5 * LAYER_HEIGHT_MM; z < max_z; z += LAYER_HEIGHT_MM)
        heights.push_back(float(z));

    std::vector<Slic3r::Domain::ExPolygons> slices{
        Slic3r::slice_mesh_ex(mesh.its, heights, CLOSING_RADIUS_MM)
    };

    return Slic3r::sla::prepare_generator_data(std::move(slices), heights);
}

/// What the generator makes of prepared layers with a given support curve.
size_t
point_count(const SupportPointGeneratorData& data, const std::vector<Slic3r::Domain::Vec2f>& curve)
{
    SupportPointGeneratorConfig cfg;
    cfg.support_curve = curve;

    return Slic3r::sla::generate_support_points(data, cfg).size();
}

/// The head of a miniature: a sphere of 8 mm on a short neck cylinder, 8.6 mm tall in all. The
/// bottom of the sphere sinks 0.6 mm into the 1 mm neck, so the two meshes join in one body instead
/// of touching in a single point, and the widest layer of the shape is the equator of the sphere.
///
/// Its size, which is what the generator scales its density with, is the smaller of the 8 mm it is
/// wide in plan and the 7.09 mm that is the side of the square of its equator.
indexed_triangle_set head_shape()
{
    constexpr double radius      = 4.;
    constexpr double neck_radius = 2.;
    constexpr double neck_height = 1.;
    constexpr double sunk_into   = 0.6;
    // Six degrees a step: a sphere of about 5000 faces, fine enough for the contour of a layer to be
    // the circle of the model and coarse enough to keep the test quick.
    constexpr double angle = 2. * std::numbers::pi / 60.;

    indexed_triangle_set sphere = triangle_mesh::its_make_sphere(radius, angle);
    for (Slic3r::Domain::Vec3f& v : sphere.vertices)
        v.z() += float(sunk_into + radius);

    indexed_triangle_set its;
    Slic3r::Domain::its_merge(its, triangle_mesh::its_make_cylinder(neck_radius, neck_height));
    Slic3r::Domain::its_merge(its, sphere);
    return its;
}

/// A box of the given size standing on the plate, centred on it. its_make_cube puts a box in the
/// positive corner, so it is moved to stand on the plate.
indexed_triangle_set box_shape(double x, double y, double z)
{
    indexed_triangle_set its = triangle_mesh::its_make_cube(x, y, z);
    for (Slic3r::Domain::Vec3f& v : its.vertices) {
        v.x() -= float(x / 2.);
        v.y() -= float(y / 2.);
    }
    return its;
}

/// A cone that widens upwards, i.e. one that stands on its apex, so that its overhang outline runs
/// along the whole circumference and the generator puts overhang points all around it. A box has no
/// overhang at all - every layer of it is the layer below - so a box on its own cannot tell whether
/// the support curve of a big part was left alone or not.
///
/// make_cone is the other cone, with its wide base on the plate and its apex at the top, so it is
/// mirrored, which keeps the faces wound outwards. The lift stands the apex clear of the plate.
indexed_triangle_set cone_shape(double radius, double height)
{
    Slic3r::Domain::TriangleMesh cone = triangle_mesh::make_cone(radius, height);
    cone.mirror(Slic3r::Domain::Axis::Z);
    cone.translate(Slic3r::Domain::Vec3f{0.f, 0.f, float(1.5 * height)});
    return cone.its;
}

/// True when two runs made the same points, whatever order they came in. The order is not what a run
/// promises - the points on the mesh surface and the roles of M7.8.2 are decided in parallel - so the
/// points are looked up in the other run instead. Where they stand and what kind they are is the
/// placement; everything else on a point follows from where it stands.
bool same_points(const SupportPoints& a, const SupportPoints& b)
{
    if (a.size() != b.size())
        return false;

    for (const SupportPoint& point : a) {
        const bool found = std::any_of(
            b.begin(),
            b.end(),
            [&point](const SupportPoint& other)
            { return other.pos == point.pos && other.type == point.type; }
        );
        if (!found)
            return false;
    }

    return true;
}

} // namespace

TEST_CASE("A miniature head is supported as densely as the studio", "[SupportSizeDensity]")
{
    const SupportPoints points = tool_points(head_shape());

    // The curve of the default config holds 6 mm of surface on one point, and that is most of a
    // head 8 mm wide, so before M7.8.7 this shape got a handful of points.
    INFO("head support points: " << points.size());
    CHECK(points.size() >= HEAD_POINTS_MIN);
    CHECK(points.size() <= HEAD_POINTS_MAX);

    // Nothing of the size is random, it is measured off the layers, so the same shape gives the
    // same run twice.
    const SupportPoints again = tool_points(head_shape());
    CHECK(same_points(points, again));
}

TEST_CASE("The size of a part scales every radius of the support curve", "[SupportSizeDensity]")
{
    const std::vector<Slic3r::Domain::Vec2f> curve = Slic3r::sla::create_default_support_curve();
    REQUIRE(curve.size() >= 2);

    SECTION("a small part gets a proportionally denser support")
    {
        // The size of the head above: the factor reaches well inside the curve.
        const double size = 7.1;
        const std::vector<Slic3r::Domain::Vec2f> scaled =
            Slic3r::sla::support_curve_for_size(curve, size);

        REQUIRE(scaled.size() == curve.size());
        CHECK(scaled.back().x() == Approx(Slic3r::sla::support_curve_size_factor * size));

        // One factor for the whole curve, and the height over the point a radius grows with is a
        // height of the part and not a radius: it stays.
        for (size_t i = 0; i < curve.size(); ++i) {
            INFO("curve point " << i);
            CHECK(scaled[i].x() < curve[i].x());
            CHECK(scaled[i].y() == curve[i].y());
        }
    }

    SECTION("a big part keeps the curve of before")
    {
        // The factor has to reach 6 mm, the largest radius of the default curve, before it changes
        // anything. A 40 mm cube and a 100 mm plate are both far past that.
        CHECK(Slic3r::sla::support_curve_for_size(curve, 40.) == curve);
        CHECK(Slic3r::sla::support_curve_for_size(curve, 100.) == curve);
    }

    SECTION("a part of no size keeps the curve of before")
    {
        CHECK(Slic3r::sla::support_curve_for_size(curve, 0.) == curve);
    }
}

TEST_CASE("A part big enough keeps the support points it had", "[SupportSizeDensity]")
{
    // A 40 mm cube and a 100 mm plate, and a cone 40 mm across for the overhangs a box cannot have.
    // The size of a box is its own width, and the size of the cone is the side of the square of its
    // base: all three are far above what the size factor can reach, which is what these three are
    // here for.
    const std::pair<const char*, indexed_triangle_set> shapes[]{
        {"a 40 mm cube", box_shape(40., 40., 40.)},
        {"a 100 mm plate", box_shape(100., 100., 2.)},
        {"a cone 40 mm across", cone_shape(20., 20.)},
    };

    for (const auto& shape : shapes) {
        const SupportPointGeneratorData data = prepare(shape.second);
        const std::vector<Slic3r::Domain::Vec2f> before_curve =
            Slic3r::sla::create_default_support_curve();
        const std::vector<Slic3r::Domain::Vec2f> after_curve =
            Slic3r::sla::support_curve_for_part(before_curve, data);

        INFO(shape.first << ", size " << Slic3r::sla::support_size_reference(data) << " mm");

        // What the size of such a part does not reach is the curve of the generator, so the curve
        // the generator is given is the curve of before. That is what keeps the points.
        CHECK(after_curve == before_curve);

        const size_t before = point_count(data, before_curve);
        const size_t after  = point_count(data, after_curve);

        REQUIRE(before > 0);
        INFO(shape.first << ": " << before << " points before, " << after << " after");
        CHECK(std::abs(double(after) - double(before)) <= 0.1 * double(before));
    }
}

TEST_CASE("The size of a part is measured off its layers", "[SupportSizeDensity]")
{
    // The head: 8 mm in plan and the side of the square of the equator of the sphere, which is the
    // smaller of the two numbers the size is made of.
    const SupportPointGeneratorData head = prepare(head_shape());
    CHECK(
        Slic3r::sla::support_size_reference(head)
        == Approx(std::sqrt(std::numbers::pi * 16.)).margin(0.1)
    );

    // A 40 mm cube is 40 mm in plan and the side of the square of its widest layer as well.
    const SupportPointGeneratorData cube = prepare(box_shape(40., 40., 40.));
    CHECK(Slic3r::sla::support_size_reference(cube) == Approx(40.).margin(0.1));

    // A 100 mm plate: 100 mm in plan, and its widest layer is its own square.
    const SupportPointGeneratorData plate = prepare(box_shape(100., 100., 2.));
    CHECK(Slic3r::sla::support_size_reference(plate) == Approx(100.).margin(0.1));

    // Nothing sliced, nothing to measure, and the caller keeps the curve it has.
    const SupportPointGeneratorData empty;
    CHECK(Slic3r::sla::support_size_reference(empty) == 0.);
}
