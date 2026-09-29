#pragma once

#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "libslic3r/SLAResult.hpp"

#include <functional>
#include <memory>

namespace Slic3r::sla {

using SupportToolStop = std::function<bool()>; // returns true to cancel

struct SupportToolTree {
    std::shared_ptr<const Domain::TriangleMesh> tree; // null if none
    std::shared_ptr<const Domain::TriangleMesh> pad;  // null if pad disabled or impossible
};

// `object_to_world` places the object's mesh (use the instance's full matrix). Meshes are
// returned in that same world frame, object NOT lifted: the tree reaches down to
// (object min z - elevation); the caller lifts everything by the elevation to draw it.
SupportToolTree build_support_tree_for_tool(const Domain::ModelObject& object,
    const Domain::Transform3d& object_to_world,
    const Domain::SLA::SupportPoints& points,          // in the object's mesh frame
    const Domain::ConfigView& object_config,           // resolved SLA object config
    const SupportToolStop& stop);

// Generated points, returned in the object's mesh frame (like model_object->sla_support_points).
Domain::SLA::SupportPoints generate_support_points_for_tool(const Domain::ModelObject& object,
    const Domain::Transform3d& object_to_world,
    const Domain::ConfigView& object_config,
    const SupportToolStop& stop);

// Elevation the tree uses (support_object_elevation, plus the pad's required elevation when
// the pad is enabled and not embedded; same rule as SLAPrintObject::get_elevation, and 0 in
// zero-elevation mode).
double support_tool_elevation(const Domain::ConfigView& object_config);

} // namespace Slic3r::sla