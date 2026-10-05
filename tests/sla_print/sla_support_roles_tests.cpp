// M7.8.2: every support point the generator makes gets a role, and a point does not stay on a
// fragile feature or on small surface detail (the support rulebook R4.1 and R4.3 - R4.6 of
// doc/sla-fork/supports/rulebook.md). M7.8.5 adds the one rule that overrides another: a point in a
// detailed region gets the minimum tip whatever its role is, except the anchor of the lowest island
// (R4.9). The shapes below are written from scratch so that every measurement the roles are decided
// by has one known value: how thick the feature under a point is, how wide the part it is on is, how
// far the point has to move to get off a stud, and how fine the surface around it is.
//
// What a role means to the tool, i.e. which tip class it takes, is not here: that is the mapping of
// M7.8.2 in the tool (SlaSupportRolesTests.cpp in slic3r-shared).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
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
using Slic3r::Domain::Vec3f;
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

namespace {

using Slic3r::sla::classify_support_point_roles;
// The three indices of a triangle of a mesh built here by hand: Index3 is a std::array of three
// ints, so the indices of a triangle go in as ints.
using Slic3r::Domain::Index3;

// R4.9: the numbers the detail of a region is read by, repeated here so that a test says what it
// relies on. They are what SupportRoleThresholds carries by default, and the classifier below is
// given them rather than its own defaults, so that a change of the default shows up as a failing
// test instead of as a silently different model.
constexpr double detail_radius_mm = 1.5;
constexpr double detail_sag_mm    = 0.2;
constexpr double detail_turns     = 2.0;

// The layer height the shapes below are asked about, as the generator sampled them.
constexpr double layer_height_mm = 0.05;

// The relief of the shapes below: a field of square pyramids 0.5 mm apart and 0.5 mm tall. That is
// the fine, dense surface R4.9 is about - the relief of a sculpted face, the mesh of a chain mail,
// the texture of a miniature - and it is not one raised bump on a plain plane either, so R4.5 has no
// stud to move a point off, and nothing of it is thin, so R4.4 has no reason to make a point
// fragile: a point on it is on as sound a surface as a point on a plate, which is what lets the
// tests below tell R4.9 from the rules around it.
constexpr double relief_pitch_mm     = 0.5;
constexpr double relief_height_mm    = 0.5;
constexpr size_t relief_cells        = 12; // 12 x 12 cells, so 6 x 6 mm of relief
constexpr double relief_thickness_mm = 2.0;

/// The relief of a plate, so that a test can name the spot it looks at instead of a pair of
/// millimetres that mean nothing on their own.
struct Relief
{
    double apex_z; // the tip of the pyramids, [in mm]
    Vec3f apex; // the tip of the pyramid in the middle of the field
    Vec3f other_apex; // and the tip of the one a few cells away from it
};

/// A plate 6 x 6 mm and 2 mm thick, standing @p lift above the plate, whose underside is a field of
/// square pyramids of relief_pitch_mm with their apexes relief_height_mm below the flat face the
/// plate would have had. With @p relief off it is the same plate with that flat face instead, which
/// is the control: the two shapes differ in nothing else, so a difference in the roles of their
/// points is the relief and not the size or the thickness of the plate.
///
/// The shape is a roof and not a solid: the field (or the flat face) under it and the flat face on
/// top, without the four sides. What R4.9 measures is a ray cast up or down through the middle of
/// the plate, so the sides would only say where the surface ends, and no test here needs them.
indexed_triangle_set relief_plate(double lift, bool relief)
{
    const double side = double(relief_cells) * relief_pitch_mm;
    const size_t grid = relief_cells + 1;

    const auto at = [=](double i, double j, double z)
    {
        return Vec3f{
            float((i - 0.5 * side) * relief_pitch_mm),
            float((j - 0.5 * side) * relief_pitch_mm),
            float(z)
        };
    };

    indexed_triangle_set its;
    its.vertices.reserve(2 * grid * grid + relief_cells * relief_cells);
    its.indices.reserve(5 * relief_cells * relief_cells);

    // The grid the field stands on, at the height of the flat underside: one vertex per grid point,
    // shared by the cells around it.
    for (size_t j = 0; j < grid; ++j)
        for (size_t i = 0; i < grid; ++i)
            its.vertices.push_back(at(double(i), double(j), lift));

    // One apex in the middle of every cell, this far below the face it hangs under.
    const size_t apexes = its.vertices.size();
    if (relief) {
        for (size_t j = 0; j < relief_cells; ++j)
            for (size_t i = 0; i < relief_cells; ++i) {
                const Vec3f apex = at(double(i) + 0.5, double(j) + 0.5, lift - relief_height_mm);
                its.vertices.push_back(apex);
            }
    }

    // The top of the plate, one vertex per grid point again.
    const size_t top = its.vertices.size();
    for (size_t j = 0; j < grid; ++j)
        for (size_t i = 0; i < grid; ++i)
            its.vertices.push_back(at(double(i), double(j), lift + relief_thickness_mm));

    for (size_t j = 0; j < relief_cells; ++j) {
        for (size_t i = 0; i < relief_cells; ++i) {
            const int v[4]{
                int(j * grid + i),
                int(j * grid + i + 1),
                int((j + 1) * grid + i + 1),
                int((j + 1) * grid + i)
            };
            const int t[4]{
                int(top + j * grid + i),
                int(top + j * grid + i + 1),
                int(top + (j + 1) * grid + i + 1),
                int(top + (j + 1) * grid + i)
            };

            if (relief) {
                // The apex first and the corners the other way round, which points the face down and
                // out of the material: that is the way the normals and the ray casts of the file
                // read.
                const int apex = int(apexes + j * relief_cells + i);
                for (int k = 0; k < 4; ++k)
                    its.indices.push_back(Index3{apex, v[(k + 1) % 4], v[k]});
            } else {
                // Without the relief the underside is the flat face it would have been: two triangles
                // per cell, wound the other way round, so that the normals point down.
                its.indices.push_back(Index3{v[0], v[2], v[1]});
                its.indices.push_back(Index3{v[0], v[3], v[2]});
            }

            // The top of the plate, wound so that its normals point up.
            its.indices.push_back(Index3{t[0], t[1], t[2]});
            its.indices.push_back(Index3{t[0], t[2], t[3]});
        }
    }

    return its;
}

/// The spots of the relief of the plate standing @p lift above the plate that the tests below put
/// their points on. The field is centred on the origin, so the tips of its pyramids are 2.75, 2.25,
/// ... 0.25 mm from it on either axis, and every spot here is well inside the field: a point 1.5 mm
/// from its rim would see the surface end and would have nothing to measure.
Relief relief_spots(double lift)
{
    const float pitch = float(relief_pitch_mm);
    return {
        lift - relief_height_mm,
        Vec3f{0.5f * pitch, 0.5f * pitch, float(lift - relief_height_mm)},
        Vec3f{1.5f * pitch, 0.5f * pitch, float(lift - relief_height_mm)}
    };
}

/// The point at the middle of the lowest triangle of a mesh. On a sphere that is a spot where the
/// normal of the surface is the normal of the facet itself, so the plane through the point is
/// tangent to the sphere instead of tilted by however the sphere happens to be cut up there.
Vec3f lowest_triangle_middle(const indexed_triangle_set& its)
{
    double lowest = std::numeric_limits<double>::max();
    Vec3f middle{0.f, 0.f, 0.f};
    for (const auto& triangle : its.indices) {
        const Vec3f a = its.vertices[triangle[0]];
        const Vec3f b = its.vertices[triangle[1]];
        const Vec3f c = its.vertices[triangle[2]];
        const Vec3f m{
            (a.x() + b.x() + c.x()) / 3.f,
            (a.y() + b.y() + c.y()) / 3.f,
            (a.z() + b.z() + c.z()) / 3.f
        };
        if (m.z() < lowest) {
            lowest = m.z();
            middle = m;
        }
    }
    return middle;
}

/// Two slices of a model with one part each: the lowest one, and one just above @p upper_z, which is
/// where R4.3 asks the area of for the points standing on the upper plate of a two level model.
void fill_layers(Slic3r::sla::Layers& layers, const Slic3r::Domain::ExPolygon& part, double upper_z)
{
    layers[0].print_z = float(0.5 * layer_height_mm);
    layers[0].parts   = {layer_part(&part)};
    layers[1].print_z = float(upper_z);
    layers[1].parts   = {layer_part(&part)};
}

} // namespace

