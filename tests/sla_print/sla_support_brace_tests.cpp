// M2.15: bracing controls. The default tree links neighbouring pillars with braces
// (zig-zag, cross or dynamic, see support_pillar_connection_mode), and three settings
// decide whether and how thick those braces are: support_brace_enable,
// support_brace_diameter and support_brace_start_height.
//
// The points here are placed by hand on the bottom face of a cube standing on the plate,
// so the two pillars are the only ones in the tree and every brace in it is a brace
// between them. Their gap is 8 mm, over the 4 mm the connection mode needs for the
// crossing braces and under the 10 mm the pillars may be linked at, and the 20 mm
// elevation makes the pillars tall enough for the zig-zag to fit between them.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cstddef>
#include <memory>
#include <string>

#include "Slic3r/Biz/Algorithms/AABBMesh.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "libslic3r/SLA/DefaultSupportTree.hpp"
#include "libslic3r/SLA/SupportTree.hpp"
#include "libslic3r/SLA/SupportTreeBuilder.hpp"

using Catch::Approx;
namespace triangle_mesh = Slic3r::Biz::Algorithms::TriangleMesh;
using Slic3r::Domain::Vec3f;
using Slic3r::Domain::SLA::SupportPoints;

namespace {

// The cube stands on the plate, so its bottom face is at z = 0.
constexpr double cube_edge = 40.;

// Tall enough for a 45 degree zig-zag with 8 mm steps between the pillar tops and the
// pillar bases.
constexpr double elevation = 20.;

// The gap the two pillars are linked at: over 2 * base_radius (4 mm) so the dynamic
// connection mode adds the crossing braces, and under max_pillar_link_distance (10 mm).
constexpr double pillar_gap = 8.;

SupportPoints braceable_points()
{
    SupportPoints pts(2);
    pts[0].pos = Vec3f{16.f, 20.f, 0.f};
    pts[1].pos = Vec3f{float(16. + pillar_gap), 20.f, 0.f};
    return pts;
}

Slic3r::sla::SupportableMesh make_supportable_mesh()
{
    Slic3r::sla::SupportTreeConfig cfg;
    cfg.object_elevation_mm = elevation;

    return Slic3r::sla::SupportableMesh{
        .emesh = Slic3r::AABBMesh(triangle_mesh::its_make_cube(cube_edge, cube_edge, cube_edge)),
        .pts   = std::make_shared<const SupportPoints>(braceable_points()),
        .cfg   = cfg
    };
}

// What the default tree made of the two pillars.
struct Bracing
{
    size_t pillars       = 0;
    size_t braces        = 0; // braces between two pillars
    size_t stubs         = 0; // pinhead to pillar links
    size_t links         = 0; // pillar to pillar links
    double radius        = 0.; // radius of the braces, they are all the same
    double pillar_radius = 0.; // radius of the pillars, they are all the same
};

Bracing build_bracing(const Slic3r::sla::SupportableMesh& sm)
{
    Slic3r::sla::SupportTreeBuilder builder;
    Slic3r::sla::create_default_tree(builder, sm);

    Bracing ret;
    ret.pillars = builder.pillarcount();
    ret.braces  = builder.crossbridges().size();
    ret.stubs   = builder.bridges().size();
    for (const Slic3r::sla::Pillar& pillar : builder.pillars()) {
        ret.links += pillar.links;
        ret.pillar_radius = pillar.r_start;
    }
    for (const Slic3r::sla::Bridge& brace : builder.crossbridges())
        ret.radius = brace.r;

    return ret;
}

} // namespace

TEST_CASE("DefaultSupportTree::braces the neighbouring pillars by default", "[suptreetree]")
{
    const Bracing bracing = build_bracing(make_supportable_mesh());

    // Both points reach the ground, so there is a pillar for each and no pinhead has to
    // lean on a neighbouring pillar.
    CHECK(bracing.pillars == 2);
    CHECK(bracing.stubs == 0);

    // The two pillars are braced: the zig-zag and, in dynamic mode, the crossings.
    CHECK_FALSE(bracing.braces == 0);

    // Without a brace diameter of its own a brace is as thick as the pillar it hangs on.
    CHECK(bracing.radius == Approx(Slic3r::sla::SupportTreeConfig{}.head_back_radius_mm));
}

