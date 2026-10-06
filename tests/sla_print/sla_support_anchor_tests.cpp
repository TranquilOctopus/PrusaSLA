// M7.8.3: the heavy anchors of the rulebook R4.2 (doc/sla-fork/supports/rulebook.md) - a few T0.4
// anchors on the flat, low-detail areas of the surface that faces the build plate, about two on a
// small miniature, more with the size of the footprint, and the largest tip (T0.6) on a very large
// object. The shapes below are written from scratch, so that every measurement the anchors are placed
// by has one known value: how flat the surface around a spot is, how thick the feature under it is,
// and where the plate-facing areas of the model are.
//
// What the role of a point means to the tool, i.e. which tip class it takes, is not here: that is the
// mapping of the roles in the tool (SlaSupportRolesTests.cpp in slic3r-shared).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <vector>

#include "Slic3r/Biz/Algorithms/AABBMesh.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/ConfigBoxesSLA.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "Slic3r/Domain/PrinterTechnology.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "libslic3r/SLA/SupportAnchors.hpp"
#include "libslic3r/SLASupportTool.hpp"

using Catch::Approx;
namespace triangle_mesh = Slic3r::Biz::Algorithms::TriangleMesh;

using Slic3r::Domain::Vec3f;
using Slic3r::Domain::SLA::SupportPoint;
using Slic3r::Domain::SLA::SupportPoints;
using Slic3r::Domain::SLA::SupportPointType;
using Role = Slic3r::Domain::SLA::SupportPoint::Role;
using Slic3r::sla::add_heavy_anchors;
using Slic3r::sla::AnchorPlacement;

namespace {

/// The tool API takes the raw config pointers and resolves the SLA object view itself, and the
/// miniature test below generates its points the way the tool does (see
/// sla_support_roles_tests.cpp, which builds the same config).
struct SlaConfig
{
    Slic3r::Domain::FullConfigSLAPtr full;
    Slic3r::Domain::PartialObjectConfigSLAPtr object_settings;
};

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

/// A model with one object holding one mesh, which is the shape the test below runs the generator on.
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
    /// A box, centred over the origin in x and y.
    ShapeBuilder& box(double x, double y, double z, double lift = 0.)
    {
        indexed_triangle_set cube = triangle_mesh::its_make_cube(x, y, z);
        for (Vec3f& v : cube.vertices) {
            v.x() -= float(x / 2.);
            v.y() -= float(y / 2.);
            v.z() += float(lift);
        }
        return add(cube);
    }

