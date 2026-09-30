#include "libslic3r/SLAAutoOrient.hpp"

#include "libslic3r/SLA/Rotfinder.hpp"
#include "Slic3r/Domain/Model.hpp"

#include <algorithm>

namespace Slic3r::sla {

namespace {

// Rotfinder rotates the mesh of the object's first instance, so an object without an instance or
// without printable geometry has nothing to rotate. Report "no rotation" for it.
bool has_geometry_to_rotate(const Domain::ModelObject& object)
{
    return !object.instances.empty() && std::any_of(
        object.volumes.begin(), object.volumes.end(), [](const Domain::ModelVolume* volume) {
            return volume->is_model_part() && !volume->mesh().its.vertices.empty();
        });
}

} // namespace

Domain::Vec2d auto_orient(
    const Domain::ModelObject& object, AutoOrientGoal goal, AutoOrientStatus status
)
{
    if (!has_geometry_to_rotate(object)) {
        return Domain::Vec2d::Zero();
    }

    return auto_orient(mesh_to_rotate(object), goal, std::move(status));
}

Domain::Vec2d auto_orient_min_height(const Domain::ModelObject& object, AutoOrientStatus status)
{
    return auto_orient(object, AutoOrientGoal::MinHeight, std::move(status));
}

Domain::TriangleMesh auto_orient_mesh(const Domain::ModelObject& object)
{
    if (!has_geometry_to_rotate(object)) {
        return Domain::TriangleMesh{};
    }

    return mesh_to_rotate(object);
}

Domain::Vec2d auto_orient(const Domain::TriangleMesh& mesh, AutoOrientGoal goal, AutoOrientStatus status)
{
    if (mesh.its.vertices.empty() || mesh.its.indices.empty()) {
        return Domain::Vec2d::Zero();
    }

    RotOptimizeParams params;
    if (status) {
        params.statucb(std::move(status));
    }

    switch (goal) {
    case AutoOrientGoal::LeastSupports:
        return find_least_supports_rotation(mesh, params);
    case AutoOrientGoal::LeastPeel:
        return find_least_peel_rotation(mesh, params);
    case AutoOrientGoal::NoCups:
        return find_no_cups_rotation(mesh, params);
    case AutoOrientGoal::MinHeight:
        return find_min_z_height_rotation(mesh, params);
    }

    return Domain::Vec2d::Zero();
}

} // namespace Slic3r::sla
