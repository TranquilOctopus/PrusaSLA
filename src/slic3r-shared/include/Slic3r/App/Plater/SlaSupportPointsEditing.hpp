#pragma once

#include "Slic3r/App/Plater/SlaSupportGeometry.hpp"
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
    double pillar_diameter_mm = 0.0;
    double base_diameter_mm = 0.0;
    double base_height_mm = 0.0;
    bool head_diameter_use_global = true;
    bool pillar_diameter_use_global = true;
    bool base_diameter_use_global = true;
    bool base_height_use_global = true;
    std::unordered_set<size_t> selected_point_indices;
    bool lock_island_supports = false;

    // The tip shape, knot, stem cross-section and stem taper a point takes (M2.16c). Filled from
    // the Supports & raft settings, so a new point gets the geometry the user configured, and
    // apply_support_geometry_to_selected(field) writes one of them on the points that are selected.
    SlaSupportGeometry support_geometry;

    // Core editing operations
    std::optional<size_t> find_nearest_point(const Domain::Vec3d& mesh_pos, double max_distance_mm) const;
    void add_point(const Domain::Vec3d& mesh_pos);
    void remove_point(size_t idx);
    void move_point(size_t idx, const Domain::Vec3d& mesh_pos);

    // Selection operations
    void select_point(size_t idx, bool add_to_selection = false);
    void deselect_point(size_t idx);
    void toggle_point(size_t idx);
    void select_all_points();
    void clear_selection();
    void delete_selected_points();
    void apply_head_diameter_to_selected();
    void apply_pillar_diameter_to_selected();
    void apply_base_diameter_to_selected();
    void apply_base_height_to_selected();
    void apply_support_geometry_to_selected(SupportGeometryField field);

    /// The geometry the current selection is shown with, empty when nothing is selected or the
    /// selected points disagree on it.
    std::optional<SlaSupportGeometry> selected_support_geometry() const
    {
        return selection_support_geometry(points, selected_point_indices);
    }

    // Rectangle selection (works on pre-projected screen positions)
    static std::vector<size_t> points_in_rectangle(
        const std::vector<Domain::Vec2d>& screen_positions,
        Domain::Vec2d rect_min,
        Domain::Vec2d rect_max);
};

} // namespace Slic3r::App::Plater