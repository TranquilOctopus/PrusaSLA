#pragma once

#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/Domain/TriangleSelector.hpp"
#include "libslic3r/SLAResult.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace Slic3r::sla {

using SupportToolStop = std::function<bool()>; // returns true to cancel

struct SupportToolTree {
    std::shared_ptr<const Domain::TriangleMesh> tree; // null if none
    std::shared_ptr<const Domain::TriangleMesh> pad;  // null if pad disabled or impossible
};

/**
 * @brief The geometry the support tool builds from: the MODEL PART volumes of one object.
 *
 * A Domain::ModelVolume keeps its mesh as a std::shared_ptr<const TriangleMesh> and every copy of a
 * volume shares that pointer, so a snapshot can hand the meshes out instead of duplicating them.
 * That makes taking it O(volumes) with no vertex copied, which is what lets a caller (the support
 * preview service) snapshot the model on the main thread and let a worker thread read it while the
 * model keeps changing: the meshes behind the pointers are const and the model replaces them, it
 * never rewrites one in place.
 *
 * The painting of the volume (the facets the support paint tool marked as enforcers or blockers,
 * Domain::ModelVolume::supported_facets) travels with the part. It is a bit stream over the
 * triangles of that very mesh, so it stays small and is empty for a volume with nothing painted.
 */
struct SupportToolModelMesh {
    struct Part {
        std::shared_ptr<const Domain::TriangleMesh> mesh; // shared with the model, never null
        Domain::Transform3d                          matrix{Domain::Transform3d::Identity()};
        Domain::TriangleSelector::TriangleSplittingData painting; // facets painted as enforcer/blocker
    };

    std::vector<Part> parts;
};

/// @brief Snapshots the MODEL PART volumes of @p object. Copies the volume matrices and shares the
/// meshes, so the cost does not depend on the size of the model. Volumes that are not model parts
/// are left out, the same filter the build has always applied.
SupportToolModelMesh support_tool_model_mesh(const Domain::ModelObject& object);

// `object_to_world` places the model's mesh (use the instance's full matrix). Meshes are
// returned in that same world frame, object NOT lifted: the tree reaches down to
// (object min z - elevation); the caller lifts everything by the elevation to draw it.
SupportToolTree build_support_tree_for_tool(const SupportToolModelMesh& model_mesh,
    const Domain::Transform3d& object_to_world,
    const Domain::SLA::SupportPoints& points,          // in the object's mesh frame
    const Domain::FullConfigSLAPtr& full_config,       // resolved SLA config
    const Domain::PartialObjectConfigSLAPtr& object_settings,
    const SupportToolStop& stop);

// Same, over a snapshot taken by support_tool_model_mesh(): the merging, the AABB and the tree are
// all built on the calling thread, which may be a worker.
SupportToolTree build_support_tree_for_tool(const Domain::ModelObject& object,
    const Domain::Transform3d& object_to_world,
    const Domain::SLA::SupportPoints& points,          // in the object's mesh frame
    const Domain::FullConfigSLAPtr& full_config,       // resolved SLA config
    const Domain::PartialObjectConfigSLAPtr& object_settings,
    const SupportToolStop& stop);

// Generated points, returned in the object's mesh frame (like model_object->sla_support_points).
Domain::SLA::SupportPoints generate_support_points_for_tool(const SupportToolModelMesh& model_mesh,
    const Domain::Transform3d& object_to_world,
    const Domain::FullConfigSLAPtr& full_config,
    const Domain::PartialObjectConfigSLAPtr& object_settings,
    const SupportToolStop& stop);

// Same, over a snapshot taken by support_tool_model_mesh().
Domain::SLA::SupportPoints generate_support_points_for_tool(const Domain::ModelObject& object,
    const Domain::Transform3d& object_to_world,
    const Domain::FullConfigSLAPtr& full_config,
    const Domain::PartialObjectConfigSLAPtr& object_settings,
    const SupportToolStop& stop);

// Elevation the tree uses (support_object_elevation, plus the pad's required elevation when
// the pad is enabled and not embedded; same rule as SLAPrintObject::get_elevation, and 0 in
// zero-elevation mode).
double support_tool_elevation(const Domain::FullConfigSLAPtr& full_config,
                             const Domain::PartialObjectConfigSLAPtr& object_settings);

} // namespace Slic3r::sla