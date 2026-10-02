#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/Plater/SlaSupportPointsLift.hpp"
#include "Slic3r/App/Plater/SlaSupportPreviewService.hpp"
#include "Slic3r/Biz/Algorithms/AABBMesh.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/ConfigBoxesSLA.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "Slic3r/Domain/PrinterTechnology.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/Domain/Transformation.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Math.hpp"
#include "libslic3r/SLASupportTool.hpp"

#include <Eigen/Geometry>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <unordered_map>

using Slic3r::App::Plater::sla_support_points_drawing_trafo;
using Slic3r::App::Plater::sla_support_tree_placement;
using Slic3r::App::Plater::SlaSupportPreviewCandidate;
using Slic3r::App::Plater::SlaSupportPreviewDiff;
using Slic3r::App::Plater::SlaSupportPreviewKey;
using Slic3r::App::Plater::SlaSupportTreePlacement;
using Slic3r::App::Plater::diff_sla_support_previews;
using Slic3r::App::Plater::hash_support_points;
using Slic3r::App::Plater::make_sla_support_preview_key;
using Slic3r::Domain::ObjectID;
using Slic3r::Domain::SLA::SupportPoint;
using Slic3r::Domain::SLA::SupportPointType;
using Slic3r::Domain::SLA::SupportPoints;
using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Vec3f;

namespace {

SupportPoints make_points(std::size_t count)
{
    SupportPoints points;
    points.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        points.push_back(SupportPoint{Vec3f{float(i), 1.f, 2.f}, 0.4f, SupportPointType::slope});
    }
    return points;
}

Transform3d translated(double x, double y, double z)
{
    Transform3d trafo = Transform3d::Identity();
    trafo.pretranslate(Slic3r::Domain::Vec3d{x, y, z});
    return trafo;
}

SlaSupportPreviewCandidate candidate(ObjectID id, bool wants, SlaSupportPreviewKey key = {})
{
    SlaSupportPreviewCandidate result;
    result.object_id     = id;
    result.wants_preview = wants;
    result.key           = wants ? key : SlaSupportPreviewKey{};
    return result;
}

// The SLA configuration the support tool engine takes, the way the preview service resolves it: the
// print settings of the plate and the (empty) settings of the object. No SLA option declares
// location == SLAConfigLocation::Object, so every key the tests set has to be set on the print box.
struct SlaConfig
{
    Slic3r::Domain::FullConfigSLAPtr         full;
    Slic3r::Domain::PartialObjectConfigSLAPtr object_settings;
};

SlaConfig make_sla_config(double elevation)
{
    Slic3r::Domain::ConfigPackSLA pack;
    pack.sla_print_settings.items.opt("supports_enable").set(true);
    pack.sla_print_settings.items.opt("support_object_elevation").set(elevation);

    SlaConfig cfg;
    cfg.full = std::make_shared<const Slic3r::Domain::FullConfigSLA>(
        pack,
        Slic3r::Domain::Preset::HwPrinterConfig{.technology = Slic3r::Domain::PrinterTechnology::SLA}
    );
    cfg.object_settings = std::make_shared<const Slic3r::Domain::PartialObjectConfigSLA>(
        Slic3r::Domain::SLAObjectSettings{}, cfg.full->hw_config()
    );
    return cfg;
}

// The same, with the raft hugging the object: zero elevation, the model stands on the plate and the
// supports grow out of the raft around it. Since M2.14d the raft type decides this.
SlaConfig make_sla_config_zero_elevation()
{
    Slic3r::Domain::ConfigPackSLA pack;
    pack.sla_print_settings.items.opt("supports_enable").set(true);
    pack.sla_print_settings.items.opt("raft_type").set(Slic3r::Domain::sla::RaftType::AroundObject);
    pack.sla_print_settings.items.opt("pad_around_object_everywhere").set(true);
    pack.sla_print_settings.items.opt("support_object_elevation").set(5.);

    SlaConfig cfg;
    cfg.full = std::make_shared<const Slic3r::Domain::FullConfigSLA>(
        pack,
        Slic3r::Domain::Preset::HwPrinterConfig{.technology = Slic3r::Domain::PrinterTechnology::SLA}
    );
    cfg.object_settings = std::make_shared<const Slic3r::Domain::PartialObjectConfigSLA>(
        Slic3r::Domain::SLAObjectSettings{}, cfg.full->hw_config()
    );
    return cfg;
}

