#pragma once

#include "Slic3r/Domain/SLA/DrainHole.hpp"
#include "Slic3r/Domain/Types.hpp"

#include <optional>
#include <unordered_set>
#include <vector>

namespace Slic3r::App::Plater {

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