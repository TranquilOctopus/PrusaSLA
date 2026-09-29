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

    RotOptimizeParams params;
    if (status) {
        params.statucb(std::move(status));
    }

    switch (goal) {
    case AutoOrientGoal::LeastSupports:
        return find_least_supports_rotation(object, params);
    case AutoOrientGoal::MinHeight:
        return find_min_z_height_rotation(object, params);
    }

    return Domain::Vec2d::Zero();
}

Domain::Vec2d auto_orient_min_height(const Domain::ModelObject& object, AutoOrientStatus status)
{
    return auto_orient(object, AutoOrientGoal::MinHeight, std::move(status));
}

} // namespace Slic3r::sla
