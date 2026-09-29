#include <catch2/catch_test_macros.hpp>

#include "libslic3r/SLASupportTool.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/ConfigBoxesSLA.hpp"
#include "Slic3r/Domain/Config.hpp"
#include "libslic3r/ConfigViews.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

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

Slic3r::SLAPrintObjectConfigView make_sla_config()
{
    auto full = std::make_shared<const Slic3r::Domain::FullConfigSLA>(Slic3r::Domain::FullConfigSLA::defaults());
    auto obj_settings = std::make_shared<Slic3r::Domain::SLAObjectSettings>();
    obj_settings->items.opt("supports_enable").set(true);
    obj_settings->items.opt("pad_enable").set(true);
    obj_settings->items.opt("support_object_elevation").set(10.0);
    return Slic3r::SLAPrintObjectConfigView(full, obj_settings);
}

Slic3r::SLAPrintObjectConfigView make_sla_config_zero_elevation()
{
    auto full = std::make_shared<const Slic3r::Domain::FullConfigSLA>(Slic3r::Domain::FullConfigSLA::defaults());
    auto obj_settings = std::make_shared<Slic3r::Domain::SLAObjectSettings>();
    obj_settings->items.opt("supports_enable").set(true);
    obj_settings->items.opt("pad_enable").set(true);
    obj_settings->items.opt("pad_around_object").set(true);
    obj_settings->items.opt("pad_around_object_everywhere").set(true);
    obj_settings->items.opt("support_object_elevation").set(10.0);
    return Slic3r::SLAPrintObjectConfigView(full, obj_settings);
}

} // namespace

TEST_CASE("SLASupportTool: generate_support_points_for_tool returns points for lifted object", "[SLASupportTool]")
{
    BoxModel box{20., 20., 40.};
    box.object->instances.front()->set_offset({0., 0., 10.}); // lift 10 mm

    Slic3r::Transform3d object_to_world = Slic3r::Transform3d::Identity();
    object_to_world.translate({0., 0., 10.});

    auto config = make_sla_config();

    auto points = Slic3r::sla::generate_support_points_for_tool(*box.object, object_to_world, config, []{ return false; });

    REQUIRE(points.size() > 0);
}

TEST_CASE("SLASupportTool: build_support_tree_for_tool returns tree with correct elevation", "[SLASupportTool]")
{
    BoxModel box{20., 20., 40.};
    box.object->instances.front()->set_offset({0., 0., 10.}); // lift 10 mm

    Slic3r::Transform3d object_to_world = Slic3r::Transform3d::Identity();
    object_to_world.translate({0., 0., 10.});

    auto config = make_sla_config();

    // First generate points
    auto points = Slic3r::sla::generate_support_points_for_tool(*box.object, object_to_world, config, []{ return false; });
    REQUIRE(points.size() > 0);

    // Then build tree
    auto tree = Slic3r::sla::build_support_tree_for_tool(*box.object, object_to_world, points, config, []{ return false; });

    REQUIRE(tree.tree != nullptr);
    REQUIRE_FALSE(tree.tree->empty());

    // Check bounding box min z is near (object min z - elevation)
    // Object mesh min z in world frame = 10 (lift) + 0 (box bottom) = 10
    // Elevation = 10 (support_object_elevation)
    // Expected tree min z ~= 10 - 10 = 0
    double expected_min_z = 10.0 - config.get<double>("support_object_elevation");
    double actual_min_z = tree.tree->bounding_box().min.z();

    CHECK(std::abs(actual_min_z - expected_min_z) < 0.2);
}

TEST_CASE("SLASupportTool: stop function returns empty result without throwing", "[SLASupportTool]")
{
    BoxModel box{20., 20., 40.};
    box.object->instances.front()->set_offset({0., 0., 10.});

    Slic3r::Transform3d object_to_world = Slic3r::Transform3d::Identity();
    object_to_world.translate({0., 0., 10.});

    auto config = make_sla_config();

    // Stop immediately
    auto points = Slic3r::sla::generate_support_points_for_tool(*box.object, object_to_world, config, []{ return true; });
    CHECK(points.empty());

    auto tree = Slic3r::sla::build_support_tree_for_tool(*box.object, object_to_world, points, config, []{ return true; });
    CHECK(tree.tree == nullptr);
    CHECK(tree.pad == nullptr);
}

TEST_CASE("SLASupportTool: support_tool_elevation returns correct values", "[SLASupportTool]")
{
    auto config = make_sla_config();
    double elev = Slic3r::sla::support_tool_elevation(config);
    // support_object_elevation (10) + pad required elevation (when pad enabled and not embedded)
    CHECK(elev >= 10.0);

    auto config_zero = make_sla_config_zero_elevation();
    double elev_zero = Slic3r::sla::support_tool_elevation(config_zero);
    // In zero-elevation mode (pad around object), elevation should be 0
    CHECK(elev_zero == 0.0);
}