// The model of the contact case: a plate overhanging a base on every side, so the support points sit
// on a downward facing surface above the base and their pillars reach the plate beside it. The
// instance is moved and turned about Z and about X, the way Auto orient leaves one, and dropped
// onto the plate the way the app drops a model after a rotation.
//
// The instance matrix is a world matrix and it carries the offset of the build plate with it, which
// is what the app's own matrices do: BedPlacement::layout shifts every instance by the transform of
// the plate it sits on, and the bed of an SLA printer does not start at the origin (the bed_shape of
// the SL1 begins at 1.48x1.02). That offset is the whole of what M2.34 was about: the tree was
// built from this matrix and then shifted by the plate a second time.
struct OverhangingModel
{
    Slic3r::Domain::Model             model;
    Slic3r::Domain::ModelObject*      object{nullptr};
    Transform3d                       instance{Transform3d::Identity()};
    SupportPoints                     points;
    Slic3r::sla::SupportToolModelMesh snapshot;

    OverhangingModel()
    {
        object = model.add_object();
        Slic3r::Domain::ModelVolume* base =
            Slic3r::Biz::Algorithms::ModelObject::add_volume(
                object, Slic3r::Biz::Algorithms::TriangleMesh::make_cube(10., 10., 10.)
            );
        base->set_offset(Slic3r::Domain::Vec3d(5., 5., 0.));
        Slic3r::Domain::ModelVolume* plate =
            Slic3r::Biz::Algorithms::ModelObject::add_volume(
                object, Slic3r::Biz::Algorithms::TriangleMesh::make_cube(20., 20., 4.)
            );
        plate->set_offset(Slic3r::Domain::Vec3d(0., 0., 10.));

        // On the underside of the plate, beside the base: a ray straight down from a head here finds
        // no model, so every point of them gets a pillar of its own down to the plate.
        points.push_back(SupportPoint{Vec3f{2.5f, 10.f, 10.f}, 0.4f, SupportPointType::slope});
        points.push_back(SupportPoint{Vec3f{17.5f, 10.f, 10.f}, 0.4f, SupportPointType::slope});
        points.push_back(SupportPoint{Vec3f{10.f, 2.5f, 10.f}, 0.4f, SupportPointType::slope});
        points.push_back(SupportPoint{Vec3f{10.f, 17.5f, 10.f}, 0.4f, SupportPointType::slope});

        instance = placed_instance();

        // What the preview service snapshots on the main thread and the worker builds from (M2.21c).
        snapshot = Slic3r::sla::support_tool_model_mesh(*object);
    }

private:
    Transform3d placed_instance() const
    {
        Transform3d trafo = Transform3d::Identity();
        trafo.rotate(Eigen::AngleAxisd(Slic3r::deg2rad(35.), Slic3r::Domain::Vec3d::UnitZ()));
        trafo.rotate(Eigen::AngleAxisd(Slic3r::deg2rad(-14.), Slic3r::Domain::Vec3d::UnitX()));
        // The offset of the build plate, then where the model stands on it.
        trafo.pretranslate(Slic3r::Domain::Vec3d{60., 40., 0.});
        trafo.pretranslate(Slic3r::Domain::Vec3d{1.48, 1.02, 0.});

        double min_z = std::numeric_limits<double>::max();
        for (const Slic3r::Domain::ModelVolume* vol : object->volumes) {
            for (const Slic3r::Domain::Vec3f& v : vol->mesh().its.vertices) {
                min_z = std::min(min_z, (trafo * vol->get_matrix() * v.cast<double>()).z());
            }
        }
        trafo.pretranslate(Slic3r::Domain::Vec3d{0., 0., -min_z});
        return trafo;
    }
};

// How far the drawn tree is from the drawn support points, and in which direction.
struct Contact
{
    double      distance{0.};
    Slic3r::Domain::Vec3d to_tree{Slic3r::Domain::Vec3d::Zero()};
    std::size_t point{0};
};