    /// A small cylinder at @a x, from z = @a lift to z = @a lift + @a h. A negative lift hangs it
    /// below the underside of a box, which is what a stud on a surface is.
    ShapeBuilder& stud(double x, double r, double h, double lift)
    {
        indexed_triangle_set stud = triangle_mesh::its_make_cylinder(r, h);
        for (Vec3f& v : stud.vertices) {
            v.x() += float(x);
            v.z() += float(lift);
        }
        return add(stud);
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

/// The studs of the miniature: 1 mm across and 0.4 mm deep bumps hanging under a 30 x 30 mm box, in a
/// row along the x axis. They are the small surface detail of R4.5, the kind a point is moved off.
constexpr double stud_radius_mm = 0.5;
constexpr double stud_depth_mm  = 0.4;
const std::vector<double> stud_centres_x{-12., -6., 0., 6., 12.};

indexed_triangle_set miniature_shape()
{
    ShapeBuilder shape;
    shape.box(30., 30., 20.);
    for (const double x : stud_centres_x)
        shape.stud(x, stud_radius_mm, stud_depth_mm, -stud_depth_mm);
    return shape.build();
}

/// How far a spot is from the axis of the nearest stud of the miniature, in mm.
double distance_from_a_stud(const SupportPoint& point)
{
    double best = std::numeric_limits<double>::max();
    for (const double x : stud_centres_x)
        best = std::min(best, std::hypot(double(point.pos.x()) - x, double(point.pos.y())));
    return best;
}

/// A point as the generator leaves it: on the surface of the model, with a role and a head radius.
SupportPoint point_at(Vec3f pos, Role role, SupportPointType type = SupportPointType::island)
{
    SupportPoint point;
    point.pos               = pos;
    point.type              = type;
    point.role              = role;
    point.head_front_radius = 0.1f;
    return point;
}

bool is_anchor(const SupportPoint& point)
{
    return point.role == Role::Anchor || point.role == Role::AnchorLarge;
}

/// Every pair of points of a model a spacing apart or further, which is the rule R4.2 places its new
/// points by.
void check_spacing(const SupportPoints& points)
{
    for (size_t i = 0; i < points.size(); ++i)
        for (size_t j = i + 1; j < points.size(); ++j) {
            INFO("points " << i << " and " << j << " of " << points.size());
            CHECK((points[i].pos - points[j].pos).norm() >= 2.99);
        }
}

/// Sort support points by (z, y, x) so that two runs of the generator on the same shape can be
/// compared as sets. The generator uses parallel execution (TBB), so the order of points in the
/// result vector is non-deterministic, but the set of points and their roles is deterministic.
void sort_points_by_zyx(SupportPoints& points)
{
    std::sort(points.begin(), points.end(), [](const SupportPoint& a, const SupportPoint& b) {
        if (a.pos.z() != b.pos.z()) return a.pos.z() < b.pos.z();
        if (a.pos.y() != b.pos.y()) return a.pos.y() < b.pos.y();
        return a.pos.x() < b.pos.x();
    });
}

} // namespace

TEST_CASE("The heavy anchors of a small miniature are on its flat underside", "[SupportAnchors]")
{
    const indexed_triangle_set shape = miniature_shape();
    const Slic3r::AABBMesh mesh{shape};

    // The points a model like this comes with: the point of the generator that is on a stud, which
    // R4.4 makes fragile and R4.5 may have moved off the stud. The underside itself has no point yet,
    // which is the case the two anchors of R4.2 are new points for.
    SupportPoints points{point_at(Vec3f{0.f, 0.f, -0.4f}, Role::Fragile)};
    const size_t before = points.size();

    const AnchorPlacement placed = add_heavy_anchors(points, mesh);

    // R4.2: about two anchors on a small miniature, and a footprint that fits in 30 x 30 mm is the
    // line at which the count starts to grow.
    CHECK(placed.wanted == 2);
    CHECK(placed.placed == 2);
    CHECK(placed.added == 2);
    CHECK(placed.promoted == 0);
    CHECK(placed.large == false);
    CHECK(points.size() == before + 2);

    // Every one of them is on the flat underside of the box. The caps of the studs are 0.4 mm below
    // it, and a spot on one of them has the flat surface the stud stands on above itself instead of
    // around itself, which is not flat ground for an anchor.
    for (size_t i = before; i < points.size(); ++i) {
        const SupportPoint& anchor = points[i];
        INFO("anchor at " << anchor.pos.x() << ", " << anchor.pos.y() << ", " << anchor.pos.z());
        CHECK(anchor.pos.z() == Approx(0.).margin(0.01));
        CHECK(anchor.role == Role::Anchor);
        CHECK(anchor.is_island());
        CHECK(anchor.head_front_radius > 0.f);
        // The spots of the grid are 3 mm apart, so no anchor of the pass can stand closer than half a
        // cell to the axis of a stud, and none of them stands on a stud a millimetre across.
        CHECK(distance_from_a_stud(anchor) > 1.5);
    }

    // And none of them stands where a point of the model already stands.
    check_spacing(points);

    // The fragile point of R4.4 keeps its role: a heavy anchor is the one support that would take a
    // bump off with it, so it never becomes one.
    CHECK(points[0].role == Role::Fragile);
    CHECK_FALSE(is_anchor(points[0]));
}

TEST_CASE(
    "A large flat plate takes as many heavy anchors as its footprint asks for",
    "[SupportAnchors]"
)
{
    // A 40 x 40 mm plate: the two anchors of a footprint that fits in 30 x 30 mm, and one more for the
    // 700 mm2 of footprint above that. The plate is flat and thick, so every spot of its underside may
    // carry an anchor.
    const indexed_triangle_set medium_shape = ShapeBuilder{}.box(40., 40., 4.).build();
    const Slic3r::AABBMesh medium{medium_shape};
    SupportPoints medium_points;
    const AnchorPlacement medium_placed = add_heavy_anchors(medium_points, medium);
    CHECK(medium_placed.wanted == 3);
    CHECK(medium_placed.placed == 3);
    CHECK(medium_placed.added == 3);
    CHECK(medium_placed.large == false);

    // A 60 x 60 mm plate is past the cap of R4.2, which is eight anchors, whatever its footprint is.
    const indexed_triangle_set large_shape = ShapeBuilder{}.box(60., 60., 4.).build();
    const Slic3r::AABBMesh large{large_shape};
    SupportPoints large_points;
    const AnchorPlacement large_placed = add_heavy_anchors(large_points, large);
    CHECK(large_placed.wanted == 8);
    CHECK(large_placed.placed == 8);
    CHECK(large_placed.added == 8);
    REQUIRE(large_points.size() == 8);
    // A big plate is not a very large object: it does not fill a mid-size printer's plate, so its
    // anchors take the heavy tip and not the largest one.
    CHECK(large_placed.large == false);

    // Every anchor of both plates is on the flat underside, and they are spread over it rather than
    // stacked on one spot: the spots beside an anchor that has just been placed are the last ones
    // tried.
    for (const SupportPoints* points : {&medium_points, &large_points}) {
        for (const SupportPoint& anchor : *points) {
            INFO(
                "anchor at " << anchor.pos.x() << ", " << anchor.pos.y() << ", " << anchor.pos.z()
            );
            CHECK(anchor.role == Role::Anchor);
            CHECK(anchor.pos.z() == Approx(0.).margin(0.01));
        }
        check_spacing(*points);
    }
}

TEST_CASE("The anchors of a very large model take the largest tip", "[SupportAnchors]")
{
    // A 200 x 200 mm plate fills a mid-size printer's plate, so the size table of the rulebook gives
    // its anchors the largest tip (T0.6), which is the AnchorLarge role.
    const indexed_triangle_set large_shape = ShapeBuilder{}.box(200., 200., 10.).build();
    const Slic3r::AABBMesh mesh{large_shape};

    // The lowest point of the model is an anchor of R4.1 before this pass sees the model, and it is
    // one of the anchors of the model: the rule is about the anchors of a very large object and not
    // about the ones this pass adds only.
    SupportPoints points{point_at(Vec3f{100.f, 100.f, 0.f}, Role::Anchor)};
    const AnchorPlacement placed = add_heavy_anchors(points, mesh);

    CHECK(placed.large == true);
    CHECK(placed.wanted == 8);
    CHECK(placed.placed == 8);
    CHECK(placed.promoted == 0);
    REQUIRE(points.size() == 9);
    for (const SupportPoint& anchor : points) {
        INFO("anchor at " << anchor.pos.x() << ", " << anchor.pos.y() << ", " << anchor.pos.z());
        CHECK(anchor.role == Role::AnchorLarge);
        CHECK(anchor.pos.z() == Approx(0.).margin(0.01));
    }
    check_spacing(points);
}

TEST_CASE(
    "An anchor is a point of the generator rather than a new one beside it",
    "[SupportAnchors]"
)
{
    // A slab standing on the plate, 40 x 12 mm in the plane and 20 mm tall: the only surface of it
    // that faces the plate is the strip of its underside, and the generator leaves slope points on
    // that strip.
    const indexed_triangle_set slab_shape = ShapeBuilder{}.box(40., 12., 20.).build();
    const Slic3r::AABBMesh mesh{slab_shape};

    // Points all over the strip, close enough together that every spot of the pass has one within
    // reach of it, which is what a generator that samples a flat face does.
    SupportPoints points;
    for (double x = -18.; x <= 18.; x += 2.)
        for (double y = -3.; y <= 3.; y += 3.)
            points.push_back(
                point_at(Vec3f{float(x), float(y), 0.f}, Role::Overhang, SupportPointType::slope)
            );
    const size_t before = points.size();
    REQUIRE(before > 0);

    const AnchorPlacement placed = add_heavy_anchors(points, mesh);

    // A footprint that fits in 30 x 30 mm takes two anchors, and a point that already stands where an
    // anchor would is the anchor: one support of one area and not two points a spacing apart.
    CHECK(placed.wanted == 2);
    CHECK(placed.placed == 2);
    CHECK(placed.promoted == 2);
    CHECK(placed.added == 0);
    REQUIRE(points.size() == before);

    size_t anchors = 0;
    for (const SupportPoint& point : points)
        anchors += is_anchor(point) ? 1 : 0;
    CHECK(anchors == 2);
}

TEST_CASE("No heavy anchor stands on a surface that faces away from the plate", "[SupportAnchors]")
{
    // A 40 x 40 x 3 mm plate with a wall of 40 x 3 x 20 mm standing on it. The underside of the plate
    // is the only surface of the model that faces the build plate: the top of the plate faces up, the
    // two large faces of the wall are vertical, and the plate is the whole footprint of the model, so
    // the anchors of R4.2 go on its underside and nowhere else.
    const indexed_triangle_set plate_wall_shape = ShapeBuilder{}.box(40., 40., 3.).box(40., 3., 20., 3.).build();
    const Slic3r::AABBMesh mesh{plate_wall_shape};

    SupportPoints points;
    const AnchorPlacement placed = add_heavy_anchors(points, mesh);

    CHECK(placed.wanted == 3);
    CHECK(placed.placed == 3);
    REQUIRE(points.size() == 3);
    for (const SupportPoint& anchor : points) {
        INFO("anchor at " << anchor.pos.x() << ", " << anchor.pos.y() << ", " << anchor.pos.z());
        CHECK(anchor.role == Role::Anchor);
        // The underside of the plate is at z = 0, its top at z = 3 and the wall from z = 3 up, so an
        // anchor anywhere else would be on a surface that does not face the plate.
        CHECK(anchor.pos.z() == Approx(0.).margin(0.01));
    }
    check_spacing(points);
}

TEST_CASE("The same model gets the same heavy anchors every run", "[SupportAnchors]")
{
    // The spots are found on a grid and ranked by how much flat surface is around them, and the
    // points they are placed from arrive in the order the generator made them, so the same shape has
    // to get the same anchors again: the tool generates the points of an object again and again while
    // the settings are moved, and the points it shows may not jump around.
    const indexed_triangle_set plate_shape = ShapeBuilder{}.box(60., 60., 4.).build();
    const Slic3r::AABBMesh mesh{plate_shape};

    SupportPoints first{point_at(Vec3f{10.f, 10.f, 0.f}, Role::Anchor)};
    SupportPoints second{point_at(Vec3f{10.f, 10.f, 0.f}, Role::Anchor)};
    const AnchorPlacement first_placed  = add_heavy_anchors(first, mesh);
    const AnchorPlacement second_placed = add_heavy_anchors(second, mesh);

    REQUIRE(first.size() == second.size());
    CHECK(first_placed.placed == second_placed.placed);
    CHECK(first_placed.added == second_placed.added);
    // add_heavy_anchors is deterministic, but sort to be safe.
    SupportPoints first_sorted  = first;
    SupportPoints second_sorted = second;
    sort_points_by_zyx(first_sorted);
    sort_points_by_zyx(second_sorted);
    for (size_t i = 0; i < first_sorted.size(); ++i) {
        INFO("point " << i << " at " << first_sorted[i].pos.x() << ", " << first_sorted[i].pos.y());
        const Slic3r::Domain::Vec3d here  = first_sorted[i].pos.cast<double>();
        const Slic3r::Domain::Vec3d there = second_sorted[i].pos.cast<double>();
        CHECK(Slic3r::Domain::is_approx(there, here, 1e-4));
        CHECK(second_sorted[i].role == first_sorted[i].role);
    }
}

namespace {

/// The points of a miniature, generated the way the support tool generates them: the slice, the
/// generator, the points on the mesh surface, the roles of M7.8.2 and the anchors of M7.8.3.
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

} // namespace

TEST_CASE(
    "A miniature the tool generates the points of gets its anchors on the underside",
    "[SupportAnchors]"
)
{
    // The whole path the support tool takes, so that the pass is in it: the anchors of R4.2 are
    // placed by generate_support_points_for_tool and come back in the frame of the model.
    const indexed_triangle_set shape = miniature_shape();
    const SupportPoints first        = generate(shape);
    REQUIRE_FALSE(first.empty());

    // R4.2 with R4.1: every anchor of the model is on the flat underside of the box, and none of them
    // is on a stud, whose cap is 0.4 mm below the underside. The lowest island of this miniature is
    // the one of the studs, and a point on a stud is fragile (R4.4), so the anchors of the underside
    // are the ones R4.2 places and not the ones R4.1 makes of the lowest island.
    size_t anchors = 0;
    for (const SupportPoint& point : first) {
        INFO("point at " << point.pos.x() << ", " << point.pos.y() << ", " << point.pos.z());
        if (!is_anchor(point))
            continue;
        ++anchors;
        CHECK(point.pos.z() == Approx(0.).margin(0.06));
    }
    CHECK(anchors >= 2);

    // The same shape gives the same points, and the same roles, on a second run of the same shape.
    // The generator uses parallel execution (TBB), so the order of points is non-deterministic.
    // Sort both runs by (z, y, x) before comparing them as sets.
    const SupportPoints second = generate(shape);
    REQUIRE(first.size() == second.size());
    SupportPoints first_sorted  = first;
    SupportPoints second_sorted = second;
    sort_points_by_zyx(first_sorted);
    sort_points_by_zyx(second_sorted);
    for (size_t i = 0; i < first_sorted.size(); ++i) {
        INFO("point " << i);
        const Slic3r::Domain::Vec3d here  = first_sorted[i].pos.cast<double>();
        const Slic3r::Domain::Vec3d there = second_sorted[i].pos.cast<double>();
        CHECK(Slic3r::Domain::is_approx(there, here, 1e-4));
        CHECK(second_sorted[i].role == first_sorted[i].role);
    }
}
