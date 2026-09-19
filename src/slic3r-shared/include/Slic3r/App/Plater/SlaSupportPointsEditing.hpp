#pragma once

#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/Domain/Types.hpp"

#include <optional>
#include <unordered_set>
#include <vector>

namespace Slic3r::App::Plater {

struct SlaSupportPointsEditing
{
    Domain::SLA::SupportPoints points;
    double head_diameter_mm = 0.4;
    std::unordered_set<size_t> selected_point_indices;
    bool lock_island_supports = false;

    // Core editing operations
    std::optional<size_t> find_nearest_point(const Domain::Vec3d& mesh_pos, double max_distance_mm) const;
    void add_point(const Domain::Vec3d& mesh_pos);
    void remove_point(size_t idx);
    void move_point(size_t idx, const Domain::Vec3d& mesh_pos);

    // Selection operations
    void select_point(size_t idx, bool add_to_selection = false);
    void deselect_point(size_t idx);
    void select_all_points();
    void clear_selection();
    void delete_selected_points();
    void apply_head_diameter_to_selected();

    // Rectangle selection (works on pre-projected screen positions)
    static std::vector<size_t> points_in_rectangle(
        const std::vector<Domain::Vec2d>& screen_positions,
        Domain::Vec2d rect_min,
        Domain::Vec2d rect_max);
};

} // namespace Slic3r::App::Plater