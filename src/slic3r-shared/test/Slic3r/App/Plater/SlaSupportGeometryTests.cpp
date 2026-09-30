// M2.16c, M2.24, M2.23b: the per-point support geometry. The tip diameter, the tip shape, the tip
// length, the knot ball between tip and stem, the stem cross-section, the stem taper and the shape of
// the foot are stored on every support point (SLA::SupportPoint, M2.13, M2.16 and M2.23); this
// covers the Supports & raft settings a new point starts from and the support tool controls that
// write them on the points that are selected. M2.24 adds the tip diameter, which used to be
// reachable only through the head diameter control, and the tip length, which had no setting and
// no control at all. M2.23b adds the foot shape, which M2.23 stored without a control. No mesh
// builder is involved here.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Slic3r/App/Plater/SlaSupportGeometry.hpp"
#include "Slic3r/App/Plater/SlaSupportPointsEditing.hpp"
#include "Slic3r/App/Plater/SlaSupportPreviewService.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"

using Catch::Approx;
using Slic3r::App::Plater::SlaSupportGeometry;
using Slic3r::App::Plater::SlaSupportPointsEditing;
using Slic3r::App::Plater::SupportGeometryField;
using Slic3r::App::Plater::apply_support_geometry;
using Slic3r::App::Plater::config_support_tip_shape_of;
using Slic3r::App::Plater::hash_support_points;
using Slic3r::App::Plater::selection_support_geometry;
using Slic3r::App::Plater::support_base_shape_of;
using Slic3r::App::Plater::support_geometry_of;
using Slic3r::App::Plater::support_tip_shape_of;
using Slic3r::Domain::ConfigItemDef;
using Slic3r::Domain::SLA::SupportPoint;
using Slic3r::Domain::SLA::SupportPoints;
using Slic3r::Domain::sla::SupportBaseShape;
using Slic3r::Domain::sla::SupportTipShape;

namespace {

SupportPoint make_point()
{
    SupportPoint point;
    point.pos               = Slic3r::Domain::Vec3f{0.f, 0.f, 10.f};
    point.head_front_radius = 0.2f;
    point.type              = Slic3r::Domain::SLA::SupportPointType::manual_add;
    return point;
}

// A point that asks for a foot of its own, which the "Foot shape" control of the support tool
// writes on a selection (M2.23b).
SupportPoint make_point_with_foot(SupportPoint::BaseShape shape)
{
    SupportPoint point = make_point();
    point.base_shape   = shape;
    return point;
}

const ConfigItemDef* find_def(const std::string& name)
{
    const auto& defs = Slic3r::Domain::get_defs_sla();
    for (const auto& def : defs.defs()) {
        if (def.name == name)
            return &def;
    }
    return nullptr;
}

} // namespace

TEST_CASE("The per-point support geometry settings sit in the head and the pillar group",
          "[Config][SLA][Supports]")
{
    const ConfigItemDef* head_front = find_def("support_head_front_diameter");
    const ConfigItemDef* pillar     = find_def("support_pillar_diameter");
    REQUIRE(head_front != nullptr);
    REQUIRE(pillar != nullptr);

    // The tip diameter, the tip shape, the tip length and the knot belong next to the pinhead sizes,
    // the stem cross-section and the taper next to the pillar diameter, so the "Supports & raft"
    // page keeps the tip and the stem apart the way the rest of the settings do.
    for (const std::string& key :
         {"support_tip_shape", "support_knot_diameter", "support_tip_length"}) {
        INFO("key " << key);
        const ConfigItemDef* def = find_def(key);
        REQUIRE(def != nullptr);
        CHECK(def->category == ConfigItemDef::Category::Print_Supports);
        CHECK(def->option_group == head_front->option_group);
        CHECK_FALSE(def->row_group.empty());
        CHECK_FALSE(def->tooltip.empty());
    }

    for (const std::string& key : {"support_stem_sides", "support_stem_taper"}) {
        INFO("key " << key);
        const ConfigItemDef* def = find_def(key);
        REQUIRE(def != nullptr);
        CHECK(def->category == ConfigItemDef::Category::Print_Supports);
        CHECK(def->option_group == pillar->option_group);
        CHECK_FALSE(def->row_group.empty());
        CHECK_FALSE(def->tooltip.empty());
    }

    const ConfigItemDef* tip_shape = find_def("support_tip_shape");
    REQUIRE(tip_shape != nullptr);
    CHECK(tip_shape->gui_type == ConfigItemDef::GUIType::combobox);
}

