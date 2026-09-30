#pragma once

#include "Slic3r/Domain/SLA/DrainHole.hpp"
#include "Slic3r/Domain/Types.hpp"

#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Slic3r::App::Plater {

/// The position and the normal of a drain hole in the frame the drain holes of a model object are
/// stored in, computed from a raycast hit on one of its volumes exactly like the hollow gizmo does.
///
/// The normal is the one the engine cuts with, pointing INTO the material. sla::to_mesh cuts the
/// hole as a cylinder that starts at the position and runs along the normal, and
/// sla::transform_drainhole_points pulls its near cap a millimetre back along the same direction to
/// bury it, so a normal that points out of the model cuts nothing. A raycast hands back the outward
/// facet normal, which is the opposite one, so it is negated here. The legacy GLGizmoHollow negated
/// it the same way, and the 3MF files it wrote carry the negated normal, so the convention on disk
/// is the one that is stored and the files of both slicers keep cutting the same way.
std::pair<Domain::Vec3d, Domain::Vec3d> drain_hole_pos_normal_in_object_mesh(
    const Domain::Transform3d& volume_to_mesh,
    const Domain::Vec3d& volume_pos,
    const Domain::Vec3d& volume_normal
);

struct SlaDrainHolesEditing
{
    Domain::SLA::DrainHoles holes;
    double hole_radius_mm = 5.0;
    double hole_height_mm = 10.0;
    std::unordered_set<size_t> selected_hole_indices;

    // Core editing operations
    std::optional<size_t> find_nearest_hole(const Domain::Vec3d& mesh_pos, double max_distance_mm) const;
    void add_hole(const Domain::Vec3d& mesh_pos, const Domain::Vec3d& mesh_normal);
    void remove_hole(size_t idx);
    void move_hole(size_t idx, const Domain::Vec3d& mesh_pos, const Domain::Vec3d& mesh_normal);

    // Selection operations
    void select_hole(size_t idx, bool add_to_selection = false);
    void deselect_hole(size_t idx);
    void toggle_hole(size_t idx);
    void select_all_holes();
    void clear_selection();
    void delete_selected_holes();

    // Property operations on selection
    void apply_radius_to_selected();
    void apply_height_to_selected();
};

} // namespace Slic3r::App::Plater