// The largest distance from a support point to the tree, both of them placed the way the app places
// them: the point as the model and its markers are drawn (the instance matrix raised by the lift the
// scene applies), the tree as the support preview draws it (the node transform of the placement on
// the mesh the engine placed with the instance matrix).
Contact measure_contact(
    const Slic3r::Domain::TriangleMesh& tree,
    const Transform3d&                  tree_node_trafo,
    const Transform3d&                  model_trafo,
    const SupportPoints&                points
)
{
    // The tree as the scene draws it.
    Slic3r::Domain::TriangleMesh drawn{tree};
    drawn.transform(tree_node_trafo);
    // An AABBMesh is a view on the mesh it was given and not a copy of it, so the mesh has to
    // outlive it: drawn is declared first and lives until the end of the function.
    const Slic3r::AABBMesh tree_aabb{drawn};

    Contact worst;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const Slic3r::Domain::Vec3d at       = model_trafo * points[i].pos.cast<double>();
        int                          face     = 0;
        Slic3r::Domain::Vec3d        closest;
        const double                 distance = std::sqrt(tree_aabb.squared_distance(at, face, closest));
        if (distance > worst.distance) {
            worst.distance = distance;
            worst.to_tree  = closest - at;
            worst.point    = i;
        }
    }
    return worst;
}

// What a head is allowed to be off by on its own: it is a mesh of floats, so a point of it can only
// be as close as the rounding of a vertex at plate coordinates.
constexpr double head_rounding = 0.05;

// How far the tree of a point may stand from the point itself. A support point is the CENTRE of the
// front ball of the pinhead and not a point on the surface of the tree: that ball is of the head
// front radius and reaches into the model by the penetration the head is built with, so a tree
// that is on its own model stands about that radius off its point (the penetration less). "About
// nothing" would be the answer of a tree that has no head under the point at all.
double contact_tolerance(const SlaConfig& cfg)
{
    // The radius of the ball at the front of a pinhead, the very value make_support_cfg() reads
    // into SupportTreeConfig::head_front_radius_mm, read here from the configuration the tree is
    // built with so that a preset asking for another head moves the bound with it.
    return 0.5 * cfg.full->get<double>("support_head_front_diameter") + head_rounding;
}

} // namespace

TEST_CASE("SlaSupportPreviewService - hash_support_points", "[SlaSupportPreviewService]")
{
    SECTION("An empty point list hashes to a stable value")
    {
        REQUIRE(hash_support_points({}) == hash_support_points({}));
    }

    SECTION("Equal point lists hash equally")
    {
        REQUIRE(hash_support_points(make_points(5)) == hash_support_points(make_points(5)));
    }

    SECTION("A moved point changes the hash")
    {
        SupportPoints a = make_points(3);
        SupportPoints b = make_points(3);
        b[1].pos.x() += 0.001f;

        REQUIRE(hash_support_points(a) != hash_support_points(b));
    }

    SECTION("A changed head radius changes the hash")
    {
        SupportPoints a = make_points(3);
        SupportPoints b = make_points(3);
        b[2].head_front_radius = 0.8f;

        REQUIRE(hash_support_points(a) != hash_support_points(b));
    }

    SECTION("More points change the hash")
    {
        REQUIRE(hash_support_points(make_points(3)) != hash_support_points(make_points(4)));
    }

    SECTION("A changed pillar or base size changes the hash")
    {
        SupportPoints a = make_points(3);
        SupportPoints b = make_points(3);
        b[2].pillar_diameter = 1.8f;
        REQUIRE(hash_support_points(a) != hash_support_points(b));

        SupportPoints c = make_points(3);
        c[1].base_diameter = 4.f;
        c[1].base_height   = 1.f;
        REQUIRE(hash_support_points(a) != hash_support_points(c));
    }

    SECTION("A changed tip shape or stem geometry changes the hash")
    {
        // These four reach the support tree mesh since M2.16b, so a project that
        // carries them has to refresh the preview geometry.
        SupportPoints a = make_points(3);

        SupportPoints shape = make_points(3);
        shape[2].tip_shape = SupportPoint::TipShape::Cone;
        REQUIRE(hash_support_points(a) != hash_support_points(shape));

        SupportPoints knot = make_points(3);
        knot[1].knot_radius = 0.8f;
        REQUIRE(hash_support_points(a) != hash_support_points(knot));

        SupportPoints sides = make_points(3);
        sides[0].stem_sides = 6;
        REQUIRE(hash_support_points(a) != hash_support_points(sides));

        SupportPoints taper = make_points(3);
        taper[2].stem_taper = 0.2f;
        REQUIRE(hash_support_points(a) != hash_support_points(taper));
    }
}