TEST_CASE("The per-point support geometry defaults are the geometry of today", "[Config][SLA][Supports]")
{
    // The configured tip diameter, Default, a tip length derived from the pinhead width, no knot and
    // a round stem of one diameter: what the support tree has always built, so a print preset that
    // never sets the keys keeps today's supports.
    CHECK(find_def("support_tip_shape")->init_fn().get<SupportTipShape>() == SupportTipShape::Default);
    CHECK(find_def("support_tip_length")->init_fn().get<double>() == Approx(0.));
    CHECK(find_def("support_knot_diameter")->init_fn().get<double>() == Approx(0.));
    CHECK(find_def("support_stem_sides")->init_fn().get<int>() == 0);
    CHECK(find_def("support_stem_taper")->init_fn().get<double>() == Approx(0.));
}

TEST_CASE("A support point carries the support geometry it is given", "[SlaSupportGeometry]")
{
    SlaSupportGeometry geometry;
    geometry.tip_diameter_mm  = 1.2;
    geometry.tip_shape        = SupportPoint::TipShape::Cone;
    geometry.tip_length_mm    = 0.8;
    geometry.knot_diameter_mm = 1.6;
    geometry.stem_sides       = 6;
    geometry.stem_taper       = 0.25;
    geometry.base_shape       = SupportPoint::BaseShape::Flat;

    SupportPoint point = make_point();
    apply_support_geometry(point, geometry);
    // The all-fields write leaves the head radius alone: the generator fills it from the tree type,
    // and a point placed by hand takes the tip diameter of the tool (M2.24).
    apply_support_geometry(point, geometry, SupportGeometryField::TipDiameter);

    // The tool works in diameters, the point stores the radii of the tip and of the knot.
    CHECK(point.head_front_radius == Approx(0.6f));
    CHECK(point.tip_shape == SupportPoint::TipShape::Cone);
    CHECK(point.tip_length == Approx(0.8f));
    CHECK(point.knot_radius == Approx(0.8f));
    CHECK(point.stem_sides == 6);
    CHECK(point.stem_taper == Approx(0.25f));
    CHECK(point.base_shape == SupportPoint::BaseShape::Flat);

    // What the point carries is what the tool reads back.
    const SlaSupportGeometry read_back = support_geometry_of(point);
    CHECK(read_back.tip_diameter_mm == Approx(1.2));
    CHECK(read_back.tip_shape == geometry.tip_shape);
    CHECK(read_back.tip_length_mm == Approx(geometry.tip_length_mm));
    CHECK(read_back.knot_diameter_mm == Approx(1.6));
    CHECK(read_back.stem_sides == geometry.stem_sides);
    CHECK(read_back.stem_taper == Approx(geometry.stem_taper));
    CHECK(read_back.base_shape == geometry.base_shape);
}

