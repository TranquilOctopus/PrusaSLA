#include <catch2/catch_test_macros.hpp>

#include "libslic3r/SLA/Rotfinder.hpp"
#include "libslic3r/SLAAutoOrient.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/Model.hpp"

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