TEST_CASE("SlaSupportPreviewService - make_sla_support_preview_key", "[SlaSupportPreviewService]")
{
    const SupportPoints points = make_points(4);

    SECTION("The same inputs give the same key")
    {
        const SlaSupportPreviewKey a =
            make_sla_support_preview_key(points, translated(1, 2, 3), 11, 22, true);
        const SlaSupportPreviewKey b =
            make_sla_support_preview_key(points, translated(1, 2, 3), 11, 22, true);
        REQUIRE(a == b);
    }

    SECTION("A moved instance changes the key")
    {
        const SlaSupportPreviewKey a =
            make_sla_support_preview_key(points, translated(1, 2, 3), 11, 22, true);
        const SlaSupportPreviewKey b =
            make_sla_support_preview_key(points, translated(1, 2, 4), 11, 22, true);
        REQUIRE(!(a == b));
    }

    SECTION("A changed printer configuration changes the key")
    {
        const SlaSupportPreviewKey a =
            make_sla_support_preview_key(points, translated(0, 0, 0), 11, 22, true);
        const SlaSupportPreviewKey b =
            make_sla_support_preview_key(points, translated(0, 0, 0), 12, 22, true);
        REQUIRE(!(a == b));
    }

    SECTION("A changed object configuration changes the key")
    {
        const SlaSupportPreviewKey a =
            make_sla_support_preview_key(points, translated(0, 0, 0), 11, 22, true);
        const SlaSupportPreviewKey b =
            make_sla_support_preview_key(points, translated(0, 0, 0), 11, 23, true);
        REQUIRE(!(a == b));
    }

    SECTION("Turned off supports change the key")
    {
        const SlaSupportPreviewKey a =
            make_sla_support_preview_key(points, translated(0, 0, 0), 11, 22, true);
        const SlaSupportPreviewKey b =
            make_sla_support_preview_key(points, translated(0, 0, 0), 11, 22, false);
        REQUIRE(!(a == b));
    }

    SECTION("The key carries the point count")
    {
        const SlaSupportPreviewKey key =
            make_sla_support_preview_key(points, translated(0, 0, 0), 1, 2, true);
        REQUIRE(key.point_count == points.size());
    }
}

