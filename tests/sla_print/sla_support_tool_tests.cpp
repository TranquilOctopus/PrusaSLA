#include <catch2/catch_test_macros.hpp>

#include "libslic3r/SLASupportTool.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/ConfigBoxesSLA.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "Slic3r/Domain/PrinterTechnology.hpp"
#include "libslic3r/ConfigViews.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace {

// A model with one object holding a box of the given size, standing on the plate.
struct BoxModel
{
    Slic3r::Domain::Model model;
    Slic3r::Domain::ModelObject* object{nullptr};

    BoxModel(double x, double y, double z)
    {
        object = model.add_object();
        Slic3r::Biz::Algorithms::ModelObject::add_volume(
            object, Slic3r::Biz::Algorithms::TriangleMesh::make_cube(x, y, z)
        );
        object->add_instance();
    }
};

// The tool API takes the raw config pointers and resolves the SLA object view itself.
struct SlaConfig
{
    Slic3r::Domain::FullConfigSLAPtr full;
    Slic3r::Domain::PartialObjectConfigSLAPtr object_settings;

    Slic3r::SLAPrintObjectConfigView view() const { return Slic3r::SLAPrintObjectConfigView{full, object_settings}; }
};

// No SLA option is declared with location == SLAConfigLocation::Object, so SLAObjectSettings
// carries no items of its own: every key the tests tweak (supports_enable, pad_enable, ...,
// all of which only declare overrides_in = Locations{ Object }) has to be set on the print box
// before the full config is built.
SlaConfig make_sla_config(Slic3r::Domain::ConfigPackSLA pack)
{
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

SlaConfig make_sla_config()
{
    Slic3r::Domain::ConfigPackSLA pack;
    pack.sla_print_settings.items.opt("supports_enable").set(true);
    pack.sla_print_settings.items.opt("pad_enable").set(true);
    pack.sla_print_settings.items.opt("support_object_elevation").set(10.0);
    return make_sla_config(std::move(pack));
}

SlaConfig make_sla_config_zero_elevation()
{
    Slic3r::Domain::ConfigPackSLA pack;
    pack.sla_print_settings.items.opt("supports_enable").set(true);
    // Since M2.14d the raft type decides whether a raft hugs the object; pad_enable /
    // pad_around_object are only the fallback for presets without raft_type.
    pack.sla_print_settings.items.opt("raft_type")
        .set(Slic3r::Domain::sla::RaftType::AroundObject);
    pack.sla_print_settings.items.opt("pad_around_object_everywhere").set(true);
    pack.sla_print_settings.items.opt("support_object_elevation").set(10.0);
    return make_sla_config(std::move(pack));
}

// A box of `count` island support points spread over the bottom face of `size` x `size`. The tree
// builder runs one search per point, so this is how a case asks for a tree that takes long enough
// to be worth stopping.
void add_island_points(Slic3r::Domain::ModelObject *object, double size, int count)
{
    using Slic3r::Domain::SLA::SupportPoint;
    using Slic3r::Domain::SLA::SupportPointType;
    using Slic3r::Domain::Vec3f;

    const int columns = std::max(1, static_cast<int>(std::lround(std::sqrt(double(count)))));
    const int rows    = (count + columns - 1) / columns;
    const double step_x = columns > 1 ? size / (columns + 1) : size / 2.;
    const double step_y = rows > 1 ? size / (rows + 1) : size / 2.;

    Slic3r::Domain::SLA::SupportPoints points;
    points.reserve(std::size_t(columns * rows));
    for (int row = 0; row < rows; ++row)
        for (int column = 0; column < columns; ++column) {
            if (int(points.size()) >= count)
                break;
            points.push_back(SupportPoint{
                Vec3f{float(step_x * (column + 1)), float(step_y * (row + 1)), 0.f},
                0.2f,
                SupportPointType::island});
        }

    object->sla_support_points = std::move(points);
}

// A stop function that says yes once its own delay has passed, and remembers when it first said so,
// so a case can measure the time the tool took to give up after the request rather than the whole
// run (M4.16).
struct StopAfter
{
    std::chrono::steady_clock::time_point start{std::chrono::steady_clock::now()};
    std::chrono::milliseconds delay{std::chrono::milliseconds{150}};
    std::chrono::steady_clock::time_point asked_at{};

    bool expired() const { return std::chrono::steady_clock::now() - start > delay; }

    Slic3r::sla::SupportToolStop stop()
    {
        return [this] {
            if (expired() && asked_at == std::chrono::steady_clock::time_point{})
                asked_at = std::chrono::steady_clock::now();
            return expired();
        };
    }

    // How long the tool ran on after the stop function turned true, in milliseconds. Zero if it
    // never turned true, which is a failure of the case rather than of the tool.
    std::chrono::milliseconds unwind_time(std::chrono::steady_clock::time_point returned_at) const
    {
        if (asked_at == std::chrono::steady_clock::time_point{})
            return std::chrono::milliseconds{0};
        return std::chrono::duration_cast<std::chrono::milliseconds>(returned_at - asked_at);
    }
};

// The budget of M4.16: the tool has to give up within about two seconds of the stop.
const std::chrono::seconds stop_budget{2};

} // namespace

