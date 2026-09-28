#include "libslic3r/SLAAutoOrient.hpp"

#include "libslic3r/SLA/Rotfinder.hpp"

namespace Slic3r::sla {

Domain::Vec2d auto_orient_min_height(const Domain::ModelObject& object, AutoOrientStatus status)
{
    RotOptimizeParams params;
    if (status) {
        params.statucb(std::move(status));
    }
    return find_min_z_height_rotation(object, params);
}

} // namespace Slic3r::sla