TEST_CASE("A plain surface is not a detailed region", "[SupportRoles]")
{
    // R4.9 is about a surface that is fine, dense or highly curved, and the underside of a plate is
    // none of the three: however the plate is cut up into triangles, its surface leaves the plane
    // through a point on it nowhere at all. The two points are the roles R4.9 would take away, the
    // anchor of the lowest island and an overhang, and neither is taken.
    const indexed_triangle_set plate = relief_plate(0., false);
    const Slic3r::AABBMesh mesh{plate};
    const Slic3r::Domain::ExPolygon footprint = rectangle(-3., -3., 3., 3.);

    // The flat underside of the plate is at z = 0 and both points are well inside it: a point
    // 1.5 mm from its rim would see the surface end and would have nothing to measure.
    SupportPoints points(2);
    points[0].type = SupportPointType::island;
    points[0].pos  = Vec3f{0.f, 0.f, 0.f};
    points[1].type = SupportPointType::slope;
    points[1].pos  = Vec3f{2.f, 1.f, 0.f};

    Slic3r::sla::SupportRoleThresholds thresholds;
    CHECK(thresholds.detail_radius_mm == Approx(detail_radius_mm));
    CHECK(thresholds.detail_sag_mm == Approx(detail_sag_mm));
    CHECK(thresholds.detail_turns == Approx(detail_turns));

    Slic3r::sla::Layers layers(2);
    fill_layers(layers, footprint, relief_thickness_mm + 0.5 * layer_height_mm);
    classify_support_point_roles(points, mesh, layers, layer_height_mm, thresholds);

    CHECK(points[0].role == Role::Anchor);
    CHECK(points[1].role == Role::Overhang);
    for (const SupportPoint& point : points) {
        INFO("point at " << point.pos.x() << ", " << point.pos.y() << ", " << point.pos.z());
        CHECK(point.role != Role::Detail);
    }
}

