// M7.8.8: the role of a generated point reaches the sizes of its support. M7.8.2 put a role on
// every generated point and M7.8.3 gave a very large object its own anchors, but the support tool
// still sized what it generates by the bands of M2.37 - the lowest island heavy, everything else the
// detail preset - so a fragile spike, an island high up and an overhang all came out the same size
// and the rulebook said nothing about them. This is the whole flow of a generation: a small model
// with an anchor, islands, an overhang and a fragile spike, generated the way the tool generates it
// (with the roles of M7.8.2 and the heavy anchors of M7.8.3 already on the points), filled the way
// the tool fills it, and every point ends up in the tip class of its role.
//
// Nothing is sliced and no tree is built: the support points of a model are all this needs.
//
// The shape is written from scratch, as the ones of tests/sla_print/sla_support_roles_tests.cpp are,
// so that every role of the model is there by construction and not by luck: the roles are measured
// against the mesh of the model, not against a fixture someone measured before.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "Slic3r/App/Plater/SlaSupportAutoPresets.hpp"
#include "Slic3r/App/Plater/SlaSupportGeometry.hpp"
#include "Slic3r/App/Plater/SlaSupportPointsSettings.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/ConfigBoxesSLA.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "Slic3r/Domain/PrinterTechnology.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/Domain/SlaLayerHeight.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "libslic3r/SLASupportTool.hpp"

using Catch::Approx;
namespace triangle_mesh = Slic3r::Biz::Algorithms::TriangleMesh;

using Slic3r::App::Plater::apply_support_geometry;
using Slic3r::App::Plater::auto_support_base_layers;
using Slic3r::App::Plater::sla_apply_auto_support_presets;
using Slic3r::App::Plater::sla_auto_detail_preset_name;
using Slic3r::App::Plater::sla_auto_heavy_base_preset_index;
using Slic3r::App::Plater::sla_support_preset;
using Slic3r::App::Plater::sla_support_preset_name;
using Slic3r::App::Plater::SlaAutoSupportChoice;
using Slic3r::App::Plater::SlaAutoSupportPresets;
using Slic3r::App::Plater::SlaSupportGeometry;
using Slic3r::Domain::ConfigView;
using Slic3r::Domain::FullConfigSLAPtr;
using Slic3r::Domain::PartialObjectConfigSLAPtr;
using Slic3r::Domain::sla::SupportAutoDetailPreset;
using Slic3r::Domain::SLA::SupportPoint;
using Slic3r::Domain::SLA::SupportPoints;
using Slic3r::Domain::SLA::SupportPointType;

namespace {

/// The config of one object, as the support tool resolves it (the build_object_sla_config of the
/// preview service). No SLA key is declared with location Object, so the keys the model needs go on
/// the print box before the full config is built.
struct SlaConfig
{
    FullConfigSLAPtr full;
    PartialObjectConfigSLAPtr object_settings;
};

SlaConfig make_sla_config()
{
    Slic3r::Domain::ConfigPackSLA pack;
    pack.sla_print_settings.items.opt("supports_enable").set(true);
    pack.sla_print_settings.items.opt("pad_enable").set(true);
    pack.sla_print_settings.items.opt("support_object_elevation").set(10.0);

    SlaConfig config;
    config.full = std::make_shared<const Slic3r::Domain::FullConfigSLA>(
        pack,
        Slic3r::Domain::Preset::
            HwPrinterConfig{.technology = Slic3r::Domain::PrinterTechnology::SLA}
    );
    config.object_settings = std::make_shared<const Slic3r::Domain::PartialObjectConfigSLA>(
        Slic3r::Domain::SLAObjectSettings{},
        config.full->hw_config()
    );
    return config;
}

/// A model with one object holding one mesh, which is the shape the generator runs on.
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

/// The meshes of one shape, merged the way the tool merges the volumes of an object. Every part is
/// placed on its own, so the shape is one model with three features in it and not three models.
class ShapeBuilder
{
public:
    /// A cube of @p dx x @p dy x @p dz mm with its underside at @p lift and its centre at (x, y), so
    /// the parts below read as one object on the build plate. its_make_cube puts a cube in the
    /// positive corner, so the half extents come off again here.
    ShapeBuilder& cube(double x, double y, double dx, double dy, double dz, double lift)
    {
        indexed_triangle_set cube = triangle_mesh::its_make_cube(dx, dy, dz);
        for (Slic3r::Domain::Vec3f& v : cube.vertices) {
            v.x() += static_cast<float>(x - 0.5 * dx);
            v.y() += static_cast<float>(y - 0.5 * dy);
            v.z() += static_cast<float>(lift);
        }
        return add(cube);
    }

