// M2.23: the shape of the foot where a pillar meets the raft or the build plate. The tree
// has always built a cone there, whose height is the base height: a gradual flare from the
// base diameter down to the pillar. Lychee can print a straight cylinder foot or a thin
// flat disc instead (lychee-parity.md row 14 and gap 9), and a support point may now ask
// for a shape of its own the way it already asks for its own base size.
//
// The points are placed by hand on a cube standing on the plate, so nothing but the
// config and the point decide how the foot is built: no support point generator, no
// printer preset.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Slic3r/Biz/Algorithms/AABBMesh.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "libslic3r/SLA/BranchingTreeSLA.hpp"
#include "libslic3r/SLA/DefaultSupportTree.hpp"
#include "libslic3r/SLA/SupportTree.hpp"
#include "libslic3r/SLA/SupportTreeBuilder.hpp"
#include "libslic3r/SLA/SupportTreeMesher.hpp"
#include "libslic3r/SLA/SupportTreeUtils.hpp"

using Catch::Approx;
namespace triangle_mesh = Slic3r::Biz::Algorithms::TriangleMesh;
using Slic3r::Domain::ConfigItemDef;
using Slic3r::Domain::sla::SupportBaseShape;
using Slic3r::Domain::SLA::SupportPoint;
using Slic3r::Domain::SLA::SupportPoints;

namespace {

// The cube stands on the plate, so its bottom face is at z = 0 and the single point of the
// tests sits on it, with a 6 mm gap under the model for the pillar to cross.
constexpr double cube_edge = 40.;
constexpr double elevation = 6.;

constexpr size_t steps = 45;

// A base tall enough for the flat disc to be the thinnest of the three feet: the disc is
// clamped to at most 0.5 mm, so with the 1 mm default base height a flat disc would hold
// more resin than the cone of the same diameter.
constexpr double base_height = 3.;
constexpr double base_radius = 2.;

SupportPoint make_point()
{
    SupportPoint point;
    point.pos               = Slic3r::Domain::Vec3f{8.f, 20.f, 0.f};
    point.head_front_radius = 0.2f;

    return point;
}

SupportPoints make_points()
{
    return SupportPoints{make_point()};
}

Slic3r::sla::SupportableMesh make_supportable_mesh(const SupportPoints &pts,
                                                  SupportBaseShape     shape = SupportBaseShape::Cone)
{
    Slic3r::sla::SupportTreeConfig cfg;
    cfg.object_elevation_mm = elevation;
    cfg.base_height_mm       = base_height;
    cfg.base_radius_mm       = base_radius;
    cfg.base_shape           = shape;

    Slic3r::sla::SupportableMesh sm{
        .emesh = Slic3r::AABBMesh(
            triangle_mesh::its_make_cube(cube_edge, cube_edge, cube_edge)),
        .pts = std::make_shared<const SupportPoints>(pts),
        .cfg = cfg};

    return sm;
}

const Slic3r::sla::Pedestal *first_pedestal(const Slic3r::sla::SupportTreeBuilder &builder)
{
    return builder.pedestals().empty() ? nullptr : &builder.pedestals().front();
}

// The mesh of the foot itself, which is what the shape changes: the pillar above it is the
// same for every shape.
indexed_triangle_set foot_mesh(const Slic3r::sla::Pedestal &ped)
{
    return Slic3r::sla::get_mesh(ped, steps);
}

// The volume enclosed by a closed mesh, from the divergence theorem. The sign depends on the
// winding of the primitive, so only the magnitude is used.
double volume(const indexed_triangle_set &mesh)
{
    double v = 0.;

    for (const auto &t : mesh.indices)
        v += mesh.vertices[t[0]].dot(mesh.vertices[t[1]].cross(mesh.vertices[t[2]]));

    return std::abs(v) / 6.;
}

// A closed mesh has every one of its edges exactly twice, in two triangles.
bool is_closed(const indexed_triangle_set &mesh)
{
    std::map<std::pair<int, int>, int> edges;

    for (const auto &t : mesh.indices)
        for (int i = 0; i < 3; ++i) {
            const int a = t[i];
            const int b = t[i == 2 ? 0 : i + 1];
            ++edges[std::minmax(a, b)];
        }

    for (const auto &edge : edges)
        if (edge.second != 2)
            return false;

    return true;
}

double foot_volume(const Slic3r::sla::SupportTreeBuilder &builder)
{
    const Slic3r::sla::Pedestal *ped = first_pedestal(builder);

    return ped != nullptr ? volume(foot_mesh(*ped)) : 0.;
}

const ConfigItemDef *find_def(const std::string &name)
{
    const auto &defs = Slic3r::Domain::get_defs_sla();
    for (const auto &def : defs.defs())
        if (def.name == name)
            return &def;

    return nullptr;
}

} // namespace

