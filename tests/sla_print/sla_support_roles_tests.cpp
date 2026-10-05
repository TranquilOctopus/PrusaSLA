// M7.8.2: every support point the generator makes gets a role, and a point does not stay on a
// fragile feature or on small surface detail (the support rulebook R4.1 and R4.3 - R4.6 of
// doc/sla-fork/supports/rulebook.md). The shapes below are written from scratch so that every
// measurement the roles are decided by has one known value: how thick the feature under a point is,
// how wide the part it is on is, and how far the point has to move to get off a stud.
//
// What a role means to the tool, i.e. which tip class it takes, is not here: that is the mapping of
// M7.8.2 in the tool (SlaSupportRolesTests.cpp in slic3r-shared).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "Slic3r/Biz/Algorithms/AABBMesh.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/ConfigBoxesSLA.hpp"
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
#include "libslic3r/SLA/SupportRoles.hpp"
#include "libslic3r/SLASupportTool.hpp"

using Catch::Approx;
namespace triangle_mesh = Slic3r::Biz::Algorithms::TriangleMesh;
namespace scaling       = Slic3r::Biz::Algorithms::Scaling;

using Slic3r::indexed_triangle_set;
using Slic3r::Domain::Point;
using Slic3r::Domain::SLA::SupportPoint;
using Slic3r::Domain::SLA::SupportPoints;
using Slic3r::Domain::SLA::SupportPointType;
using Role = Slic3r::Domain::SLA::SupportPoint::Role;