TEST_CASE("SLASupportTool: generate_support_points_for_tool returns points for lifted object", "[SLASupportTool]")
{
    BoxModel box{20., 20., 40.};
    box.object->instances.front()->set_offset({0., 0., 10.}); // lift 10 mm

    Slic3r::Domain::Transform3d object_to_world = Slic3r::Domain::Transform3d::Identity();
    object_to_world.translate(Slic3r::Domain::Vec3d(0., 0., 10.));

    SlaConfig config = make_sla_config();

    auto points = Slic3r::sla::generate_support_points_for_tool(*box.object, object_to_world, config.full, config.object_settings, []{ return false; });

    REQUIRE(points.size() > 0);
}

TEST_CASE("SLASupportTool: build_support_tree_for_tool returns tree with correct elevation", "[SLASupportTool]")
{
    BoxModel box{20., 20., 40.};
    box.object->instances.front()->set_offset({0., 0., 10.}); // lift 10 mm

    Slic3r::Domain::Transform3d object_to_world = Slic3r::Domain::Transform3d::Identity();
    object_to_world.translate(Slic3r::Domain::Vec3d(0., 0., 10.));

    SlaConfig config = make_sla_config();

    // First generate points
    auto points = Slic3r::sla::generate_support_points_for_tool(*box.object, object_to_world, config.full, config.object_settings, []{ return false; });
    REQUIRE(points.size() > 0);

    // Then build tree
    auto tree = Slic3r::sla::build_support_tree_for_tool(*box.object, object_to_world, points, config.full, config.object_settings, []{ return false; });

    REQUIRE(tree.tree != nullptr);
    REQUIRE_FALSE(tree.tree->empty());

    // Check bounding box min z is near (object min z - elevation)
    // Object mesh min z in world frame = 10 (lift) + 0 (box bottom) = 10
    // Elevation = 10 (support_object_elevation)
    // Expected tree min z ~= 10 - 10 = 0
    double expected_min_z = 10.0 - config.view().get<double>("support_object_elevation");
    double actual_min_z = tree.tree->bounding_box().min.z();

    CHECK(std::abs(actual_min_z - expected_min_z) < 0.2);
}

TEST_CASE("SLASupportTool: stop function returns empty result without throwing", "[SLASupportTool]")
{
    BoxModel box{20., 20., 40.};
    box.object->instances.front()->set_offset({0., 0., 10.});

    Slic3r::Domain::Transform3d object_to_world = Slic3r::Domain::Transform3d::Identity();
    object_to_world.translate(Slic3r::Domain::Vec3d(0., 0., 10.));

    SlaConfig config = make_sla_config();

    // Stop immediately
    auto points = Slic3r::sla::generate_support_points_for_tool(*box.object, object_to_world, config.full, config.object_settings, []{ return true; });
    CHECK(points.empty());

    auto tree = Slic3r::sla::build_support_tree_for_tool(*box.object, object_to_world, points, config.full, config.object_settings, []{ return true; });
    CHECK(tree.tree == nullptr);
    CHECK(tree.pad == nullptr);
}

TEST_CASE("SLASupportTool: support_tool_elevation returns correct values", "[SLASupportTool]")
{
    SlaConfig config = make_sla_config();
    double elev = Slic3r::sla::support_tool_elevation(config.full, config.object_settings);
    // support_object_elevation (10) + pad required elevation (when pad enabled and not embedded)
    CHECK(elev >= 10.0);

    SlaConfig config_zero = make_sla_config_zero_elevation();
    double elev_zero = Slic3r::sla::support_tool_elevation(config_zero.full, config_zero.object_settings);
    // In zero-elevation mode (pad around object), elevation should be 0
    CHECK(elev_zero == 0.0);
}

