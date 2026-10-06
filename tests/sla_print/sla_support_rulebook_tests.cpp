// M7.8.1: the sizes of the support rulebook (R3) as the support presets and as the defaults of a
// fresh profile. A support is named by the size of its contact (T0.1 to T0.6 mm), every class
// carries the same geometry around that tip - a ball contact sunk half the tip into the model, the
// cone under it, a hexagonal stem of one diameter and the one prism base of 6 mm by 0.3 mm - and the
// values a new profile gets are the T0.2 class with that same stem and base.
//
// The presets live in the tool (App/Plater/SlaSupportPointsSettings.hpp), so the classes are taken
// from there rather than written out again: this file is what says that a button of the tool really
// builds the support the rulebook describes. The points are placed by hand on a cube standing on the
// plate, so nothing but the point and the config defaults decide the tree: no generator, no printer
// preset.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "Slic3r/App/Plater/SlaSupportPointsSettings.hpp"
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
using Slic3r::App::Plater::sla_support_contact_depth;
using Slic3r::App::Plater::sla_support_preset;
using Slic3r::App::Plater::SlaSupportPreset;
using Slic3r::Domain::ConfigItemDef;
using Slic3r::Domain::sla::SupportBaseShape;
using Slic3r::Domain::sla::SupportTipShape;
using Slic3r::Domain::SLA::SupportPoint;
using Slic3r::Domain::SLA::SupportPoints;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec3f;
namespace triangle_mesh = Slic3r::Biz::Algorithms::TriangleMesh;