TEST_CASE("SlaSupportPreviewService - diff_sla_support_previews", "[SlaSupportPreviewService]")
{
    const SlaSupportPreviewKey key_a = make_sla_support_preview_key(make_points(3), translated(0, 0, 0), 1, 2, true);
    const SlaSupportPreviewKey key_b = make_sla_support_preview_key(make_points(9), translated(5, 0, 0), 1, 2, true);

    SECTION("A new object is built")
    {
        std::unordered_map<std::size_t, SlaSupportPreviewKey> current;
        const SlaSupportPreviewDiff diff = diff_sla_support_previews({candidate(ObjectID{7}, true, key_a)}, current);

        REQUIRE(diff.to_recompute == std::vector<ObjectID>{ObjectID{7}});
        REQUIRE(diff.to_remove.empty());
        REQUIRE(current.at(7) == key_a);
    }

    SECTION("An unchanged object is left alone")
    {
        std::unordered_map<std::size_t, SlaSupportPreviewKey> current{{7, key_a}};
        const SlaSupportPreviewDiff diff = diff_sla_support_previews({candidate(ObjectID{7}, true, key_a)}, current);

        REQUIRE(diff.to_recompute.empty());
        REQUIRE(diff.to_remove.empty());
        REQUIRE(current.at(7) == key_a);
    }

    SECTION("A changed key rebuilds the object")
    {
        std::unordered_map<std::size_t, SlaSupportPreviewKey> current{{7, key_a}};
        const SlaSupportPreviewDiff diff = diff_sla_support_previews({candidate(ObjectID{7}, true, key_b)}, current);

        REQUIRE(diff.to_recompute == std::vector<ObjectID>{ObjectID{7}});
        REQUIRE(diff.to_remove.empty());
        REQUIRE(current.at(7) == key_b);
    }

    SECTION("An object without points loses its preview")
    {
        std::unordered_map<std::size_t, SlaSupportPreviewKey> current{{7, key_a}};
        const SlaSupportPreviewDiff diff = diff_sla_support_previews({candidate(ObjectID{7}, false)}, current);

        REQUIRE(diff.to_recompute.empty());
        REQUIRE(diff.to_remove == std::vector<ObjectID>{ObjectID{7}});
        REQUIRE(current.find(7) == current.end());
    }

    SECTION("An object that lost its preview is removed even with another key")
    {
        std::unordered_map<std::size_t, SlaSupportPreviewKey> current{{7, key_a}};
        const SlaSupportPreviewDiff diff = diff_sla_support_previews({candidate(ObjectID{7}, false, key_b)}, current);

        REQUIRE(diff.to_recompute.empty());
        REQUIRE(diff.to_remove == std::vector<ObjectID>{ObjectID{7}});
        REQUIRE(current.empty());
    }

    SECTION("An object that left the plate loses its preview")
    {
        std::unordered_map<std::size_t, SlaSupportPreviewKey> current{{7, key_a}, {8, key_b}};
        const SlaSupportPreviewDiff diff = diff_sla_support_previews({candidate(ObjectID{7}, true, key_a)}, current);

        REQUIRE(diff.to_recompute.empty());
        REQUIRE(diff.to_remove == std::vector<ObjectID>{ObjectID{8}});
        REQUIRE(current.size() == 1);
    }

    SECTION("An empty plate clears everything")
    {
        std::unordered_map<std::size_t, SlaSupportPreviewKey> current{{7, key_a}};
        const SlaSupportPreviewDiff diff = diff_sla_support_previews({}, current);

        REQUIRE(diff.to_recompute.empty());
        REQUIRE(diff.to_remove == std::vector<ObjectID>{ObjectID{7}});
        REQUIRE(current.empty());
    }
}