TEST_CASE("DefaultSupportTree::Every base shape is a closed foot of its own volume",
          "[suptreetree]")
{
    // A cylinder is the full base diameter over the full base height, a cone tapers from that
    // diameter down to the pillar and a flat disc is at most 0.5 mm of it, so the three feet
    // hold more resin in the order disc < cone < cylinder.
    Slic3r::sla::SupportableMesh disc_sm =
        make_supportable_mesh(make_points(), SupportBaseShape::Flat);
    Slic3r::sla::SupportableMesh cone_sm =
        make_supportable_mesh(make_points(), SupportBaseShape::Cone);
    Slic3r::sla::SupportableMesh cylinder_sm =
        make_supportable_mesh(make_points(), SupportBaseShape::Cylinder);

    Slic3r::sla::SupportTreeBuilder disc_builder;
    Slic3r::sla::create_default_tree(disc_builder, disc_sm);
    Slic3r::sla::SupportTreeBuilder cone_builder;
    Slic3r::sla::create_default_tree(cone_builder, cone_sm);
    Slic3r::sla::SupportTreeBuilder cylinder_builder;
    Slic3r::sla::create_default_tree(cylinder_builder, cylinder_sm);

    const Slic3r::sla::Pedestal *disc = first_pedestal(disc_builder);
    const Slic3r::sla::Pedestal *cone = first_pedestal(cone_builder);
    const Slic3r::sla::Pedestal *cylinder = first_pedestal(cylinder_builder);

    REQUIRE(disc != nullptr);
    REQUIRE(cone != nullptr);
    REQUIRE(cylinder != nullptr);

    CHECK(disc->shape == SupportBaseShape::Flat);
    CHECK(cone->shape == SupportBaseShape::Cone);
    CHECK(cylinder->shape == SupportBaseShape::Cylinder);

    // The size of the foot is the configured one for all three, only the shape differs, and
    // every one of them is a closed solid.
    for (const Slic3r::sla::Pedestal *ped : {disc, cone, cylinder}) {
        CHECK(ped->r_bottom == Approx(base_radius));
        CHECK(ped->height == Approx(base_height));
        CHECK(is_closed(foot_mesh(*ped)));
    }

    // A flat disc never gets taller than its clamp, whatever the base height is.
    CHECK(volume(foot_mesh(*disc)) ==
          Approx(volume(Slic3r::sla::cylinder(disc->r_bottom,
                                               Slic3r::sla::flat_base_height_mm, steps))));

    CHECK(foot_volume(disc_builder) < foot_volume(cone_builder));
    CHECK(foot_volume(cone_builder) < foot_volume(cylinder_builder));
}

TEST_CASE("DefaultSupportTree::The default base shape is the cone of today", "[suptreetree]")
{
    // Nothing configured: the foot is the half cone the tree has always built.
    REQUIRE(Slic3r::sla::SupportTreeConfig{}.base_shape == SupportBaseShape::Cone);

    Slic3r::sla::SupportableMesh sm = make_supportable_mesh(make_points());
    Slic3r::sla::SupportTreeBuilder builder;
    Slic3r::sla::create_default_tree(builder, sm);

    const Slic3r::sla::Pedestal *ped = first_pedestal(builder);

    REQUIRE(ped != nullptr);
    CHECK(ped->shape == SupportBaseShape::Cone);
    CHECK(foot_mesh(*ped) == Slic3r::sla::halfcone(ped->height, ped->r_bottom, ped->r_top,
                                                    ped->pos, steps));
}

TEST_CASE("DefaultSupportTree::A point may ask for a base shape of its own", "[suptreetree]")
{
    SupportPoints flat_points = make_points();
    flat_points[0].base_shape = SupportPoint::BaseShape::Flat;

    Slic3r::sla::SupportableMesh flat_sm =
        make_supportable_mesh(flat_points, SupportBaseShape::Cone);
    Slic3r::sla::SupportableMesh cylinder_sm =
        make_supportable_mesh(make_points(), SupportBaseShape::Cylinder);

    // The point's own shape wins over the configured one, and a point that does not ask for a
    // shape keeps the configured one.
    CHECK(Slic3r::sla::base_size(flat_sm, &flat_points[0]).shape == SupportBaseShape::Flat);
    CHECK(Slic3r::sla::base_size(cylinder_sm, &cylinder_sm.pts->front()).shape ==
          SupportBaseShape::Cylinder);
    CHECK(Slic3r::sla::base_size(cylinder_sm, nullptr).shape == SupportBaseShape::Cylinder);

    Slic3r::sla::SupportTreeBuilder flat_builder;
    Slic3r::sla::create_default_tree(flat_builder, flat_sm);
    Slic3r::sla::SupportTreeBuilder cylinder_builder;
    Slic3r::sla::create_default_tree(cylinder_builder, cylinder_sm);

    const Slic3r::sla::Pedestal *flat = first_pedestal(flat_builder);
    const Slic3r::sla::Pedestal *cylinder = first_pedestal(cylinder_builder);

    REQUIRE(flat != nullptr);
    REQUIRE(cylinder != nullptr);

    CHECK(flat->shape == SupportBaseShape::Flat);
    CHECK(cylinder->shape == SupportBaseShape::Cylinder);
    CHECK(foot_volume(flat_builder) < foot_volume(cylinder_builder));
}