namespace {

// The sizes of R3, as the tool holds them (SlaSupportPreset) and as a fresh profile holds them (the
// config definitions). One place to write them down for both halves of this file.
constexpr double class_tip_diameter[5]{0.1, 0.2, 0.3, 0.4, 0.6};
constexpr const char* class_preset_id[5]{"mini", "light", "medium", "heavy", "xheavy"};

// The stem, the base and the cone every class has (R3.2 to R3.4).
constexpr double rulebook_stem_diameter = 1.0;
constexpr double rulebook_cone_length   = 0.5;
constexpr double rulebook_base_diameter = 6.0;
constexpr double rulebook_base_height   = 0.3;
constexpr int    rulebook_stem_sides    = 6;

// The cube stands on the plate, so its bottom face is at z = 0 and a point on it has the model above
// it, which is where a contact sinks into. The point is well inside the face, not on its edge, so
// the normal of the surface under it is straight down and the head keeps the direction it is built
// with.
constexpr double cube_edge = 40.;
constexpr double elevation = 6.;
const Vec3d point_pos{8., 12., 0.};
constexpr size_t steps = 45;

const ConfigItemDef* find_def(const std::string& name)
{
    const auto& defs = Slic3r::Domain::get_defs_sla();
    for (const auto& def : defs.defs())
        if (def.name == name)
            return &def;

    return nullptr;
}

/// The default of one of the global support settings, which is what a print profile that never sets
/// the key gets.
double default_number(const std::string& name)
{
    const ConfigItemDef *def = find_def(name);
    REQUIRE(def != nullptr);
    return def->init_fn().get<double>();
}

/// The same for a setting that counts rather than measures, the sides of the stem cross-section.
int default_sides(const std::string& name)
{
    const ConfigItemDef *def = find_def(name);
    REQUIRE(def != nullptr);
    return def->init_fn().get<int>();
}

/// The tree of a fresh profile: the support settings as the config definitions ship them, read the
/// way make_support_cfg() reads them.
Slic3r::sla::SupportTreeConfig default_support_cfg()
{
    Slic3r::sla::SupportTreeConfig cfg;
    cfg.object_elevation_mm      = elevation;
    cfg.head_front_radius_mm     = 0.5 * default_number("support_head_front_diameter");
    cfg.head_back_radius_mm      = 0.5 * default_number("support_pillar_diameter");
    // The 50% of support_small_pillar_diameter_percent, the radius the algorithm may shrink a head
    // to. No rulebook class asks for a stem thinner than it.
    cfg.head_fallback_radius_mm  = 0.5 * cfg.head_back_radius_mm;
    cfg.head_penetration_mm      = default_number("support_head_penetration");
    cfg.tip_length_mm            = default_number("support_tip_length");
    cfg.base_radius_mm           = 0.5 * default_number("support_base_diameter");
    cfg.base_height_mm           = default_number("support_base_height");
    cfg.base_shape               = find_def("support_base_shape")->init_fn().get<SupportBaseShape>();
    return cfg;
}

// The cube, as a mesh that outlives every tree built from it. The AABBMesh of a SupportableMesh is a
// view on a triangle mesh and not a copy of it: it keeps the pointer, builds its AABB tree on it and
// reads its vertices and its indices for every query, so a cube built inside make_supportable_mesh()
// below is freed before the first of them and the tree then runs on released memory.
const indexed_triangle_set& cube_mesh()
{
    static const indexed_triangle_set cube =
        triangle_mesh::its_make_cube(cube_edge, cube_edge, cube_edge);

    return cube;
}

Slic3r::sla::SupportableMesh make_supportable_mesh(const SupportPoints& pts)
{
    return Slic3r::sla::SupportableMesh{
        .emesh = Slic3r::AABBMesh(cube_mesh()),
        .pts   = std::make_shared<const SupportPoints>(pts),
        .cfg   = default_support_cfg()
    };
}

/// The point a preset button of the tool writes on a point it places: the class of the preset, all of
/// it, with the contact sunk half the tip of that class (R3.1).
SupportPoint point_of_class(const SlaSupportPreset& preset)
{
    const double tip = preset.geometry.tip_diameter_mm;

    SupportPoint point;
    point.pos               = point_pos.cast<float>();
    point.head_front_radius = static_cast<float>(0.5 * tip);
    point.tip_shape         = preset.geometry.tip_shape;
    point.tip_length        = static_cast<float>(preset.geometry.tip_length_mm);
    point.contact_depth     = static_cast<float>(sla_support_contact_depth(tip));
    point.stem_sides        = static_cast<uint8_t>(preset.geometry.stem_sides);
    point.base_shape        = preset.geometry.base_shape;
    point.pillar_diameter   = static_cast<float>(preset.stem_diameter_mm);
    point.base_diameter     = static_cast<float>(preset.base_diameter_mm);
    point.base_height       = static_cast<float>(preset.base_height_mm);
    return point;
}

const Slic3r::sla::Head* head_at(const Slic3r::sla::SupportTreeBuilder& builder)
{
    for (const Slic3r::sla::Head& head : builder.heads())
        if (head.is_valid() && (head.pos - point_pos).norm() < 1e-3)
            return &head;

    return nullptr;
}

const Slic3r::sla::Pedestal* pedestal_at(const Slic3r::sla::SupportTreeBuilder& builder)
{
    for (const Slic3r::sla::Pedestal& ped : builder.pedestals())
        if (std::abs(ped.pos.x() - point_pos.x()) < 1e-3)
            return &ped;

    return nullptr;
}

// The volume enclosed by a closed mesh, from the divergence theorem. The sign depends on the winding
// of the primitive, so only the magnitude is used.
double volume(const indexed_triangle_set& mesh)
{
    double v = 0.;
    for (const auto& t : mesh.indices)
        v += mesh.vertices[t[0]].dot(mesh.vertices[t[1]].cross(mesh.vertices[t[2]]));

    return std::abs(v) / 6.;
}

// How deep the contact of a head reaches into the model: the highest vertex of its mesh, which is the
// tip of the ball, in a tree whose point sits on the surface of the model.
float contact_height(const Slic3r::sla::Head& head)
{
    float highest = -std::numeric_limits<float>::max();
    for (const auto& v : Slic3r::sla::get_mesh(head, steps).vertices)
        highest = std::max(highest, v.z());

    return highest;
}

} // namespace

TEST_CASE("The presets of the tool are the tip classes of the support rulebook", "[suptreetree]")
{
    // A support is named by the size of its contact, so the tip of a class is the class itself and
    // the classes are the five of the rulebook: 0.1, 0.2, 0.3, 0.4 and 0.6 mm, with none between
    // 0.4 and 0.6.
    for (size_t i = 0; i < 5; ++i) {
        INFO("preset " << class_preset_id[i]);
        CHECK(sla_support_preset(class_preset_id[i]).geometry.tip_diameter_mm ==
              Approx(class_tip_diameter[i]));
    }
}