// M2.34: "The supports are also not contacting neither the model nor the support points." The tree
// of the Prepare view is drawn under a node of its own and the model is drawn by the lift of the
// scene, so the two transforms have to be one. The case below plays both parts: it runs the engine
// call the worker runs with the placement the service hands it, draws the tree with the node
// transform of that placement and the support points the way the model and the point markers of the
// tool are drawn, and asks how far the tree is from the points. The pinhead of a point is built at
// the point, but the point is the centre of the ball at the front of that head, so the answer has
// to be about the radius of the ball (contact_tolerance) and not about nothing.
TEST_CASE(
    "SlaSupportPreviewService - the drawn tree touches the model at every support point",
    "[SlaSupportPreviewService][contact]"
)
{
    const OverhangingModel fixture;
    REQUIRE(fixture.snapshot.parts.size() == 2u);

    SECTION("A model raised by its supports touches the model at every support point")
    {
        const SlaConfig config = make_sla_config(6.);
        const double    elevation = Slic3r::sla::support_tool_elevation(config.full, config.object_settings);
        REQUIRE(elevation > 0.);

        // The tree is on its own model when the head of every point stands at its point, which is
        // a head front radius off it: see contact_tolerance().
        const double tolerance = contact_tolerance(config);

        // The service: build the tree from the placement of the object, draw it under a node.
        const SlaSupportTreePlacement placement = sla_support_tree_placement(fixture.instance, elevation);
        const Slic3r::sla::SupportToolTree tree = Slic3r::sla::build_support_tree_for_tool(
            fixture.snapshot,
            placement.object_to_world,
            fixture.points,
            config.full,
            config.object_settings,
            [] { return false; }
        );

        REQUIRE(tree.tree != nullptr);
        REQUIRE_FALSE(tree.tree->empty());

        // The model and its support point markers are drawn with the instance matrix raised by the
        // lift (PlaterScenePresenter::instance_transform, sla_support_points_drawing_trafo).
        const Transform3d model_trafo = sla_support_points_drawing_trafo(fixture.instance, elevation);
        const Contact     contact = measure_contact(*tree.tree, placement.node_trafo, model_trafo, fixture.points);

        INFO("support point " << contact.point << " of " << fixture.points.size() << " is "
                              << contact.distance << " mm off the tree, at " << contact.to_tree);
        CHECK(contact.distance <= tolerance);
    }

    SECTION("A raft around the object (zero elevation) touches the model the same way")
    {
        const SlaConfig config = make_sla_config_zero_elevation();
        const double    elevation = Slic3r::sla::support_tool_elevation(config.full, config.object_settings);
        REQUIRE(elevation == 0.);

        const double tolerance = contact_tolerance(config);

        const SlaSupportTreePlacement placement = sla_support_tree_placement(fixture.instance, elevation);
        const Slic3r::sla::SupportToolTree tree = Slic3r::sla::build_support_tree_for_tool(
            fixture.snapshot,
            placement.object_to_world,
            fixture.points,
            config.full,
            config.object_settings,
            [] { return false; }
        );

        REQUIRE(tree.tree != nullptr);
        REQUIRE_FALSE(tree.tree->empty());

        // Nothing to lift: the model stands on the plate and the node stands still.
        CHECK(placement.node_trafo.isApprox(Transform3d::Identity()));

        const Transform3d model_trafo = sla_support_points_drawing_trafo(fixture.instance, elevation);
        const Contact     contact = measure_contact(*tree.tree, placement.node_trafo, model_trafo, fixture.points);

        INFO("support point " << contact.point << " of " << fixture.points.size() << " is "
                              << contact.distance << " mm off the tree, at " << contact.to_tree);
        CHECK(contact.distance <= tolerance);
    }

    SECTION("The offset of the build plate is not a transform of the tree any more")
    {
        // What M2.34 fixed: the node transform multiplied the transform of the build plate, while
        // the instance matrix the tree was built from carries that very offset. The heads of the
        // tree ended up that far from the points and from the model they belong to.
        const SlaConfig config = make_sla_config(6.);
        const double    elevation = Slic3r::sla::support_tool_elevation(config.full, config.object_settings);
        const double    tolerance = contact_tolerance(config);

        const SlaSupportTreePlacement placement = sla_support_tree_placement(fixture.instance, elevation);
        const Slic3r::sla::SupportToolTree tree = Slic3r::sla::build_support_tree_for_tool(
            fixture.snapshot,
            placement.object_to_world,
            fixture.points,
            config.full,
            config.object_settings,
            [] { return false; }
        );
        REQUIRE(tree.tree != nullptr);

        // The offset the SL1's bed_shape (which begins at 1.48x1.02) puts into every instance
        // matrix, applied to the tree a second time.
        const Transform3d bed_trafo =
            Slic3r::Domain::translation_transform(Slic3r::Domain::Vec3d(1.48, 1.02, 0.));
        const Contact     missed = measure_contact(
            *tree.tree,
            bed_trafo * placement.node_trafo,
            sla_support_points_drawing_trafo(fixture.instance, elevation),
            fixture.points
        );

        // The tree drawn with the plate offset stands nowhere near the points of the model it
        // belongs to, which is the whole of the user report, and the placement of the service leaves
        // the plate transform out. How far off it stands is not the length of the offset: the point
        // of the measure is on the model and the surface of the translated tree nearest to it is not
        // the translated tip, so the miss is only asked to be far outside the contact tolerance the
        // two sections above allow, and both numbers are reported.
        const double offset = bed_trafo.translation().norm();
        INFO("the tree of the plate offset is " << missed.distance << " mm off the points, at "
                                               << missed.to_tree << ", the offset is " << offset
                                               << " mm, a contact is " << tolerance << " mm");
        CHECK(missed.distance > 3 * tolerance);
        CHECK(placement.node_trafo.isApprox(
            Slic3r::Domain::translation_transform(Slic3r::Domain::Vec3d(0., 0., elevation))));
    }
}
