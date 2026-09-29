#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "libslic3r/SLA/Rotfinder.hpp"
#include "libslic3r/SLAAutoOrient.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/Model.hpp"

#include <Eigen/Geometry>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

using Catch::Approx;

namespace {

// A model with one object holding a box of the given size, standing on the plate. The box can be
// tilted in the mesh itself, because that is what the rotation optimizer works on.
struct BoxModel
{
    Slic3r::Domain::Model model;
    Slic3r::Domain::ModelObject* object{nullptr};

    BoxModel(double x, double y, double z, double tilt_about_x_rad = 0.)
    {
        Slic3r::Domain::TriangleMesh mesh{Slic3r::Biz::Algorithms::TriangleMesh::make_cube(x, y, z)};
        if (tilt_about_x_rad != 0.) {
            mesh.transform(Slic3r::Transform3d{
                Eigen::AngleAxisd{tilt_about_x_rad, Slic3r::Vec3d::UnitX()}});
        }

        object = model.add_object();
        Slic3r::Biz::Algorithms::ModelObject::add_volume(object, std::move(mesh));
        object->add_instance();
    }
};

// Height of the object's mesh after rotating it by the given X/Y angles.
double height_after_rotation(const Slic3r::Domain::ModelObject& object, const Slic3r::Vec2d& rotation)
{
    const Slic3r::Transform3f trafo = Slic3r::sla::rotation_angles_to_transform(rotation);
    float min_z = std::numeric_limits<float>::max();
    float max_z = std::numeric_limits<float>::lowest();
    for (const auto& volume : object.volumes) {
        for (const auto& vertex : volume->mesh().its.vertices) {
            const float z = (trafo * vertex).z();
            min_z = std::min(min_z, z);
            max_z = std::max(max_z, z);
        }
    }
    return double(max_z - min_z);
}

struct Faces
{
    // Area of the biggest face of the mesh.
    double largest{0.};
    // Area of the biggest face lying flat on the build plate.
    double largest_horizontal{0.};
    // Normal of the biggest face of the mesh.
    Slic3r::Vec3d largest_normal{Slic3r::Vec3d::Zero()};
};

// The faces of the object's mesh after rotating it by the given X/Y angles.
Faces faces_after_rotation(const Slic3r::Domain::ModelObject& object, const Slic3r::Vec2d& rotation)
{
    const Slic3r::Transform3f trafo = Slic3r::sla::rotation_angles_to_transform(rotation);
    Faces faces;
    for (const auto& volume : object.volumes) {
        const indexed_triangle_set& its{volume->mesh().its};
        for (const auto& face : its.indices) {
            const Slic3r::Vec3d p0{trafo * its.vertices[face[0]]};
            const Slic3r::Vec3d p1{trafo * its.vertices[face[1]]};
            const Slic3r::Vec3d p2{trafo * its.vertices[face[2]]};
            const Slic3r::Vec3d cross{(p1 - p0).cross(p2 - p0)};
            const double area = 0.5 * cross.norm();
            if (area > faces.largest) {
                faces.largest        = area;
                faces.largest_normal = cross.normalized();
            }
            if (std::abs(cross.normalized().z()) > 1. - 1e-3 && area > faces.largest_horizontal) {
                faces.largest_horizontal = area;
            }
        }
    }
    return faces;
}

} // namespace

TEST_CASE("Rotfinder: minimum-height rotation lays a tall box down", "[SLA][Rotfinder]")
{
    BoxModel box{10., 10., 40.};
    REQUIRE(height_after_rotation(*box.object, Slic3r::Vec2d::Zero()) > 39.);

    const Slic3r::Vec2d rotation =
        Slic3r::sla::find_min_z_height_rotation(*box.object, Slic3r::sla::RotOptimizeParams{});

    REQUIRE(std::isfinite(rotation.x()));
    REQUIRE(std::isfinite(rotation.y()));
    // Lying down, the box is 10 mm tall instead of 40 mm.
    CHECK(height_after_rotation(*box.object, rotation) < 11.);
}

TEST_CASE("Auto orient (public API) lays a tall box down", "[SLA][Rotfinder]")
{
    BoxModel box{10., 10., 40.};
    const Slic3r::Vec2d rotation = Slic3r::sla::auto_orient_min_height(*box.object);
    CHECK(height_after_rotation(*box.object, rotation) < 11.);
}

TEST_CASE("Rotfinder: misalignment rotation returns finite angles", "[SLA][Rotfinder]")
{
    BoxModel box{10., 20., 5.};

    const Slic3r::Vec2d rotation = Slic3r::sla::find_best_misalignment_rotation(
        *box.object, Slic3r::sla::RotOptimizeParams{}.accuracy(0.1f)
    );

    CHECK(std::isfinite(rotation.x()));
    CHECK(std::isfinite(rotation.y()));
}

TEST_CASE("Auto orient: fewest supports lays a tilted box on its largest face", "[SLA][Rotfinder]")
{
    // A 40 x 10 x 10 mm box tilted 30 degrees in the mesh. The mesh is cut into triangles, so its
    // biggest faces are the 200 mm2 halves of the two 400 mm2 sides.
    BoxModel box{40., 10., 10., 30. * std::numbers::pi / 180.};
    const double half_largest_face_area = 40. * 10. / 2.;

    const Faces tilted = faces_after_rotation(*box.object, Slic3r::Vec2d::Zero());
    REQUIRE(tilted.largest == Approx(half_largest_face_area));
    REQUIRE(std::abs(tilted.largest_normal.z()) < 0.9); // the biggest face is not horizontal yet
    // No face of the tilted box is flat.
    REQUIRE(tilted.largest_horizontal == 0.);

    const Slic3r::Vec2d rotation =
        Slic3r::sla::auto_orient(*box.object, Slic3r::sla::AutoOrientGoal::LeastSupports);

    REQUIRE(std::isfinite(rotation.x()));
    REQUIRE(std::isfinite(rotation.y()));

    // The biggest face is the one the box can rest on the plate with, without any support.
    const Faces laid_down = faces_after_rotation(*box.object, rotation);
    CHECK(laid_down.largest == Approx(half_largest_face_area));
    CHECK(laid_down.largest_horizontal == Approx(laid_down.largest));
}

TEST_CASE("Auto orient: an object with nothing to rotate does not throw", "[SLA][Rotfinder]")
{
    Slic3r::Domain::Model model;
    Slic3r::Domain::ModelObject* object{model.add_object()};

    CHECK_NOTHROW(Slic3r::sla::auto_orient(*object, Slic3r::sla::AutoOrientGoal::LeastSupports));
    CHECK_NOTHROW(Slic3r::sla::auto_orient(*object, Slic3r::sla::AutoOrientGoal::MinHeight));
}