namespace {

/// The tool API takes the raw config pointers and resolves the SLA object view itself.
struct SlaConfig
{
    Slic3r::Domain::FullConfigSLAPtr full;
    Slic3r::Domain::PartialObjectConfigSLAPtr object_settings;
};

// No SLA option is declared with location == SLAConfigLocation::Object, so every key the tests
// tweak has to be set on the print box before the full config is built (see
// sla_support_tool_tests.cpp, which builds the same config).
SlaConfig make_sla_config()
{
    Slic3r::Domain::ConfigPackSLA pack;
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

/// A model with one object holding one mesh, which is the shape the tests below run the generator
/// on.
struct ShapeModel
{
    Slic3r::Domain::Model model;
    Slic3r::Domain::ModelObject* object = nullptr;

    explicit ShapeModel(const indexed_triangle_set& its)
    {
        object = model.add_object();
        Slic3r::Biz::Algorithms::ModelObject::add_volume(object, triangle_mesh::construct(its));
        object->add_instance();
    }
};

/// The meshes of one shape, merged the way the tool merges the volumes of an object.
class ShapeBuilder
{
public:
    ShapeBuilder& cube(double x, double y, double z, double lift)
    {
        indexed_triangle_set cube = triangle_mesh::its_make_cube(x, y, z);
        // its_make_cube puts the cube in the positive corner, the shapes below are centred so that
        // their detail sits over the origin.
        for (Slic3r::Domain::Vec3f& v : cube.vertices) {
            v.x() -= float(x / 2.);
            v.y() -= float(y / 2.);
            v.z() += float(lift);
        }
        return add(cube);
    }

    ShapeBuilder& cylinder(double r, double h, double lift = 0.)
    {
        indexed_triangle_set cylinder = triangle_mesh::its_make_cylinder(r, h);
        for (Slic3r::Domain::Vec3f& v : cylinder.vertices)
            v.z() += float(lift);
        return add(cylinder);
    }

    /// A cone that widens upwards, i.e. standing on its apex: `its_make_cone` is the other one, with
    /// its wide base on the plate and its apex at the top, so it is mirrored. Mirroring keeps the
    /// faces wound outwards, which is what the normals and the ray casts of this file read (see
    /// sla_support_placement_tests.cpp, which builds the same cone).
    ShapeBuilder& upward_cone(double r, double h, double lift)
    {
        Slic3r::Domain::TriangleMesh cone = triangle_mesh::make_cone(r, h);
        cone.mirror(Slic3r::Domain::Axis::Z);
        cone.translate(Slic3r::Domain::Vec3f{0.f, 0.f, float(lift + h)});
        return add(cone.its);
    }

    indexed_triangle_set build() const
    {
        return m_its;
    }

private:
    ShapeBuilder& add(const indexed_triangle_set& part)
    {
        Slic3r::Domain::its_merge(m_its, part);
        return *this;
    }

    indexed_triangle_set m_its;
};

/// The points of a shape, generated the way the support tool generates them: the slice, the
/// generator, the points on the mesh surface and then the roles (M7.8.2).
SupportPoints generate(const indexed_triangle_set& its)
{
    const ShapeModel shape{its};
    const SlaConfig cfg = make_sla_config();
    return Slic3r::sla::generate_support_points_for_tool(
        *shape.object,
        Slic3r::Domain::Transform3d::Identity(),
        cfg.full,
        cfg.object_settings,
        [] { return false; }
    );
}

const SupportPoint* lowest_point(const SupportPoints& points)
{
    const SupportPoint* lowest = nullptr;
    for (const SupportPoint& point : points)
        if (lowest == nullptr || point.pos.z() < lowest->pos.z())
            lowest = &point;
    return lowest;
}

/// The points on one height of the model, which is how a test picks out the points of one island of
/// a shape that carries more than one.
std::vector<const SupportPoint*> points_at(const SupportPoints& points, double z, double tolerance)
{
    std::vector<const SupportPoint*> found;
    for (const SupportPoint& point : points)
        if (std::abs(double(point.pos.z()) - z) <= tolerance)
            found.push_back(&point);
    return found;
}

/// A block standing on a spike that is too thin to carry anything but the smallest tip: a blunt tip
/// of 0.6 mm, a cone that widens up to 1 mm and a 10 mm cube on top of that. Everything above the
/// spike hangs on the one point of the tip, which is the whole point of the shape.
indexed_triangle_set spike_shape()
{
    return ShapeBuilder{}
        .cylinder(0.3, 0.5) // the tip: 0.6 mm across, from z = 0 to z = 0.5
        .upward_cone(1.0, 4.0, 0.5) // the spike: apex on the tip, 1 mm across at z = 4.5
        .cube(10., 10., 10., 4.5) // the block, standing on the spike
        .build();
}

/// A plate with a stud of 0.6 mm under it: the plate is 2.4 mm thick and hangs 0.6 mm above the
/// plate, with nothing on the plate around it. The stud is small surface detail of R4.5.
indexed_triangle_set stud_shape()
{
    return ShapeBuilder{}
        .cylinder(0.3, 0.6) // the stud, from z = 0 to the plate at z = 0.6
        .cube(20., 20., 2.4, 0.6) // the plate, from z = 0.6 to z = 3.0
        .build();
}

/// Two blocks: one standing on the plate and one hanging in the air above it. The lowest island of
/// the object is the one of the block below, and the other island is a big one of its own.
indexed_triangle_set two_blocks_shape()
{
    return ShapeBuilder{}
        .cube(20., 20., 20., 0.) // the block on the plate, from z = 0 to z = 20
        .cube(12., 12., 12., 30.) // the block in the air, from z = 30 to z = 42
        .build();
}

} // namespace

TEST_CASE("The point on the tip of a spike is fragile", "[SupportRoles]")
{
    const SupportPoints points = generate(spike_shape());
    REQUIRE_FALSE(points.empty());

    // The spike tip is the only island of the shape, and the block above it rests on it, so the
    // point of that island is the lowest point of the model.
    const SupportPoint* tip = lowest_point(points);
    REQUIRE(tip != nullptr);
    CHECK(tip->is_island());
    CHECK(tip->pos.z() == Approx(0.).margin(0.06));

    // R4.4: a support under a tip that is 0.6 mm across takes the tip with it when it is removed,
    // so this point is fragile and takes the minimum tip. The tip of the spike is also the lowest
    // point of the object, and R4.1 does not make it an anchor: the fragile rule wins, a heavy
    // anchor would be the one support that snaps the spike.
    CHECK(tip->role == Role::Fragile);
    for (const SupportPoint& point : points) {
        INFO("point at " << point.pos.x() << ", " << point.pos.y() << ", " << point.pos.z());
        CHECK(point.role != Role::Anchor);
    }
}

TEST_CASE("No support point stays on a stud under a plate", "[SupportRoles]")
{
    const SupportPoints points = generate(stud_shape());
    REQUIRE_FALSE(points.empty());

    // R4.5: a point on small surface detail moves to the plain surface next to it, so the stud
    // carries no support at all. The stud is 0.6 mm across and a point may move 1 mm, so the point
    // of the stud ends up just outside the stud rather than on the far side of the plate. The margin
    // is a twentieth of the radius of the stud: the point of the stud is sampled in the middle of
    // it, and the points of the plate around it may sit right at its rim.
    for (const SupportPoint& point : points) {
        INFO("point at " << point.pos.x() << ", " << point.pos.y() << ", " << point.pos.z());
        const double from_axis = std::hypot(double(point.pos.x()), double(point.pos.y()));
        CHECK(from_axis > 0.28);
    }

    // What stands under the plate is the island of the plate now, which is a big one and not thin,
    // so the point next to the stud is not fragile either.
    bool a_support_next_to_the_stud = false;
    for (const SupportPoint& point : points) {
        const double from_axis = std::hypot(double(point.pos.x()), double(point.pos.y()));
        if (from_axis > 1.1)
            continue;
        INFO("point at " << point.pos.x() << ", " << point.pos.y() << ", " << point.pos.z());
        CHECK(point.role != Role::Fragile);
        a_support_next_to_the_stud = true;
    }
    CHECK(a_support_next_to_the_stud);
}

TEST_CASE("The lowest island of an object is anchored and a higher one is not", "[SupportRoles]")
{
    const SupportPoints points = generate(two_blocks_shape());
    REQUIRE_FALSE(points.empty());

    // R4.1: the lowest island of the object is the one the block below stands on and the one that
    // carries the whole model early in the print, so its points are the heavy anchors.
    const SupportPoint* lowest = lowest_point(points);
    REQUIRE(lowest != nullptr);
    CHECK(lowest->is_island());
    CHECK(lowest->pos.z() == Approx(0.).margin(0.06));
    CHECK(lowest->role == Role::Anchor);

    // The island of the block that hangs in the air is not the lowest one, and its area is far
    // above the threshold of a small island, so its points are island points of their own (R4.3)
    // and never anchors.
    const std::vector<const SupportPoint*> upper = points_at(points, 30., 0.2);
    REQUIRE_FALSE(upper.empty());
    bool an_island_point = false;
    for (const SupportPoint* point : upper) {
        INFO("point at " << point->pos.x() << ", " << point->pos.y() << ", " << point->pos.z());
        CHECK(point->role != Role::Anchor);
        if (point->role == Role::Island)
            an_island_point = true;
    }
    CHECK(an_island_point);
}

TEST_CASE("The roles of a model are the same every run", "[SupportRoles]")
{
    // The roles are decided by ray casts and by the layers of the slice, and a second run of the
    // same shape has to answer the same: the tool generates the points of an object again and again
    // while the settings are moved, and the points it shows may not jump around.
    const indexed_triangle_set shape = spike_shape();
    const SupportPoints first        = generate(shape);
    const SupportPoints second       = generate(shape);

    REQUIRE(first.size() == second.size());
    for (size_t i = 0; i < first.size(); ++i) {
        INFO("point " << i << " at z " << first[i].pos.z());
        const Slic3r::Domain::Vec3d here  = first[i].pos.cast<double>();
        const Slic3r::Domain::Vec3d there = second[i].pos.cast<double>();
        const bool same_place             = Slic3r::Domain::is_approx(there, here, 1e-4);
        CHECK(same_place);
        CHECK(second[i].role == first[i].role);
    }
}

namespace {

/// A layer of the model a point is on, as the generator leaves it in its data: where the shape of
/// the part is and how big it is.
Slic3r::sla::LayerPart layer_part(const Slic3r::Domain::ExPolygon* shape)
{
    return Slic3r::sla::LayerPart{
        shape,
        {},
        Slic3r::Biz::Algorithms::BoundingBox::construct(shape->contour.points)
    };
}

/// A rectangle of the size given in millimetres, as the shape of a part of a layer.
Slic3r::Domain::ExPolygon rectangle(double x0, double y0, double x1, double y1)
{
    return Slic3r::Domain::ExPolygon{
        {Point{int(scaling::scaled(x0)), int(scaling::scaled(y0))},
         Point{int(scaling::scaled(x1)), int(scaling::scaled(y0))},
         Point{int(scaling::scaled(x1)), int(scaling::scaled(y1))},
         Point{int(scaling::scaled(x0)), int(scaling::scaled(y1))}}
    };
}

} // namespace

TEST_CASE("The size of the island decides between an island and a small island", "[SupportRoles]")
{
    // The two sizes R4.3 asks about: a part of a layer big enough to be an island of its own, and a
    // part that is a speck next to it. Both stand on a slab two millimetres thick, so neither is
    // thin (R4.4) and neither is a bump (R4.5), and the area of the part is the only thing left that
    // tells them apart. The small part is 2 mm long and 0.45 mm wide, so its area is below the
    // threshold while its footprint still reaches past it.
    const indexed_triangle_set slab = ShapeBuilder{}.cube(20., 20., 2., 0.).build();
    const Slic3r::AABBMesh mesh{slab};

    const Slic3r::Domain::ExPolygon island = rectangle(-4., -4., 4., 4.); // 64 mm2
    const Slic3r::Domain::ExPolygon speck  = rectangle(-4., -0.225, -2., 0.225); // 0.9 mm2

    // One layer on the underside of the slab and one on its upper face, so that the points of the
    // upper face are far enough above the lowest island to be islands and not anchors of R4.1.
    constexpr double layer_height = 0.05;
    Slic3r::sla::Layers layers(2);
    layers[0].print_z = float(0.025);
    layers[0].parts   = {layer_part(&island), layer_part(&speck)};
    layers[1].print_z = float(2.025);
    layers[1].parts   = {layer_part(&island), layer_part(&speck)};
    // The parts are linked the way the generator links them: what is on a layer stands on what is
    // on the one below, and that is what tells a spike tip from the flat top of a thin pillar.
    for (size_t part = 0; part < 2; ++part) {
        layers[0].parts[part].next_parts.push_back(layers[1].parts.begin() + part);
        layers[1].parts[part].prev_parts.push_back(layers[0].parts.begin() + part);
    }

    SupportPoints points(4);
    points[0].type = SupportPointType::island;
    points[0].pos  = Slic3r::Domain::Vec3f{-1.5f, 0.f, 0.f}; // the lowest island of the object
    points[1].type = SupportPointType::island;
    points[1].pos  = Slic3r::Domain::Vec3f{1.5f, 0.f, 2.f}; // an island of 64 mm2 above it
    points[2].type = SupportPointType::island;
    points[2].pos  = Slic3r::Domain::Vec3f{-3.f, 0.f, 2.f}; // the 0.9 mm2 speck next to it
    points[3].type = SupportPointType::slope;
    points[3].pos  = Slic3r::Domain::Vec3f{1.5f, 0.f, 0.f}; // an overhang, not an island at all

    Slic3r::sla::classify_support_point_roles(points, mesh, layers, layer_height);

    CHECK(points[0].role == Role::Anchor); // R4.1, the lowest island of the object
    CHECK(points[1].role == Role::Island); // R4.3, an island that is not small
    CHECK(points[2].role == Role::SmallIsland); // R4.3, very small ones
    CHECK(points[3].role == Role::Overhang); // R4.6, everything else is a light support

    // The roles do not move the points: nothing here is small surface detail, the slab is flat.
    for (const SupportPoint& point : points) {
        INFO("point at " << point.pos.x() << ", " << point.pos.y() << ", " << point.pos.z());
        CHECK(point.role != Role::Fragile);
    }
}

TEST_CASE("A point that is not on the model is a light overhang", "[SupportRoles]")
{
    // There is no surface under a point that fell off the model and no layer it is on, so there is
    // no thickness, no island and no bump to be told apart there: such a point carries nothing that
    // makes it fragile or an anchor, so it is an overhang like any other (R4.6).
    const indexed_triangle_set slab = ShapeBuilder{}.cube(20., 20., 2., 0.).build();
    const Slic3r::AABBMesh mesh{slab};

    Slic3r::sla::Layers layers(1);
    layers[0].print_z = float(0.025);

    SupportPoints points(2);
    points[0].type = SupportPointType::slope;
    points[0].pos  = Slic3r::Domain::Vec3f{100.f, 100.f, 10.f}; // nowhere near the slab
    points[1].type = SupportPointType::slope;
    points[1].pos  = Slic3r::Domain::Vec3f{0.5f, 0.5f, 0.f}; // on the underside of the slab

    Slic3r::sla::classify_support_point_roles(points, mesh, layers, 0.05);

    CHECK(points[0].role == Role::Overhang);
    CHECK(points[1].role == Role::Overhang);

    // And the point that is nowhere stays where it was put: there was no surface to move it onto.
    CHECK(points[0].pos.x() == 100.f);
    CHECK(points[0].pos.y() == 100.f);
    CHECK(points[0].pos.z() == 10.f);
}