TEST_CASE("A default support point builds the head and the base of the rulebook", "[suptreetree]")
{
    // A point placed by hand takes the geometry of the settings and the sizes of the global config,
    // which are the rulebook's: the T0.2 tip with a ball contact sunk half of it into the model
    // (R3.1), the short cone under it (R3.2), a hexagonal stem of one diameter (R3.3) and the one
    // prism base of 6 mm by 0.3 mm (R3.4).
    SupportPoint point;
    point.pos               = point_pos.cast<float>();
    point.head_front_radius = static_cast<float>(0.5 * default_number("support_head_front_diameter"));
    point.tip_shape         = static_cast<SupportPoint::TipShape>(find_def("support_tip_shape")->init_fn().get<SupportTipShape>()); // SupportTipShape and SupportPoint::TipShape share the same order
    point.stem_sides        = static_cast<uint8_t>(default_sides("support_stem_sides"));

    Slic3r::sla::SupportableMesh sm = make_supportable_mesh(SupportPoints{point});

    Slic3r::sla::SupportTreeBuilder builder;
    Slic3r::sla::create_default_tree(builder, sm);

    const Slic3r::sla::Head *head = head_at(builder);
    const Slic3r::sla::Pedestal *pedestal = pedestal_at(builder);

    REQUIRE(head != nullptr);
    REQUIRE(pedestal != nullptr);

    // The tip is a ball (R3.1) and it reaches half of its own diameter into the model.
    CHECK(head->tip_shape == SupportPoint::TipShape::Ball);
    CHECK(head->r_pin_mm == Approx(0.1));
    CHECK(head->penetration_mm == Approx(0.1));
    CHECK(contact_height(*head) == Approx(0.1f));

    // The cone under the contact is short (R3.2), so the break point is right under the ball.
    CHECK(head->width_mm == Approx(rulebook_cone_length));

    // The stem is one diameter of R3.3 and a hexagon.
    CHECK(head->r_back_mm == Approx(0.5));
    REQUIRE(head->pillar_id >= 0);
    const Slic3r::sla::Pillar& pillar = builder.pillar(head->pillar_id);
    CHECK(pillar.r_start == Approx(0.5));
    CHECK(pillar.r_end == Approx(0.5));
    CHECK(pillar.stem.sides == rulebook_stem_sides);

    // One base for everything (R3.4): 6 mm across, 0.3 mm high, a prism of the stem's own diameter.
    CHECK(pedestal->r_bottom == Approx(3.0));
    CHECK(pedestal->height == Approx(0.3));
    CHECK(pedestal->r_top == Approx(0.5));
    CHECK(pedestal->shape == SupportBaseShape::Cylinder);
    CHECK(volume(Slic3r::sla::get_mesh(*pedestal, steps)) ==
          Approx(volume(Slic3r::sla::cylinder(3.0, 0.3, steps))));
}

TEST_CASE("BranchingSupportTree::A default support point builds the rulebook head and base",
          "[suptreetree]")
{
    // The other tree reads the same global config and the same point, so it builds the same foot: one
    // base of 6 mm by 0.3 mm (R3.4) under a stem of one diameter (R3.3).
    SupportPoint point;
    point.pos               = point_pos.cast<float>();
    point.head_front_radius = static_cast<float>(0.5 * default_number("support_head_front_diameter"));
    point.stem_sides        = static_cast<uint8_t>(default_sides("support_stem_sides"));

    Slic3r::sla::SupportableMesh sm = make_supportable_mesh(SupportPoints{point});

    Slic3r::sla::SupportTreeBuilder builder;
    Slic3r::sla::create_branching_tree(builder, sm);

    const Slic3r::sla::Head *head = head_at(builder);
    const Slic3r::sla::Pedestal *pedestal = pedestal_at(builder);

    REQUIRE(head != nullptr);
    REQUIRE(pedestal != nullptr);

    CHECK(head->r_pin_mm == Approx(0.1));
    CHECK(head->penetration_mm == Approx(0.1));
    CHECK(head->r_back_mm == Approx(0.5));
    CHECK(head->width_mm == Approx(rulebook_cone_length));

    CHECK(pedestal->r_bottom == Approx(3.0));
    CHECK(pedestal->height == Approx(0.3));
    CHECK(pedestal->shape == SupportBaseShape::Cylinder);
}