TEST_CASE("SLASupportTool: build_support_tree_for_tool places tree under moved object", "[SLASupportTool]")
{
    BoxModel box{20., 20., 40.};
    // Lift 10 mm AND move to x=30, y=20
    box.object->instances.front()->set_offset({30., 20., 10.});

    Slic3r::Domain::Transform3d object_to_world = Slic3r::Domain::Transform3d::Identity();
    object_to_world.translate(Slic3r::Domain::Vec3d(30., 20., 10.));

    SlaConfig config = make_sla_config();

    // Generate points (they come back in object's mesh frame)
    auto points = Slic3r::sla::generate_support_points_for_tool(*box.object, object_to_world, config.full, config.object_settings, []{ return false; });
    REQUIRE(points.size() > 0);

    // Build tree
    auto tree = Slic3r::sla::build_support_tree_for_tool(*box.object, object_to_world, points, config.full, config.object_settings, []{ return false; });

    REQUIRE(tree.tree != nullptr);
    REQUIRE_FALSE(tree.tree->empty());

    // Object's world bounding box: box is 20x20x40 at origin in object frame, instance offset (30,20,10)
    // So world bbox: x in [30, 50], y in [20, 40], z in [10, 50]
    // Tree should be under the object, so its bbox should overlap in X and Y
    auto tree_bb = tree.tree->bounding_box();
    double obj_min_x = 30., obj_max_x = 50.;
    double obj_min_y = 20., obj_max_y = 40.;
    double obj_min_z = 10.;

    // Check X overlap
    CHECK(tree_bb.max.x() >= obj_min_x - 1e-6);
    CHECK(tree_bb.min.x() <= obj_max_x + 1e-6);
    // Check Y overlap
    CHECK(tree_bb.max.y() >= obj_min_y - 1e-6);
    CHECK(tree_bb.min.y() <= obj_max_y + 1e-6);
    // Check tree max z is at least object world min z - 0.5 (tree extends down from object bottom)
    CHECK(tree_bb.max.z() >= obj_min_z - 0.5);
}

// M2.21c: the preview snapshots the model on the main thread and builds on a worker, so the
// snapshot has to share the meshes of the model instead of copying them.

TEST_CASE("SLASupportTool: the model mesh snapshot shares the meshes of the model", "[SLASupportTool]")
{
    BoxModel box{20., 20., 40.};
    // A volume that is not a model part never reached the tree, and it is not in the snapshot either.
    Slic3r::Biz::Algorithms::ModelObject::add_volume(
        box.object,
        Slic3r::Biz::Algorithms::TriangleMesh::make_cube(5., 5., 5.),
        Slic3r::Domain::ModelVolumeType::NEGATIVE_VOLUME
    );

    Slic3r::Domain::ModelVolume* volume = box.object->volumes.front();
    const std::shared_ptr<const Slic3r::Domain::TriangleMesh> mesh = volume->mesh_ptr();
    const Slic3r::Domain::Vec3f* vertices                         = mesh->its.vertices.data();

    const Slic3r::sla::SupportToolModelMesh snapshot = Slic3r::sla::support_tool_model_mesh(*box.object);

    REQUIRE(snapshot.parts.size() == 1);
    // The very mesh the volume holds, down to the vertex buffer: taking the snapshot copied nothing.
    CHECK(snapshot.parts.front().mesh.get() == mesh.get());
    CHECK(snapshot.parts.front().mesh->its.vertices.data() == vertices);
    // Only the volume's own placement is copied with it.
    CHECK(snapshot.parts.front().matrix.isApprox(volume->get_matrix()));
}

TEST_CASE("SLASupportTool: a snapshot keeps the geometry the model had", "[SLASupportTool]")
{
    BoxModel box{20., 20., 40.};
    const std::shared_ptr<const Slic3r::Domain::TriangleMesh> mesh = box.object->volumes.front()->mesh_ptr();
    const std::size_t vertices                                    = mesh->its.vertices.size();

    const Slic3r::sla::SupportToolModelMesh snapshot = Slic3r::sla::support_tool_model_mesh(*box.object);

    // The model goes on changing while the worker is building: a replaced mesh and a moved volume
    // leave the snapshot on the geometry the build was asked for, so the worker reads data the
    // main thread cannot change under it.
    box.object->volumes.front()->set_mesh(Slic3r::Biz::Algorithms::TriangleMesh::make_sphere(5., 1.));
    box.object->volumes.front()->set_offset(Slic3r::Domain::Vec3d(10., 0., 0.));

    REQUIRE(snapshot.parts.size() == 1);
    CHECK(snapshot.parts.front().mesh.get() == mesh.get());
    CHECK(snapshot.parts.front().mesh->its.vertices.size() == vertices);
    CHECK(snapshot.parts.front().matrix.isApprox(Slic3r::Domain::Transform3d::Identity()));
    // While the model moved on to a mesh of its own, of a very different size.
    CHECK(box.object->volumes.front()->mesh_ptr()->its.vertices.size() != vertices);
}

