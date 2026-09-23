#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "libslic3r/SLA/Rotfinder.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/Model.hpp"

using namespace Slic3r;
using namespace Slic3r::Domain;
using namespace Slic3r::Biz::Algorithms;
using Catch::Approx;

TEST_CASE("Rotfinder: find_min_z_height_rotation lays down a tall box", "[SLA][Rotfinder]")
{
    // Create a 10 x 10 x 40 mm box standing upright (Z is the tall axis)
    TriangleMesh mesh = TriangleMesh::make_cube(10.0, 10.0, 40.0);

    // Create a Model and ModelObject
    Model model;
    ModelObject* mo = Model::add_object(&model, "box", "box", std::move(mesh));

    // Ensure there's an instance
    REQUIRE(mo->instances.size() == 1);

    // Find rotation that minimizes Z height
    sla::RotOptimizeParams params;
    params.accuracy(0.5f); // Use moderate accuracy for faster test
    Vec2d rot = sla::find_min_z_height_rotation(*mo, params);

    // Check that returned angles are finite
    REQUIRE(std::isfinite(rot.x()));
    REQUIRE(std::isfinite(rot.y()));

    // Apply the rotation to the mesh and check bounding box height
    Transform3f tr = sla::rotation_angles_to_transform(rot);
    TriangleMesh rotated_mesh = mo->raw_mesh();
    rotated_mesh.transform(tr.cast<double>());

    BoundingBox3d bb = rotated_mesh.bounding_box();
    double height = bb.max.z() - bb.min.z();

    // The box should lay down, so height should be ~10 mm (the shortest dimension)
    // Allow some tolerance for optimization accuracy
    CHECK(height < 15.0);
}

TEST_CASE("Rotfinder: find_best_misalignment_rotation returns finite angles", "[SLA][Rotfinder]")
{
    // Create a simple cube
    TriangleMesh mesh = TriangleMesh::make_cube(10.0, 10.0, 10.0);

    Model model;
    ModelObject* mo = Model::add_object(&model, "cube", "cube", std::move(mesh));
    REQUIRE(mo->instances.size() == 1);

    sla::RotOptimizeParams params;
    params.accuracy(0.3f);
    Vec2d rot = sla::find_best_misalignment_rotation(*mo, params);

    REQUIRE(std::isfinite(rot.x()));
    REQUIRE(std::isfinite(rot.y()));
}

TEST_CASE("Rotfinder: find_min_z_height_rotation on flat box returns finite angles", "[SLA][Rotfinder]")
{
    // Create a flat box (10 x 20 x 5 mm)
    TriangleMesh mesh = TriangleMesh::make_cube(10.0, 20.0, 5.0);

    Model model;
    ModelObject* mo = Model::add_object(&model, "flat_box", "flat_box", std::move(mesh));
    REQUIRE(mo->instances.size() == 1);

    sla::RotOptimizeParams params;
    params.accuracy(0.5f);
    Vec2d rot = sla::find_min_z_height_rotation(*mo, params);

    REQUIRE(std::isfinite(rot.x()));
    REQUIRE(std::isfinite(rot.y()));

    // Apply rotation and check height is minimized (should be ~5mm)
    Transform3f tr = sla::rotation_angles_to_transform(rot);
    TriangleMesh rotated_mesh = mo->raw_mesh();
    rotated_mesh.transform(tr.cast<double>());

    BoundingBox3d bb = rotated_mesh.bounding_box();
    double height = bb.max.z() - bb.min.z();

    CHECK(height < 10.0); // Should be close to 5mm
}