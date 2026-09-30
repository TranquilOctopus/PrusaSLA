// M2.16c: the per-point support geometry. The tip shape, the knot ball between tip and stem, the
// stem cross-section and the stem taper are stored on every support point (SLA::SupportPoint, M2.13
// and M2.16); this todo adds the four Supports & raft settings a new point starts from and the
// support tool controls that write them on the points that are selected. No mesh builder reads them
// yet (M2.16b), so nothing here is about the geometry that comes out.
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
using Slic3r::App::Plater::support_geometry_of;
using Slic3r::App::Plater::support_tip_shape_of;
using Slic3r::Domain::ConfigItemDef;
using Slic3r::Domain::SLA::SupportPoint;
using Slic3r::Domain::SLA::SupportPoints;
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

    // The tip shape and the knot belong next to the pinhead sizes, the stem cross-section and the
    // taper next to the pillar diameter, so the "Supports & raft" page keeps the tip and the stem
    // apart the way the rest of the settings do.
    for (const std::string& key : {"support_tip_shape", "support_knot_diameter"}) {
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
    // Default, no knot, a round stem of one diameter: what the support tree has always built, so a
    // print preset that never sets the four keys keeps today's supports.
    CHECK(find_def("support_tip_shape")->init_fn().get<SupportTipShape>() == SupportTipShape::Default);
    CHECK(find_def("support_knot_diameter")->init_fn().get<double>() == Approx(0.));
    CHECK(find_def("support_stem_sides")->init_fn().get<int>() == 0);
    CHECK(find_def("support_stem_taper")->init_fn().get<double>() == Approx(0.));
}

TEST_CASE("A support point carries the support geometry it is given", "[SlaSupportGeometry]")
{
    SlaSupportGeometry geometry;
    geometry.tip_shape        = SupportPoint::TipShape::Cone;
    geometry.knot_diameter_mm = 1.6;
    geometry.stem_sides       = 6;
    geometry.stem_taper       = 0.25;

    SupportPoint point = make_point();
    apply_support_geometry(point, geometry);

    CHECK(point.tip_shape == SupportPoint::TipShape::Cone);
    // The tool works in diameters, the point stores the radius of the knot.
    CHECK(point.knot_radius == Approx(0.8f));
    CHECK(point.stem_sides == 6);
    CHECK(point.stem_taper == Approx(0.25f));

    // What the point carries is what the tool reads back.
    const SlaSupportGeometry read_back = support_geometry_of(point);
    CHECK(read_back.knot_diameter_mm == Approx(1.6));
    CHECK(read_back.tip_shape == geometry.tip_shape);
    CHECK(read_back.stem_sides == geometry.stem_sides);
    CHECK(read_back.stem_taper == Approx(geometry.stem_taper));
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
    editor.points[1].tip_shape   = SupportPoint::TipShape::Cone;
    editor.points[1].stem_sides  = 4;
    editor.points[1].knot_radius = 0.5f;
    editor.points[1].stem_taper  = 0.5f;

    editor.support_geometry.tip_shape        = SupportPoint::TipShape::Ball;
    editor.support_geometry.knot_diameter_mm = 2.0;
    editor.support_geometry.stem_sides       = 6;
    editor.support_geometry.stem_taper       = 0.25;

    for (const auto& [field, is_set] : std::vector<std::pair<SupportGeometryField, std::function<bool(const SupportPoint&)>>>{
             {SupportGeometryField::TipShape,
              [](const SupportPoint& p) { return p.tip_shape == SupportPoint::TipShape::Ball; }},
             {SupportGeometryField::KnotDiameter,
              [](const SupportPoint& p) { return p.knot_radius == Approx(1.0f); }},
             {SupportGeometryField::StemSides,
              [](const SupportPoint& p) { return p.stem_sides == 6; }},
             {SupportGeometryField::StemTaper,
              [](const SupportPoint& p) { return p.stem_taper == Approx(0.25f); }},
         }) {
        INFO("field " << static_cast<int>(field));
        editor.apply_support_geometry_to_selected(field);
        for (const size_t idx : {size_t(0), size_t(2)}) {
            CHECK(is_set(editor.points[idx]));
        }
    }

    CHECK(editor.points[1].tip_shape == SupportPoint::TipShape::Cone);
    CHECK(editor.points[1].knot_radius == Approx(0.5f));
    CHECK(editor.points[1].stem_sides == 4);
    CHECK(editor.points[1].stem_taper == Approx(0.5f));
}

TEST_CASE("One support geometry field at a time leaves the other three alone", "[SlaSupportGeometry]")
{
    SlaSupportPointsEditing editor;
    editor.points.push_back(make_point());
    editor.points[0].tip_shape   = SupportPoint::TipShape::Cone;
    editor.points[0].stem_sides  = 4;
    editor.points[0].knot_radius = 0.5f;
    editor.points[0].stem_taper  = 0.5f;
    editor.select_point(0);

    editor.support_geometry.tip_shape        = SupportPoint::TipShape::Ball;
    editor.support_geometry.knot_diameter_mm = 2.0;
    editor.support_geometry.stem_sides       = 6;
    editor.support_geometry.stem_taper       = 0.25;

    editor.apply_support_geometry_to_selected(SupportGeometryField::TipShape);

    CHECK(editor.points[0].tip_shape == SupportPoint::TipShape::Ball);
    CHECK(editor.points[0].knot_radius == Approx(0.5f));
    CHECK(editor.points[0].stem_sides == 4);
    CHECK(editor.points[0].stem_taper == Approx(0.5f));
}

TEST_CASE("A point placed by hand takes the support geometry of the settings", "[SlaSupportGeometry]")
{
    SlaSupportPointsEditing editor;
    editor.support_geometry.tip_shape        = SupportPoint::TipShape::Cone;
    editor.support_geometry.knot_diameter_mm = 1.0;
    editor.support_geometry.stem_sides       = 4;
    editor.support_geometry.stem_taper       = 0.5;

    editor.add_point(Slic3r::Domain::Vec3d{1., 2., 3.});

    REQUIRE(editor.points.size() == 1);
    CHECK(editor.points[0].tip_shape == SupportPoint::TipShape::Cone);
    CHECK(editor.points[0].knot_radius == Approx(0.5f));
    CHECK(editor.points[0].stem_sides == 4);
    CHECK(editor.points[0].stem_taper == Approx(0.5f));
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

TEST_CASE("A change of the per-point support geometry refreshes the live preview",
          "[SlaSupportGeometry][SlaSupportPreview]")
{
    const std::uint64_t base = hash_support_points(SupportPoints{make_point()});

    SupportPoint tip      = make_point();
    tip.tip_shape         = SupportPoint::TipShape::Cone;
    SupportPoint knot     = make_point();
    knot.knot_radius      = 0.5f;
    SupportPoint sides    = make_point();
    sides.stem_sides      = 6;
    SupportPoint taper    = make_point();
    taper.stem_taper      = 0.5f;
    const std::array<SupportPoint, 4> changed{tip, knot, sides, taper};

    // The tree of a point whose tip shape, knot, cross-section or taper changed is another tree, so
    // the key of the preview has to change with it (M2.16b builds the geometry, this job the key).
    for (const SupportPoint& point : changed) {
        INFO("point changed");
        CHECK(hash_support_points(SupportPoints{point}) != base);
    }
}