TEST_CASE("The support geometry of a selection is only shown when the points agree", "[SlaSupportGeometry]")
{
    SupportPoints points{make_point(), make_point(), make_point()};
    points[1].tip_shape   = SupportPoint::TipShape::Ball;
    points[2].knot_radius = 0.5f;

    SECTION("nothing selected shows no value")
    {
        CHECK_FALSE(selection_support_geometry(points, {}).has_value());
    }

    SECTION("one point shows its own geometry")
    {
        const std::optional<SlaSupportGeometry> shown = selection_support_geometry(points, {1});
        REQUIRE(shown.has_value());
        CHECK(shown->tip_shape == SupportPoint::TipShape::Ball);
    }

    SECTION("points that disagree show no value at all")
    {
        CHECK_FALSE(selection_support_geometry(points, {0, 1}).has_value());
        CHECK_FALSE(selection_support_geometry(points, {0, 1, 2}).has_value());
    }

    SECTION("points that agree show the value they share")
    {
        SupportPoints agreeing{make_point(), make_point()};
        agreeing[1].stem_sides = 6;
        const std::optional<SlaSupportGeometry> shared = selection_support_geometry(agreeing, {0, 1});
        REQUIRE(shared.has_value());
        CHECK(shared->stem_sides == 6);
    }

    SECTION("a tip diameter of its own is a disagreement too")
    {
        // The tip diameter is the head diameter control of the tool, and two points of a different
        // size leave it blank rather than showing a diameter only some of them have (M2.24).
        SupportPoints sized{make_point(), make_point()};
        sized[1].head_front_radius = 0.5f;
        CHECK_FALSE(selection_support_geometry(sized, {0, 1}).has_value());
        const std::optional<SlaSupportGeometry> one = selection_support_geometry(sized, {1});
        REQUIRE(one.has_value());
        CHECK(one->tip_diameter_mm == Approx(1.0));
    }

    SECTION("a tip length of its own is a disagreement too")
    {
        SupportPoints lengths{make_point(), make_point()};
        lengths[1].tip_length = 1.4f;
        CHECK_FALSE(selection_support_geometry(lengths, {0, 1}).has_value());
    }

    SECTION("a foot shape of its own is a disagreement too")
    {
        // The foot shape is one of the per-point geometry fields (M2.23b), so points that ask for
        // different feet leave the whole set blank, exactly like the other fields.
        SupportPoints feet{make_point(), make_point()};
        feet[1].base_shape = SupportPoint::BaseShape::Cylinder;
        CHECK_FALSE(selection_support_geometry(feet, {0, 1}).has_value());
        const std::optional<SlaSupportGeometry> one = selection_support_geometry(feet, {1});
        REQUIRE(one.has_value());
        CHECK(one->base_shape == SupportPoint::BaseShape::Cylinder);
    }

    SECTION("points that agree on the foot shape show it")
    {
        SupportPoints feet{make_point_with_foot(SupportPoint::BaseShape::Flat),
                            make_point_with_foot(SupportPoint::BaseShape::Flat)};
        const std::optional<SlaSupportGeometry> shared = selection_support_geometry(feet, {0, 1});
        REQUIRE(shared.has_value());
        CHECK(shared->base_shape == SupportPoint::BaseShape::Flat);
    }
}

TEST_CASE("The support geometry goes on the selected points only", "[SlaSupportGeometry]")
{
    SlaSupportPointsEditing editor;
    editor.points.push_back(make_point());
    editor.points.push_back(make_point());
    editor.points.push_back(make_point());
    editor.select_point(0);
    editor.select_point(2, true);

    // The point that is not selected, with a geometry of its own, has to keep it.
    editor.points[1].head_front_radius = 0.5f;
    editor.points[1].tip_shape         = SupportPoint::TipShape::Cone;
    editor.points[1].tip_length        = 1.4f;
    editor.points[1].stem_sides        = 4;
    editor.points[1].knot_radius       = 0.5f;
    editor.points[1].stem_taper        = 0.5f;
    editor.points[1].base_shape        = SupportPoint::BaseShape::Default;

    editor.support_geometry.tip_diameter_mm  = 1.2;
    editor.support_geometry.tip_shape        = SupportPoint::TipShape::Ball;
    editor.support_geometry.tip_length_mm    = 0.6;
    editor.support_geometry.knot_diameter_mm = 2.0;
    editor.support_geometry.stem_sides       = 6;
    editor.support_geometry.stem_taper       = 0.25;
    editor.support_geometry.base_shape       = SupportPoint::BaseShape::Cylinder;

    for (const auto& [field, is_set] : std::vector<std::pair<SupportGeometryField, std::function<bool(const SupportPoint&)>>>{
             {SupportGeometryField::TipDiameter,
              [](const SupportPoint& p) { return p.head_front_radius == Approx(0.6f); }},
             {SupportGeometryField::TipShape,
              [](const SupportPoint& p) { return p.tip_shape == SupportPoint::TipShape::Ball; }},
             {SupportGeometryField::TipLength,
              [](const SupportPoint& p) { return p.tip_length == Approx(0.6f); }},
             {SupportGeometryField::KnotDiameter,
              [](const SupportPoint& p) { return p.knot_radius == Approx(1.0f); }},
             {SupportGeometryField::StemSides,
              [](const SupportPoint& p) { return p.stem_sides == 6; }},
             {SupportGeometryField::StemTaper,
              [](const SupportPoint& p) { return p.stem_taper == Approx(0.25f); }},
             {SupportGeometryField::BaseShape,
              [](const SupportPoint& p) { return p.base_shape == SupportPoint::BaseShape::Cylinder; }},
         }) {
        INFO("field " << static_cast<int>(field));
        editor.apply_support_geometry_to_selected(field);
        for (const size_t idx : {size_t(0), size_t(2)}) {
            CHECK(is_set(editor.points[idx]));
        }
    }

    CHECK(editor.points[1].head_front_radius == Approx(0.5f));
    CHECK(editor.points[1].tip_shape == SupportPoint::TipShape::Cone);
    CHECK(editor.points[1].tip_length == Approx(1.4f));
    CHECK(editor.points[1].knot_radius == Approx(0.5f));
    CHECK(editor.points[1].stem_sides == 4);
    CHECK(editor.points[1].stem_taper == Approx(0.5f));
    CHECK(editor.points[1].base_shape == SupportPoint::BaseShape::Default);
}

