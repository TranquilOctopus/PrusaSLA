#pragma once

#include "Slic3r/App/Plater/SlaSupportGeometry.hpp"
#include "Slic3r/App/Plater/SlaSupportOnModel.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/Domain/Types.hpp"

#include <optional>
#include <unordered_set>
#include <vector>

namespace Slic3r::App::Plater {

struct SlaSupportPointsEditing
{
    Domain::SLA::SupportPoints points;
    double pillar_diameter_mm = 0.0;
    double base_diameter_mm = 0.0;
    double base_height_mm = 0.0;
    bool head_diameter_use_global = true;
    bool pillar_diameter_use_global = true;
    bool base_diameter_use_global = true;
    bool base_height_use_global = true;
    std::unordered_set<size_t> selected_point_indices;
    bool lock_island_supports = false;

    // The tip diameter, tip shape, tip length, knot, stem cross-section and stem taper a point takes
    // (M2.16c, M2.24). Filled from the Supports & raft settings, so a new point gets the geometry
    // the user configured, and apply_support_geometry_to_selected(field) writes one of them on the
    // points that are selected. The tip diameter is the tool's "head diameter" control: it is the
    // only home of that number, which a preset button also writes.
    SlaSupportGeometry support_geometry;

    // The "may this support end on the model" state a clicked point takes (M2.33). It belongs to
    // the "New supports" group, so a point placed by hand is an ordinary support (Inherit, the
    // object's own setting) until the group is changed, and editing the state of points that are
    // already there leaves it alone.
    SupportOnModel new_support_on_model{SupportOnModel::Inherit};

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
    void apply_pillar_diameter_to_selected();
    void apply_base_diameter_to_selected();
    void apply_base_height_to_selected();
    void apply_support_geometry_to_selected(SupportGeometryField field);

    // The per-point "may this support end on the model" switch (M2.26) of the points that are
    // selected. A new point takes Inherit, i.e. the object's own setting, so a point placed by
    // hand is an ordinary support.
    void apply_support_on_model_to_selected(SupportOnModel on_model);

    /// The geometry the current selection is shown with, empty when nothing is selected or the
    /// selected points disagree on it.
    std::optional<SlaSupportGeometry> selected_support_geometry() const
    {
        return selection_support_geometry(points, selected_point_indices);
    }

    /// The "may rest on the model" state the current selection is shown with, empty when nothing
    /// is selected or the selected points disagree on it.
    std::optional<SupportOnModel> selected_support_on_model() const
    {
        return selection_support_on_model(points, selected_point_indices);
    }

    // Rectangle selection (works on pre-projected screen positions)
    static std::vector<size_t> points_in_rectangle(
        const std::vector<Domain::Vec2d>& screen_positions,
        Domain::Vec2d rect_min,
        Domain::Vec2d rect_max);
};

} // namespace Slic3r::App::Plater