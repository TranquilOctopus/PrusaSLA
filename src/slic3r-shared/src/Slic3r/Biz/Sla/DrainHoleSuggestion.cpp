#include "Slic3r/Biz/Sla/DrainHoleSuggestion.hpp"

#include "Slic3r/Biz/Algorithms/AABBMesh.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace Slic3r::Biz::Sla {

using SlaIssue = Slic3r::Biz::Slicing::Sla::SlaIssue;

namespace {

/// Hits closer than this to the Z the search started at are the surface the cavity sits on
/// already, not a wall of the cavity: a layer is 0.05 mm thick, so this is well under one layer
/// and only filters out the degenerate hit a ray that starts on a triangle can produce.
constexpr double z_epsilon_mm = 1e-4;

/// The search for the floor of a cavity starts this far above the bottom of its lowest layer.
/// That bottom is the plane the floor itself lies in, so a ray started exactly there would have to
/// hit the surface it starts on, and a hit at the start of a ray is the degenerate case every
/// raycaster handles worst. One layer up, the floor is an ordinary hit a fraction of a millimetre
/// away, and nothing of the cavity is in between.
constexpr double floor_search_start_offset_mm = 0.05;

} // namespace

Domain::SLA::DrainHole DrainHoleSuggestion::to_drain_hole() const
{
    Domain::SLA::DrainHole hole;
    hole.pos    = position_mm.cast<float>();
    hole.normal = normal.cast<float>();
    hole.radius = static_cast<float>(radius_mm);
    hole.height = static_cast<float>(height_mm);
    hole.failed = false;
    return hole;
}

bool accepts_drain_hole_suggestion(SlaIssue::Kind kind)
{
    return kind == SlaIssue::Kind::Cup || kind == SlaIssue::Kind::TrappedResin;
}

std::optional<DrainHoleSuggestion> suggest_drain_hole(
    const Slic3r::AABBMesh& mesh,
    const Domain::Transform3d& mesh_to_world,
    SlaIssue::Kind kind,
    const Domain::Vec3d& slice_position_mm,
    const DrainHoleSuggestionOptions& opts)
{
    if (!accepts_drain_hole_suggestion(kind)) {
        return std::nullopt;
    }

    // The search runs along the axis of the cavity: down from its bottom for the resin that has
    // to leave through the floor, up from its bottom for the air that has to leave through the
    // roof. See the header for why the floor and the roof are the places. The floor is the plane
    // the search for it starts on, so that one starts a layer higher.
    const bool downwards = kind == SlaIssue::Kind::TrappedResin;
    Domain::Vec3d ray_direction{0., 0., 1.};
    Domain::Vec3d search_origin = slice_position_mm;
    if (downwards) {
        ray_direction(2) = -1.;
        search_origin(2) += floor_search_start_offset_mm;
    }

    // The ray is cast in the frame of the layers and pulled into the frame of the mesh, so the hit
    // comes back in the coordinates the drain holes of the model object are stored in.
    const Domain::Transform3d world_to_mesh = mesh_to_world.inverse();
    const Domain::Vec3d      mesh_origin   = world_to_mesh * search_origin;
    Domain::Vec3d            mesh_dir      = world_to_mesh.linear() * ray_direction;
    if (mesh_dir.norm() < 1e-12) {
        return std::nullopt; // a flat object, the axis of the cavity lies in its plane
    }
    mesh_dir.normalize();

    const std::vector<Slic3r::AABBMesh::hit_result> hits = mesh.query_ray_hits(mesh_origin, mesh_dir);
    if (hits.empty()) {
        return std::nullopt;
    }

    // The surface nearest to the cavity along the axis: the highest one below the start for the
    // floor, the lowest one above it for the roof. Comparing in the frame of the layers is what
    // makes this the cavity's own floor or roof even for a tilted instance.
    const Slic3r::AABBMesh::hit_result* best = nullptr;
    double                             best_z = 0.;
    for (const Slic3r::AABBMesh::hit_result& hit : hits) {
        if (!hit.is_hit()) {
            continue;
        }
        const double z = (mesh_to_world * hit.position()).z();
        if (downwards ? (z >= search_origin.z() - z_epsilon_mm) : (z <= search_origin.z() + z_epsilon_mm)) {
            continue;
        }
        if (best == nullptr || (downwards ? (z > best_z) : (z < best_z))) {
            best   = &hit;
            best_z = z;
        }
    }
    if (best == nullptr) {
        return std::nullopt;
    }

    Domain::Vec3d normal = best->normal();
    if (normal.norm() < 1e-12) {
        normal = ray_direction;
    }
    normal.normalize();
    // The raycast hands back the normal of the facet, which on the wall of a cavity points into the
    // cavity. The hole has to be cut into the material, so the normal of the hole is the other
    // one, and which of the two that is follows from the direction of the search: down for the
    // floor, up for the roof. Deciding it here instead of trusting the winding means a mesh that
    // does not follow the convention still gets a hole that goes the right way. The frame of the
    // layers is where "up" is, so the normal is carried over with the inverse transpose, which a
    // non-uniformly scaled instance needs.
    const Domain::Vec3d world_normal = (mesh_to_mesh.linear().inverse().transpose() * normal).normalized();
    if (downwards ? (world_normal.z() > 0.) : (world_normal.z() < 0.)) {
        normal = -normal;
    }

    DrainHoleSuggestion suggestion;
    suggestion.position_mm = best->position();
    suggestion.normal      = normal;
    suggestion.radius_mm   = opts.radius_mm;
    suggestion.height_mm   = opts.height_mm;
    return suggestion;
}

std::optional<DrainHoleSuggestion> suggest_nearest_drain_hole(
    const std::vector<DrainHoleCandidate>& candidates,
    SlaIssue::Kind kind,
    const Domain::Vec3d& slice_position_mm,
    const DrainHoleSuggestionOptions& opts)
{
    std::optional<DrainHoleSuggestion> nearest;
    double                              nearest_distance = std::numeric_limits<double>::max();
    for (const DrainHoleCandidate& candidate : candidates) {
        if (candidate.mesh == nullptr || candidate.mesh->empty()) {
            continue;
        }
        const Slic3r::AABBMesh aabb_mesh(*candidate.mesh);
        std::optional<DrainHoleSuggestion> suggestion =
            suggest_drain_hole(aabb_mesh, candidate.mesh_to_world, kind, slice_position_mm, opts);
        if (!suggestion.has_value()) {
            continue;
        }
        suggestion->object_id = candidate.object_id;
        const double distance = std::abs((candidate.mesh_to_world * suggestion->position_mm).z() - slice_position_mm.z());
        if (distance < nearest_distance) {
            nearest          = std::move(suggestion);
            nearest_distance = distance;
        }
    }
    return nearest;
}

} // namespace Slic3r::Biz::Sla