TEST_CASE("One support geometry field at a time leaves the other six alone", "[SlaSupportGeometry]")
{
    SlaSupportPointsEditing editor;
    editor.points.push_back(make_point());
    editor.points[0].head_front_radius = 0.5f;
    editor.points[0].tip_shape         = SupportPoint::TipShape::Cone;
    editor.points[0].tip_length        = 1.4f;
    editor.points[0].stem_sides        = 4;
    editor.points[0].knot_radius       = 0.5f;
    editor.points[0].stem_taper        = 0.5f;
    editor.points[0].base_shape        = SupportPoint::BaseShape::Flat;
    editor.select_point(0);

    editor.support_geometry.tip_diameter_mm  = 1.2;
    editor.support_geometry.tip_shape        = SupportPoint::TipShape::Ball;
    editor.support_geometry.tip_length_mm    = 0.6;
    editor.support_geometry.knot_diameter_mm = 2.0;
    editor.support_geometry.stem_sides       = 6;
    editor.support_geometry.stem_taper       = 0.25;
    editor.support_geometry.base_shape       = SupportPoint::BaseShape::Cylinder;

    editor.apply_support_geometry_to_selected(SupportGeometryField::BaseShape);

    CHECK(editor.points[0].base_shape == SupportPoint::BaseShape::Cylinder);
    CHECK(editor.points[0].head_front_radius == Approx(0.5f));
    CHECK(editor.points[0].tip_shape == SupportPoint::TipShape::Cone);
    CHECK(editor.points[0].tip_length == Approx(1.4f));
    CHECK(editor.points[0].knot_radius == Approx(0.5f));
    CHECK(editor.points[0].stem_sides == 4);
    CHECK(editor.points[0].stem_taper == Approx(0.5f));
}

TEST_CASE("A point placed by hand takes the support geometry of the settings", "[SlaSupportGeometry]")
{
    SlaSupportPointsEditing editor;
    editor.support_geometry.tip_diameter_mm  = 1.2;
    editor.support_geometry.tip_shape        = SupportPoint::TipShape::Cone;
    editor.support_geometry.tip_length_mm    = 0.6;
    editor.support_geometry.knot_diameter_mm = 1.0;
    editor.support_geometry.stem_sides       = 4;
    editor.support_geometry.stem_taper       = 0.5;
    editor.support_geometry.base_shape       = SupportPoint::BaseShape::Flat;

    editor.add_point(Slic3r::Domain::Vec3d{1., 2., 3.});

    REQUIRE(editor.points.size() == 1);
    CHECK(editor.points[0].head_front_radius == Approx(0.6f));
    CHECK(editor.points[0].tip_shape == SupportPoint::TipShape::Cone);
    CHECK(editor.points[0].tip_length == Approx(0.6f));
    CHECK(editor.points[0].knot_radius == Approx(0.5f));
    CHECK(editor.points[0].stem_sides == 4);
    CHECK(editor.points[0].stem_taper == Approx(0.5f));
    // The foot of a new point is the shape support_base_shape asks for (M2.23b).
    CHECK(editor.points[0].base_shape == SupportPoint::BaseShape::Flat);
}

