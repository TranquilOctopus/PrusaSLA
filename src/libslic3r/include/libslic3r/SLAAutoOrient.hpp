#pragma once

#include "Slic3r/Domain/Point.hpp"

#include <functional>

namespace Slic3r::Domain {
class ModelObject;
} // namespace Slic3r::Domain

namespace Slic3r::sla {

// Public entry point to the SLA auto-orientation (the engine's SLA/Rotfinder, which is private to
// libslic3r).
//
// Returns rotations in radians about X (x()) and Y (y()) for the object's mesh with its scaling and
// mirroring applied but no rotation. The rotation is R = Ry(y) * Rx(x): rotate about X first, then
// about Y (the order Rotfinder's to_transform3f uses). Callers that apply it to an instance that is
// already rotated must first undo the instance's current rotation.
//
// `status` is called with progress 0..100 (or -1 to only ask whether to continue) and returns false
// to cancel. The default never cancels.
using AutoOrientStatus = std::function<bool(int)>;

// The rotation that makes the object as low as possible on the build plate (fewest layers).
Domain::Vec2d auto_orient_min_height(const Domain::ModelObject& object, AutoOrientStatus status = {});

} // namespace Slic3r::sla