TEST_CASE("The relief of a fine, dense surface is a detailed region", "[SupportRoles]")
{
    // Two plates of the same relief, the upper one 4 mm above the lower one, so that the points of
    // the lower one are the anchors of the lowest island (R4.1) and the points of the upper one are
    // an island of their own (R4.3). The relief stands half a millimetre out of the face it hangs
    // under every 0.5 mm, which is where a miniature loses its detail to a support of its own: the
    // most common auto-support failure the maintainer sees (R4.9).
    indexed_triangle_set shape       = relief_plate(0., true);
    const indexed_triangle_set upper = relief_plate(4., true);
    Slic3r::Domain::its_merge(shape, upper);

    const Slic3r::AABBMesh mesh{shape};
    const Relief low                          = relief_spots(0.);
    const Relief high                         = relief_spots(4.);
    const Slic3r::Domain::ExPolygon footprint = rectangle(-3., -3., 3., 3.);

    SupportPoints points(3);
    points[0].type = SupportPointType::island; // the lowest point of the object
    points[0].pos  = low.apex;
    points[1].type = SupportPointType::island; // an island of its own, 4 mm above it
    points[1].pos  = high.apex;
    points[2].type = SupportPointType::slope; // the extra support of the relief of the lowest plate
    points[2].pos  = low.other_apex;

    Slic3r::sla::SupportRoleThresholds thresholds;
    Slic3r::sla::Layers layers(2);
    fill_layers(layers, footprint, high.apex_z + 0.5 * layer_height_mm);
    classify_support_point_roles(points, mesh, layers, layer_height_mm, thresholds);

    // R4.1 wins over R4.9: the anchor of the lowest island is the one role the rule of the detail
    // does not replace, whatever the surface under it is like. The whole part hangs on those points
    // in the first layers of the print, and a minimum tip under them would not hold it.
    CHECK(points[0].role == Role::Anchor);

    // The same relief of an island that is not the lowest, so R4.3 would have given the point the
    // medium class: R4.9 takes it away and the support in the relief is the lightest one.
    CHECK(points[1].role == Role::Detail);

    // And the same relief as an overhang, which is what most of the points on a relief are.
    CHECK(points[2].role == Role::Detail);

    // Nothing here was thin and nothing was moved: the relief is under the plate rather than on it,
    // so R4.4 has no reason to call a point fragile and R4.5 has no stud to move it off.
    for (const SupportPoint& point : points) {
        INFO("point at " << point.pos.x() << ", " << point.pos.y() << ", " << point.pos.z());
        CHECK(point.role != Role::Fragile);
    }
    CHECK(points[0].pos.x() == Approx(low.apex.x()));
    CHECK(points[0].pos.z() == Approx(low.apex.z()));
    CHECK(points[2].pos.x() == Approx(low.other_apex.x()));
    CHECK(points[2].pos.z() == Approx(low.other_apex.z()));
}

TEST_CASE("A smooth curve is detail where it curves hard and nowhere else", "[SupportRoles]")
{
    // The "highly curved" half of R4.9 is read off how far the surface leaves the plane through the
    // point: a sphere of radius R has r2 / 2R of that 1.5 mm out, which is 0.056 mm on a 20 mm
    // sphere and 0.375 mm on a 3 mm one, i.e. a bead or a finger against the side of a barrel. Both
    // shapes are spheres and both points are the same kind of point, so whatever the two answers
    // are, the radius is what made them.
    const indexed_triangle_set barrel = triangle_mesh::its_make_sphere(20., 0.1);
    const indexed_triangle_set bead   = triangle_mesh::its_make_sphere(3., 0.1);
    const Slic3r::AABBMesh barrel_mesh{barrel};
    const Slic3r::AABBMesh bead_mesh{bead};

    SupportPoints barrel_pts(1);
    barrel_pts[0].type = SupportPointType::slope;
    barrel_pts[0].pos  = lowest_triangle_middle(barrel);

    SupportPoints bead_pts(1);
    bead_pts[0].type = SupportPointType::slope;
    bead_pts[0].pos  = lowest_triangle_middle(bead);

    // No part of any layer to stand on: a point under a sphere is on no layer part of a real slice
    // either, and this is about the surface around the point, not about the island it may be part
    // of.
    Slic3r::sla::Layers layers(1);
    layers[0].print_z = float(100.);

    Slic3r::sla::SupportRoleThresholds thresholds;
    classify_support_point_roles(barrel_pts, barrel_mesh, layers, layer_height_mm, thresholds);
    classify_support_point_roles(bead_pts, bead_mesh, layers, layer_height_mm, thresholds);

    CHECK(barrel_pts[0].role == Role::Overhang);
    CHECK(bead_pts[0].role == Role::Detail);
}