TEST_CASE("A point placed by hand takes the tip diameter of the tool", "[SlaSupportGeometry]")
{
    // The head diameter control is the tip diameter field of the support geometry (M2.24), so a
    // point placed after a preset takes the tip of that preset.
    SlaSupportPointsEditing editor;
    editor.add_point(Slic3r::Domain::Vec3d{0., 0., 0.});
    editor.support_geometry.tip_diameter_mm = 0.2;
    editor.add_point(Slic3r::Domain::Vec3d{1., 0., 0.});

    REQUIRE(editor.points.size() == 2);
    CHECK(editor.points[0].head_front_radius == Approx(0.2f));
    CHECK(editor.points[1].head_front_radius == Approx(0.1f));
}

TEST_CASE("The three tip shapes map both ways", "[SlaSupportGeometry]")
{
    const std::array<std::pair<SupportTipShape, SupportPoint::TipShape>, 3> shapes{{
        {SupportTipShape::Default, SupportPoint::TipShape::Default},
        {SupportTipShape::Cone, SupportPoint::TipShape::Cone},
        {SupportTipShape::Ball, SupportPoint::TipShape::Ball},
    }};

    for (const auto& [config, point] : shapes) {
        INFO("tip shape " << static_cast<int>(config));
        CHECK(support_tip_shape_of(config) == point);
        CHECK(config_support_tip_shape_of(point) == config);
    }
}

TEST_CASE("The three foot shapes map onto the point, and a point can follow the setting",
          "[SlaSupportGeometry]")
{
    // The key names a real foot for every one of its values, so a new point takes one of the three
    // rather than the cone of before. Default is the point's own way of saying "whatever
    // support_base_shape says" (M2.23, M2.23b).
    const std::array<std::pair<SupportBaseShape, SupportPoint::BaseShape>, 3> shapes{{
        {SupportBaseShape::Cone, SupportPoint::BaseShape::Cone},
        {SupportBaseShape::Cylinder, SupportPoint::BaseShape::Cylinder},
        {SupportBaseShape::Flat, SupportPoint::BaseShape::Flat},
    }};

    for (const auto& [config, point] : shapes) {
        INFO("foot shape " << static_cast<int>(config));
        CHECK(support_base_shape_of(config) == point);
    }

    // A point without a shape of its own, which is every point of a project made before the
    // per-point foot shape (M2.23).
    SupportPoint following = make_point();
    CHECK(following.base_shape == SupportPoint::BaseShape::Default);
}

TEST_CASE("A change of the per-point support geometry refreshes the live preview",
          "[SlaSupportGeometry][SlaSupportPreview]")
{
    const std::uint64_t base = hash_support_points(SupportPoints{make_point()});

    SupportPoint tip_dia     = make_point();
    tip_dia.head_front_radius = 0.5f;
    SupportPoint tip         = make_point();
    tip.tip_shape            = SupportPoint::TipShape::Cone;
    SupportPoint tip_len     = make_point();
    tip_len.tip_length       = 1.4f;
    SupportPoint knot        = make_point();
    knot.knot_radius         = 0.5f;
    SupportPoint sides       = make_point();
    sides.stem_sides         = 6;
    SupportPoint taper       = make_point();
    taper.stem_taper         = 0.5f;
    SupportPoint foot        = make_point();
    foot.base_shape          = SupportPoint::BaseShape::Cylinder;
    const std::array<SupportPoint, 7> changed{tip_dia, tip, tip_len, knot, sides, taper, foot};

    // The tree of a point whose tip diameter, tip shape, tip length, knot, cross-section, taper or
    // foot changed is another tree, so the key of the preview has to change with it (M2.16b and
    // M2.23 build the geometry, M2.13, M2.16c and M2.23 write the values, this job the key).
    for (const SupportPoint& point : changed) {
        INFO("point changed");
        CHECK(hash_support_points(SupportPoints{point}) != base);
    }
}
