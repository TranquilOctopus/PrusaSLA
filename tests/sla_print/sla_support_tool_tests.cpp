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
#include <cmath>
#include <limits>
#include <utility>

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