TEST_CASE("DefaultSupportTree::bracing off leaves the pillars on their own", "[suptreetree]")
{
    Slic3r::sla::SupportableMesh sm = make_supportable_mesh();
    sm.cfg.brace_enable             = false;

    const Bracing bracing = build_bracing(sm);

    // The pillars are still there, they only do not lean on each other any more.
    CHECK(bracing.pillars == 2);
    CHECK(bracing.braces == 0);
    CHECK(bracing.stubs == 0);
    CHECK(bracing.links == 0);
}

TEST_CASE("DefaultSupportTree::brace start height raises the bracing", "[suptreetree]")
{
    const size_t braces_by_default = build_bracing(make_supportable_mesh()).braces;
    REQUIRE_FALSE(braces_by_default == 0);

    // Bracing above the pillar bases: fewer braces fit between the pillar tops and the
    // height the braces may not go below.
    Slic3r::sla::SupportableMesh raised = make_supportable_mesh();
    raised.cfg.brace_start_height_mm    = 5.;
    const size_t braces_raised          = build_bracing(raised).braces;

    CHECK(braces_raised < braces_by_default);
    CHECK_FALSE(braces_raised == 0);

    // Above the pillar tops there is nothing left to brace.
    Slic3r::sla::SupportableMesh high = make_supportable_mesh();
    high.cfg.brace_start_height_mm    = elevation;

    CHECK(build_bracing(high).braces == 0);
}

TEST_CASE("DefaultSupportTree::brace diameter is independent of the pillar", "[suptreetree]")
{
    constexpr double brace_diameter = 0.4;

    Slic3r::sla::SupportableMesh sm = make_supportable_mesh();
    sm.cfg.brace_diameter_mm        = brace_diameter;

    const Bracing bracing = build_bracing(sm);

    // The braces are thinner than the pillars...
    CHECK_FALSE(bracing.braces == 0);
    CHECK(bracing.radius == Approx(0.5 * brace_diameter));

    // ... and the pillars kept the radius of the configured pillar diameter.
    CHECK(bracing.pillar_radius == Approx(sm.cfg.head_back_radius_mm));
}

TEST_CASE("The brace settings are shown with the pillar connection mode", "[suptreetree]")
{
    const auto& defs = Slic3r::Domain::get_defs_sla();

    const auto find_def = [&defs](const std::string& name) -> const Slic3r::Domain::ConfigItemDef*
    {
        for (const auto& def : defs.defs())
            if (def.name == name)
                return &def;

        return nullptr;
    };

    const Slic3r::Domain::ConfigItemDef* connection_mode =
        find_def("support_pillar_connection_mode");
    REQUIRE(connection_mode != nullptr);

    // The rows of the same group of the "Supports & raft" page, so the brace settings sit
    // next to the connection mode they complete.
    for (const std::string& key :
         {"support_brace_enable", "support_brace_diameter", "support_brace_start_height"})
    {
        INFO("Key: " << key);
        const Slic3r::Domain::ConfigItemDef* def = find_def(key);
        REQUIRE(def != nullptr);
        CHECK(def->category == connection_mode->category);
        CHECK(def->option_group == connection_mode->option_group);
        CHECK_FALSE(def->row_group.empty());
        CHECK_FALSE(def->tooltip.empty());
    }

    const Slic3r::Domain::ConfigItemDef* enable = find_def("support_brace_enable");
    REQUIRE(enable != nullptr);
    CHECK(enable->gui_type == Slic3r::Domain::ConfigItemDef::GUIType::checkbox);

    // The defaults are the geometry the tree has always built: bracing on, braces as
    // thick as their pillar, reaching down to the pillar bases.
    CHECK(enable->init_fn().get<bool>());
    CHECK(find_def("support_brace_diameter")->init_fn().get<double>() == Approx(0.));
    CHECK(find_def("support_brace_start_height")->init_fn().get<double>() == Approx(0.));
}