    /// A cylinder of radius @p r and height @p h, standing on @p lift with its axis at (x, y): a
    /// cylinder of its_make_cylinder is already centred around its axis.
    ShapeBuilder& cylinder(double x, double y, double r, double h, double lift)
    {
        indexed_triangle_set cylinder = triangle_mesh::its_make_cylinder(r, h);
        for (Slic3r::Domain::Vec3f& v : cylinder.vertices) {
            v.x() += static_cast<float>(x);
            v.y() += static_cast<float>(y);
            v.z() += static_cast<float>(lift);
        }
        return add(cylinder);
    }

    /// A cone that widens upwards, i.e. standing on its apex: its_make_cone is the other one, with
    /// its wide base on the plate and its apex at the top, so this mirrors it in z. Mirroring keeps
    /// the faces wound outwards, which is what the ray casts of the roles read.
    ShapeBuilder& upward_cone(double x, double y, double r, double h, double lift)
    {
        Slic3r::Domain::TriangleMesh cone = triangle_mesh::make_cone(r, h);
        cone.mirror(Slic3r::Domain::Axis::Z);
        cone.translate(
            Slic3r::Domain::
                Vec3f{static_cast<float>(x), static_cast<float>(y), static_cast<float>(lift + h)}
        );
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

/// The model of the flow: a slab on the plate with a mushroom and a spike beside it, so that one
/// generation of it has every role in it.
///
/// - the slab, 24 x 24 x 4 mm, is what the model stands on. Its underside is the lowest island of
/// the object, which takes the anchors of R4.1, and the flat part of it takes the heavy anchors of
/// R4.2 as well;
/// - the mushroom, a pillar of 3 mm under a cap of 10 x 10 x 2 mm that floats 8 mm above the plate,
/// has an underside facing the plate 100 mm2 above it: the overhangs of R4.6. The bottom face of
/// the cap is an island of its own, a big one (R4.3);
/// - the spike, a blunt tip of 0.6 mm with a cone that widens to 1 mm above it, is a feature too
/// thin to carry anything but the minimum tip (R4.4).
indexed_triangle_set a_model_with_every_role()
{
    return ShapeBuilder{}
        .cube(0., 0., 24., 24., 4., 0.) // the slab, from z = 0 to z = 4
        .cylinder(25., 0., 1.5, 8., 0.) // the pillar of the mushroom, from z = 0 to z = 8
        .cube(25., 0., 10., 10., 2., 8.) // the cap of the mushroom, from z = 8 to z = 10
        .cylinder(-25., 0., 0.3, 0.5, 0.) // the tip of the spike, from z = 0 to z = 0.5
        .upward_cone(-25., 0., 1.0, 4.0, 0.5) // the spike, from z = 0.5 to z = 4.5
        .build();
}

/// The points of a shape, generated the way the support tool generates them: the slice, the
/// generator, the roles of M7.8.2 and the heavy anchors of M7.8.3.
SupportPoints generate(const indexed_triangle_set& its, const SlaConfig& config)
{
    const ShapeModel shape{its};
    return Slic3r::sla::generate_support_points_for_tool(
        *shape.object,
        Slic3r::Domain::Transform3d::Identity(),
        config.full,
        config.object_settings,
        [] { return false; }
    );
}

/// The five tip classes of the rulebook as the config definitions carry them, which is what the tool
/// builds out of a print preset that carries none of the keys, and the two classes of M2.37 out of
/// them: the heavy one on the base of the model, the detail the setting names.
SlaAutoSupportPresets the_presets(const SlaAutoSupportChoice& choice)
{
    SlaAutoSupportPresets presets;
    for (std::size_t index = 0; index < presets.classes.size(); ++index) {
        const std::string name = sla_support_preset_name(static_cast<int>(index));
        presets.classes[index] = sla_support_preset(name);
    }
    presets.base   = presets.classes[std::size_t(sla_auto_heavy_base_preset_index)];
    presets.detail = presets.class_of(sla_auto_detail_preset_name(choice.detail));
    return presets;
}

/// What support_geometry_defaults() reads off the object settings of the model: the tip, tip shape,
/// tip length, knot, cross-section, taper and foot of a fresh profile (M2.16c, M2.24, M2.23b). The
/// class of a point is written over all of it, which is what the flow below shows.
SlaSupportGeometry the_geometry_defaults()
{
    SlaSupportGeometry geometry;
    geometry.tip_diameter_mm = 0.2; // support_head_front_diameter, the T0.2 tip
    geometry.tip_shape       = SupportPoint::TipShape::Ball;
    geometry.tip_length_mm   = 0.5; // support_tip_length
    geometry.stem_sides      = 6; // support_stem_sides, a hexagonal stem (R3.3)
    geometry.base_shape      = SupportPoint::BaseShape::Cylinder;
    return geometry;
}

/// The fill of the tool, played here: the two writes
/// SlaSupportPointsGizmo::fill_generated_point_geometry() does to a generation and nothing else. The
/// tool reads its presets off the print preset of the selected printer - a print preset without the
/// keys is the config definitions, which is what the_presets() is - and the band of the base off the
/// mesh of the model and the layer height the print will be sliced at, both of which are computed
/// here the way the tool computes them.
void fill_generated_points(
    SupportPoints& points,
    const indexed_triangle_set& its,
    const SlaConfig& config,
    const SlaAutoSupportChoice& choice
)
{
    const SlaSupportGeometry geometry = the_geometry_defaults();
    for (SupportPoint& point : points)
        apply_support_geometry(point, geometry);

    // The bottom of the mesh is the bottom of the model in the frame the points are in, which is
    // what the band of the base is measured from.
    const double lowest_z_mm =
        Slic3r::Biz::Algorithms::BoundingBox::construct(its.vertices).min.z();

    ConfigView config_view{config.full, {config.object_settings}};
    config_view.finalize();
    const double layer_height_mm = Slic3r::Domain::sla_effective_layer_height(config_view);
    REQUIRE(layer_height_mm > 0.);

    sla_apply_auto_support_presets(
        points,
        lowest_z_mm,
        layer_height_mm,
        the_presets(choice),
        choice
    );
}

/// What one role of a point is expected to end up as, written out from the rulebook rather than
/// asked of the rule itself, so that a test can disagree with it.
struct RoleExpectation
{
    SupportPoint::Role role;
    double tip_diameter_mm;
    /// Whether the model has to carry a point of this role at all. A small island is not something a
    /// model produces easily - R4.5 moves a point off the detail that would make one - so it is only
    /// checked on the points that do have the role.
    bool required = true;
};

size_t points_of_role(const SupportPoints& points, SupportPoint::Role role)
{
    size_t of_this_role = 0;
    for (const SupportPoint& point : points)
        if (point.role == role)
            ++of_this_role;
    return of_this_role;
}

/// Print a histogram of all roles in the points for debugging.
void info_role_histogram(const SupportPoints& points)
{
    std::array<size_t, 8> counts{};
    for (const SupportPoint& point : points) {
        size_t idx = static_cast<size_t>(point.role);
        if (idx < counts.size()) {
            counts[idx]++;
        }
    }
    UNSCOPED_INFO("Role histogram: Unknown=" << counts[0]
        << " Anchor=" << counts[1]
        << " Island=" << counts[2]
        << " SmallIsland=" << counts[3]
        << " Overhang=" << counts[4]
        << " Fragile=" << counts[5]
        << " AnchorLarge=" << counts[6]
        << " Detail=" << counts[7]);
    for (const SupportPoint& point : points) {
        const char* type_str = point.type == SupportPointType::island ? "island" : 
                              point.type == SupportPointType::slope ? "slope" : "other";
        UNSCOPED_INFO("point: type=" << type_str << " role=" << static_cast<int>(point.role)
            << " pos=(" << point.pos.x() << ", " << point.pos.y() << ", " << point.pos.z() << ")");
    }
}

/// Everything a class puts on a generated point, one role at a time: the tip is the class itself,
/// the contact sinks half of it (R3.1), and the stem, the foot and the cross-section are what every
/// class shares (R3.2 to R3.4), never the class of the point next to it.
void check_the_class_of(const SupportPoint& point, double tip_diameter_mm)
{
    CHECK(2. * point.head_front_radius == Approx(tip_diameter_mm));
    CHECK(point.contact_depth == Approx(0.5 * tip_diameter_mm));
    CHECK(point.pillar_diameter == Approx(1.0)); // R3.3
    CHECK(point.base_diameter == Approx(6.0)); // R3.4
    CHECK(point.base_height == Approx(0.3)); // R3.4
    CHECK(point.tip_shape == SupportPoint::TipShape::Ball); // R3.1
    CHECK(point.stem_sides == 6); // R3.3
    CHECK(point.base_shape == SupportPoint::BaseShape::Cylinder); // R3.4
}

/// Every point of a generation carries the class of its role, and a point no role covers carries the
/// band rule of M2.37, which is what it was sized by before the roles existed.
void check_the_classes_of(
    const SupportPoints& points,
    const std::vector<RoleExpectation>& expected,
    double lowest_z_mm,
    double layer_height_mm
)
{
    info_role_histogram(points);
    for (const RoleExpectation& one : expected) {
        for (const SupportPoint& point : points) {
            if (point.role != one.role) {
                continue;
            }
            INFO(
                "role "
                << static_cast<int>(one.role)
                << " at "
                << point.pos.x()
                << ", "
                << point.pos.y()
                << ", "
                << point.pos.z()
            );
            check_the_class_of(point, one.tip_diameter_mm);
        }
        if (one.required) {
            INFO("role " << static_cast<int>(one.role) << " of the model");
            CHECK(points_of_role(points, one.role) > 0u);
        }
    }

    for (const SupportPoint& point : points) {
        INFO("point at " << point.pos.x() << ", " << point.pos.y() << ", " << point.pos.z());
        const bool is_a_role_of_the_list = std::any_of(
            expected.begin(),
            expected.end(),
            [&point](const RoleExpectation& one) { return one.role == point.role; }
        );
        if (is_a_role_of_the_list) {
            continue;
        }

        // A point the generator did not classify is sized the way it was before the roles existed:
        // the base of the model heavy, everything else the detail preset, which is Light here.
        CHECK(point.role == SupportPoint::Role::Unknown);
        const bool on_the_base = point.is_island()
            && static_cast<double>(point.pos.z()
               ) <= lowest_z_mm + auto_support_base_layers * layer_height_mm;
        CHECK(2. * point.head_front_radius == Approx(on_the_base ? 0.4 : 0.2));
    }
}

} // namespace

TEST_CASE(
    "Every point of a generation is sized by the role of the feature it stands on",
    "[SlaSupportRoleFill]"
)
{
    const indexed_triangle_set shape = a_model_with_every_role();
    const SlaConfig config           = make_sla_config();

    SupportPoints points = generate(shape, config);
    REQUIRE_FALSE(points.empty());

    // The heights the band of M2.37 is measured with, which the last loop of the check needs.
    ConfigView config_view{config.full, {config.object_settings}};
    config_view.finalize();
    const double layer_height_mm = Slic3r::Domain::sla_effective_layer_height(config_view);
    const double lowest_z_mm =
        Slic3r::Biz::Algorithms::BoundingBox::construct(shape.vertices).min.z();

    SECTION("The settings of the automatic placement are on, the way they ship")
    {
        fill_generated_points(points, shape, config, SlaAutoSupportChoice{});

        // R4.1 and R4.2: an anchor is heavy, T0.4. R4.3 and R4.6: an island, a small island and an
        // overhang take the detail preset, the T0.2 class by default. R4.4 and R4.5: the spike takes
        // the minimum tip, T0.1, whatever the rest of the model does.
        check_the_classes_of(
            points,
            {{SupportPoint::Role::Anchor, 0.4},
             {SupportPoint::Role::Island, 0.2},
             {SupportPoint::Role::SmallIsland, 0.2, false},
             {SupportPoint::Role::Overhang, 0.2},
             {SupportPoint::Role::Fragile, 0.1}},
            lowest_z_mm,
            layer_height_mm
        );
    }

    SECTION("The detail preset of Medium gives the islands and the overhangs the medium tip")
    {
        SlaAutoSupportChoice choice;
        choice.detail = SupportAutoDetailPreset::Medium;
        fill_generated_points(points, shape, config, choice);

        // R4.3: an island is the medium class, and this setting is the one that names it. The
        // anchors keep the heavy class and the spike keeps the minimum tip.
        check_the_classes_of(
            points,
            {{SupportPoint::Role::Anchor, 0.4},
             {SupportPoint::Role::Island, 0.3},
             {SupportPoint::Role::SmallIsland, 0.3, false},
             {SupportPoint::Role::Overhang, 0.3},
             {SupportPoint::Role::Fragile, 0.1}},
            lowest_z_mm,
            layer_height_mm
        );
    }

    SECTION("The detail preset of Mini thins everything but the anchors")
    {
        SlaAutoSupportChoice choice;
        choice.detail = SupportAutoDetailPreset::Mini;
        fill_generated_points(points, shape, config, choice);

        // Every point of the detail is at the minimum tip, which is what the spike takes anyway: a
        // setting may not make a support thicker than the rule of the role it is on.
        check_the_classes_of(
            points,
            {{SupportPoint::Role::Anchor, 0.4},
             {SupportPoint::Role::Island, 0.1},
             {SupportPoint::Role::SmallIsland, 0.1, false},
             {SupportPoint::Role::Overhang, 0.1},
             {SupportPoint::Role::Fragile, 0.1}},
            lowest_z_mm,
            layer_height_mm
        );
    }

    SECTION("The heavy base switched off leaves the anchors with the detail preset")
    {
        SlaAutoSupportChoice choice;
        choice.heavy_base = false;
        fill_generated_points(points, shape, config, choice);

        // M2.37 with the heavy base off: there is nothing to tell the base of the model apart, so
        // every point of it is a light support. The spike is still the minimum tip.
        check_the_classes_of(
            points,
            {{SupportPoint::Role::Anchor, 0.2},
             {SupportPoint::Role::Island, 0.2},
             {SupportPoint::Role::SmallIsland, 0.2, false},
             {SupportPoint::Role::Overhang, 0.2},
             {SupportPoint::Role::Fragile, 0.1}},
            lowest_z_mm,
            layer_height_mm
        );
    }
}

TEST_CASE("The band of M2.37 is what a point without a role is sized by", "[SlaSupportRoleFill]")
{
    // A model generated before the roles existed, or by a path that does not classify: its points
    // carry no role, and the rule of M2.37 is all there is to size them, so the tool still answers
    // with the class it answered with before.
    constexpr double layer_height_mm = 0.05;
    const SlaAutoSupportChoice choice;

    SupportPoints points{
        SupportPoint{Slic3r::Domain::Vec3f{1.f, 2.f, 0.025f}, 0.2f, SupportPointType::island},
        SupportPoint{Slic3r::Domain::Vec3f{3.f, 4.f, 8.025f}, 0.2f, SupportPointType::island},
        SupportPoint{Slic3r::Domain::Vec3f{5.f, 6.f, 8.025f}, 0.2f, SupportPointType::slope}
    };

    sla_apply_auto_support_presets(points, 0., layer_height_mm, the_presets(choice), choice);

    for (const SupportPoint& point : points)
        CHECK(point.role == SupportPoint::Role::Unknown);

    // The lowest island is the heavy class and everything else the detail preset: the band is a
    // height, and without a role it is the only thing there is.
    check_the_class_of(points[0], 0.4);
    check_the_class_of(points[1], 0.2);
    check_the_class_of(points[2], 0.2);
}