TEST_CASE("SLASupportTool: a tree built from a snapshot is the tree of the object", "[SLASupportTool]")
{
    BoxModel box{20., 20., 40.};

    Slic3r::Domain::Transform3d object_to_world = Slic3r::Domain::Transform3d::Identity();
    object_to_world.translate(Slic3r::Domain::Vec3d(0., 0., 10.));

    SlaConfig config = make_sla_config();

    const auto points = Slic3r::sla::generate_support_points_for_tool(*box.object, object_to_world, config.full, config.object_settings, []{ return false; });
    REQUIRE(points.size() > 0);

    const auto from_object = Slic3r::sla::build_support_tree_for_tool(*box.object, object_to_world, points, config.full, config.object_settings, []{ return false; });
    const auto from_snapshot =
        Slic3r::sla::build_support_tree_for_tool(Slic3r::sla::support_tool_model_mesh(*box.object), object_to_world, points, config.full, config.object_settings, []{ return false; });

    REQUIRE(from_object.tree != nullptr);
    REQUIRE(from_snapshot.tree != nullptr);
    REQUIRE_FALSE(from_snapshot.tree->empty());

    // The same tree out of the snapshot as out of the object: as many pillars of the same height
    // down to the same place. (The facets themselves are not compared one by one, the order they
    // come in is not part of what the preview shows.)
    CHECK(from_snapshot.tree->facets_count() == from_object.tree->facets_count());
    CHECK(from_snapshot.tree->its.vertices.size() == from_object.tree->its.vertices.size());
    CHECK(std::abs(from_snapshot.tree->bounding_box().min.z() - from_object.tree->bounding_box().min.z()) < 1e-6);
    CHECK(std::abs(from_snapshot.tree->bounding_box().max.z() - from_object.tree->bounding_box().max.z()) < 1e-6);
    CHECK(std::abs(from_snapshot.tree->stats().volume - from_object.tree->stats().volume) < 1e-3);

    // And the same raft, which is built from that same merged mesh (or none at all for both, when
    // the pad does not validate for this model).
    CHECK((from_object.pad == nullptr) == (from_snapshot.pad == nullptr));
    if (from_object.pad != nullptr) {
        CHECK(from_snapshot.pad->facets_count() == from_object.pad->facets_count());
        CHECK(from_snapshot.pad->its.vertices.size() == from_object.pad->its.vertices.size());
        CHECK(std::abs(from_snapshot.pad->bounding_box().min.z() - from_object.pad->bounding_box().min.z()) < 1e-6);
    }
}

TEST_CASE("SLASupportTool: points generated from a snapshot are the points of the object", "[SLASupportTool]")
{
    BoxModel box{20., 20., 40.};

    Slic3r::Domain::Transform3d object_to_world = Slic3r::Domain::Transform3d::Identity();
    object_to_world.translate(Slic3r::Domain::Vec3d(0., 0., 10.));

    SlaConfig config = make_sla_config();

    const auto from_object = Slic3r::sla::generate_support_points_for_tool(*box.object, object_to_world, config.full, config.object_settings, []{ return false; });
    REQUIRE(from_object.size() > 0);

    const auto from_snapshot =
        Slic3r::sla::generate_support_points_for_tool(Slic3r::sla::support_tool_model_mesh(*box.object), object_to_world, config.full, config.object_settings, []{ return false; });

    REQUIRE(from_snapshot.size() == from_object.size());
    for (std::size_t i = 0; i < from_object.size(); ++i) {
        CHECK(from_snapshot[i].pos.isApprox(from_object[i].pos));
        CHECK(from_snapshot[i].head_front_radius == from_object[i].head_front_radius);
    }
}