TEST_CASE("Every tip class builds its own rulebook support", "[suptreetree]")
{
    // One button of the tool, one class of the rulebook: the tip is the class, the contact is a ball
    // sunk half the tip into the model (R3.1) and everything below it is the same support: the cone
    // under the contact (R3.2), a hexagonal stem of 1.0 mm (R3.3) and a 6 mm by 0.3 mm prism base
    // (R3.4).
    for (size_t i = 0; i < 5; ++i) {
        INFO("class " << class_preset_id[i]);
        const SlaSupportPreset preset = sla_support_preset(class_preset_id[i]);
        const double tip             = class_tip_diameter[i];

        Slic3r::sla::SupportableMesh sm = make_supportable_mesh(SupportPoints{point_of_class(preset)});

        Slic3r::sla::SupportTreeBuilder builder;
        Slic3r::sla::create_default_tree(builder, sm);

        const Slic3r::sla::Head *head = head_at(builder);
        const Slic3r::sla::Pedestal *pedestal = pedestal_at(builder);

        REQUIRE(head != nullptr);
        REQUIRE(pedestal != nullptr);

        CHECK(head->r_pin_mm == Approx(0.5 * tip));
        CHECK(head->tip_shape == SupportPoint::TipShape::Ball);
        CHECK(head->penetration_mm == Approx(0.5 * tip));
        CHECK(contact_height(*head) == Approx(static_cast<float>(0.5 * tip)));

        CHECK(head->width_mm == Approx(rulebook_cone_length));
        CHECK(head->r_back_mm == Approx(0.5 * rulebook_stem_diameter));
        REQUIRE(head->pillar_id >= 0);
        const Slic3r::sla::Pillar& pillar = builder.pillar(head->pillar_id);
        CHECK(pillar.r_start == Approx(0.5 * rulebook_stem_diameter));
        CHECK(pillar.r_end == Approx(0.5 * rulebook_stem_diameter));
        CHECK(pillar.stem.sides == rulebook_stem_sides);

        CHECK(pedestal->r_bottom == Approx(0.5 * rulebook_base_diameter));
        CHECK(pedestal->height == Approx(rulebook_base_height));
        CHECK(pedestal->shape == SupportBaseShape::Cylinder);
        CHECK(volume(Slic3r::sla::get_mesh(*pedestal, steps)) ==
              Approx(volume(Slic3r::sla::cylinder(0.5 * rulebook_base_diameter,
                                                 rulebook_base_height, steps))));
    }
}

TEST_CASE("The global support defaults are the T0.2 class of the rulebook", "[suptreetree]")
{
    // What a fresh profile gets, read off the config definitions the way the settings page does:
    // the T0.2 tip, sunk half of it into the model (R3.1), and the stem and base of R3.3 and R3.4.
    CHECK(default_number("support_head_front_diameter") == Approx(0.2));
    CHECK(default_number("support_head_penetration") == Approx(0.1));
    CHECK(default_number("support_pillar_diameter") == Approx(rulebook_stem_diameter));
    CHECK(default_number("support_base_diameter") == Approx(rulebook_base_diameter));
    CHECK(default_number("support_base_height") == Approx(rulebook_base_height));
    CHECK(default_number("support_tip_length") == Approx(rulebook_cone_length));
    CHECK(find_def("support_tip_shape")->init_fn().get<SupportTipShape>() == SupportTipShape::Ball);
    CHECK(find_def("support_base_shape")->init_fn().get<SupportBaseShape>() ==
          SupportBaseShape::Cylinder);
    CHECK(default_sides("support_stem_sides") == rulebook_stem_sides);

    // The presets of the tool are the classes, with the same stem and base behind every tip.
    for (size_t i = 0; i < 5; ++i) {
        INFO("class " << class_preset_id[i]);
        const SlaSupportPreset preset = sla_support_preset(class_preset_id[i]);
        CHECK(preset.stem_diameter_mm == Approx(rulebook_stem_diameter));
        CHECK(preset.base_diameter_mm == Approx(rulebook_base_diameter));
        CHECK(preset.base_height_mm == Approx(rulebook_base_height));
        CHECK(preset.geometry.stem_sides == rulebook_stem_sides);
        CHECK(preset.geometry.base_shape == SupportPoint::BaseShape::Cylinder);
    }
}