TEST_CASE("BranchingSupportTree::Every base shape is a closed foot of its own volume",
          "[suptreetree]")
{
    Slic3r::sla::SupportableMesh disc_sm =
        make_supportable_mesh(make_points(), SupportBaseShape::Flat);
    Slic3r::sla::SupportableMesh cone_sm =
        make_supportable_mesh(make_points(), SupportBaseShape::Cone);
    Slic3r::sla::SupportableMesh cylinder_sm =
        make_supportable_mesh(make_points(), SupportBaseShape::Cylinder);

    Slic3r::sla::SupportTreeBuilder disc_builder;
    Slic3r::sla::create_branching_tree(disc_builder, disc_sm);
    Slic3r::sla::SupportTreeBuilder cone_builder;
    Slic3r::sla::create_branching_tree(cone_builder, cone_sm);
    Slic3r::sla::SupportTreeBuilder cylinder_builder;
    Slic3r::sla::create_branching_tree(cylinder_builder, cylinder_sm);

    const Slic3r::sla::Pedestal *disc = first_pedestal(disc_builder);
    const Slic3r::sla::Pedestal *cone = first_pedestal(cone_builder);
    const Slic3r::sla::Pedestal *cylinder = first_pedestal(cylinder_builder);

    REQUIRE(disc != nullptr);
    REQUIRE(cone != nullptr);
    REQUIRE(cylinder != nullptr);

    CHECK(disc->shape == SupportBaseShape::Flat);
    CHECK(cone->shape == SupportBaseShape::Cone);
    CHECK(cylinder->shape == SupportBaseShape::Cylinder);

    for (const Slic3r::sla::SupportTreeBuilder *builder :
         {&disc_builder, &cone_builder, &cylinder_builder}) {
        REQUIRE(!builder->pedestals().empty());
        for (const Slic3r::sla::Pedestal &ped : builder->pedestals()) {
            CHECK(ped.r_bottom == Approx(base_radius));
            CHECK(is_closed(foot_mesh(ped)));
        }
    }

    CHECK(foot_volume(disc_builder) < foot_volume(cone_builder));
    CHECK(foot_volume(cone_builder) < foot_volume(cylinder_builder));
}

TEST_CASE("BranchingSupportTree::The default base shape is the cone of today", "[suptreetree]")
{
    Slic3r::sla::SupportableMesh sm = make_supportable_mesh(make_points());
    Slic3r::sla::SupportTreeBuilder builder;
    Slic3r::sla::create_branching_tree(builder, sm);

    const Slic3r::sla::Pedestal *ped = first_pedestal(builder);

    REQUIRE(ped != nullptr);
    CHECK(ped->shape == SupportBaseShape::Cone);
    CHECK(foot_mesh(*ped) == Slic3r::sla::halfcone(ped->height, ped->r_bottom, ped->r_top,
                                                    ped->pos, steps));
}

TEST_CASE("BranchingSupportTree::A point may ask for a base shape of its own", "[suptreetree]")
{
    SupportPoints cylinder_points = make_points();
    cylinder_points[0].base_shape = SupportPoint::BaseShape::Cylinder;

    Slic3r::sla::SupportableMesh cylinder_sm =
        make_supportable_mesh(cylinder_points, SupportBaseShape::Cone);
    Slic3r::sla::SupportableMesh cone_sm =
        make_supportable_mesh(make_points(), SupportBaseShape::Cone);

    Slic3r::sla::SupportTreeBuilder cylinder_builder;
    Slic3r::sla::create_branching_tree(cylinder_builder, cylinder_sm);
    Slic3r::sla::SupportTreeBuilder cone_builder;
    Slic3r::sla::create_branching_tree(cone_builder, cone_sm);

    const Slic3r::sla::Pedestal *cylinder = first_pedestal(cylinder_builder);
    const Slic3r::sla::Pedestal *cone = first_pedestal(cone_builder);

    REQUIRE(cylinder != nullptr);
    REQUIRE(cone != nullptr);

    CHECK(cylinder->shape == SupportBaseShape::Cylinder);
    CHECK(cone->shape == SupportBaseShape::Cone);
    CHECK(foot_volume(cylinder_builder) > foot_volume(cone_builder));
}

TEST_CASE("The support_base_shape setting sits with the other base settings", "[suptreetree]")
{
    // The dropdown lives next to the base diameter and height, in the group that shows them,
    // and it starts at the cone, so a print preset that never sets it keeps today's supports.
    const ConfigItemDef *base_diameter = find_def("support_base_diameter");
    const ConfigItemDef *base_shape = find_def("support_base_shape");

    REQUIRE(base_diameter != nullptr);
    REQUIRE(base_shape != nullptr);

    CHECK(base_shape->category == ConfigItemDef::Category::Print_Supports);
    CHECK(base_shape->option_group == base_diameter->option_group);
    CHECK(base_shape->gui_type == ConfigItemDef::GUIType::combobox);
    CHECK_FALSE(base_shape->row_group.empty());
    CHECK_FALSE(base_shape->tooltip.empty());
    CHECK(base_shape->init_fn().get<SupportBaseShape>() == SupportBaseShape::Cone);

    // The branching tree reads the same setting under its own name, like the base diameter.
    CHECK(find_def("branchingsupport_base_shape") != nullptr);
}