TEST_CASE("SLASupportTool: the support point generator gives up a big model within the budget",
          "[SLASupportTool]")
{
    // A metre wide cube. Every layer of it is a million times the area of a 20 mm one, so the
    // island sampler alone runs for minutes: this is the case that used not to stop.
    BoxModel box{1000., 1000., 1000.};
    box.object->instances.front()->set_offset({0., 0., 10.});

    Slic3r::Domain::Transform3d object_to_world = Slic3r::Domain::Transform3d::Identity();
    object_to_world.translate(Slic3r::Domain::Vec3d(0., 0., 10.));

    // A coarse layer height, so the layer count is not what is being measured: ten layers of a
    // metre wide plate are enough to make the sampling endless.
    Slic3r::Domain::ConfigPackSLA pack;
    pack.sla_print_settings.items.opt("supports_enable").set(true);
    pack.sla_print_settings.items.opt("pad_enable").set(true);
    pack.sla_print_settings.items.opt("support_object_elevation").set(10.0);
    pack.sla_print_settings.items.opt("layer_height").set(100.);
    SlaConfig config = make_sla_config(std::move(pack));

    StopAfter stop;
    const auto points = Slic3r::sla::generate_support_points_for_tool(
        *box.object, object_to_world, config.full, config.object_settings, stop.stop());
    const auto returned_at = std::chrono::steady_clock::now();

    INFO("asked to stop after " << stop.delay.count() << " ms, took "
                                << stop.unwind_time(returned_at).count() << " ms to give up");
    // The stop function really did come to be asked for, so this is a run that was given up.
    REQUIRE(stop.expired());
    CHECK(stop.unwind_time(returned_at) <= stop_budget);
    // A stopped run answers with what it has, which for the tool is an empty result.
    CHECK(points.empty());
}

TEST_CASE("SLASupportTool: the support tree builder gives up a big model within the budget",
          "[SLASupportTool]")
{
    BoxModel box{1000., 1000., 1000.};
    box.object->instances.front()->set_offset({0., 0., 10.});
    // Four thousand points is four thousand pinheads and pillars, and the raft below them is cut
    // out of the mesh of all of them.
    add_island_points(box.object, 1000., 4000);

    Slic3r::Domain::Transform3d object_to_world = Slic3r::Domain::Transform3d::Identity();
    object_to_world.translate(Slic3r::Domain::Vec3d(0., 0., 10.));

    SlaConfig config = make_sla_config();

    StopAfter stop;
    const auto tree = Slic3r::sla::build_support_tree_for_tool(
        *box.object, object_to_world, box.object->sla_support_points, config.full,
        config.object_settings, stop.stop());
    const auto returned_at = std::chrono::steady_clock::now();

    INFO("asked to stop after " << stop.delay.count() << " ms, took "
                                << stop.unwind_time(returned_at).count() << " ms to give up");
    REQUIRE(stop.expired());
    CHECK(stop.unwind_time(returned_at) <= stop_budget);
    // Stopped, so there is no tree and no raft to draw.
    CHECK(tree.tree == nullptr);
    CHECK(tree.pad == nullptr);
}

TEST_CASE("SLASupportTool: a branching tree gives up between two of its serialised searches",
          "[SLASupportTool]")
{
    // Since M4.5c the searches of the different leaves of a branching tree run one after the other
    // (the nlopt lock is held from the seeding to the end of nlopt_optimize()), so a stop can only
    // be seen from inside a search itself: that is what this case is for.
    BoxModel box{1000., 1000., 1000.};
    box.object->instances.front()->set_offset({0., 0., 10.});
    add_island_points(box.object, 1000., 200);

    Slic3r::Domain::Transform3d object_to_world = Slic3r::Domain::Transform3d::Identity();
    object_to_world.translate(Slic3r::Domain::Vec3d(0., 0., 10.));

    Slic3r::Domain::ConfigPackSLA pack;
    pack.sla_print_settings.items.opt("supports_enable").set(true);
    pack.sla_print_settings.items.opt("pad_enable").set(true);
    pack.sla_print_settings.items.opt("support_object_elevation").set(10.0);
    pack.sla_print_settings.items.opt("support_tree_type")
        .set(Slic3r::Domain::sla::SupportTreeType::Branching);
    SlaConfig config = make_sla_config(std::move(pack));

    StopAfter stop;
    stop.delay = std::chrono::milliseconds{250};

    const auto tree = Slic3r::sla::build_support_tree_for_tool(
        *box.object, object_to_world, box.object->sla_support_points, config.full,
        config.object_settings, stop.stop());
    const auto returned_at = std::chrono::steady_clock::now();

    INFO("asked to stop after " << stop.delay.count() << " ms, took "
                                << stop.unwind_time(returned_at).count() << " ms to give up");
    REQUIRE(stop.expired());
    CHECK(stop.unwind_time(returned_at) <= stop_budget);
    CHECK(tree.tree == nullptr);
    CHECK(tree.pad == nullptr);
}
