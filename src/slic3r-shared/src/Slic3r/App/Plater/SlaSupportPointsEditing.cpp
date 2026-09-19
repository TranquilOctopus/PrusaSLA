#include "Slic3r/App/Plater/SlaSupportPointsEditing.hpp"

#include <algorithm>

namespace Slic3r::App::Plater {

std::optional<size_t> SlaSupportPointsEditing::find_nearest_point(
    const Domain::Vec3d& mesh_pos, double max_distance_mm) const
{
    std::optional<size_t> nearest_idx;
    double nearest_dist_sq = max_distance_mm * max_distance_mm;

    for (size_t i = 0; i < points.size(); ++i) {
        const Domain::Vec3d point_pos = points[i].pos.cast<double>();
        double dist_sq = (point_pos - mesh_pos).squaredNorm();
        if (dist_sq < nearest_dist_sq) {
            nearest_dist_sq = dist_sq;
            nearest_idx = i;
        }
    }

    return nearest_idx;
}

void SlaSupportPointsEditing::add_point(const Domain::Vec3d& mesh_pos)
{
    Domain::SLA::SupportPoint new_point;
    new_point.pos = mesh_pos.cast<float>();
    new_point.head_front_radius = static_cast<float>(head_diameter_mm / 2.0);
    new_point.type = Domain::SLA::SupportPointType::manual_add;
    points.push_back(new_point);
}

void SlaSupportPointsEditing::remove_point(size_t idx)
{
    if (idx >= points.size()) {
        return;
    }

    points.erase(points.begin() + idx);
    selected_point_indices.erase(idx);

    // Adjust indices > idx
    std::unordered_set<size_t> new_selected;
    for (size_t s : selected_point_indices) {
        if (s > idx) {
            new_selected.insert(s - 1);
        } else if (s < idx) {
            new_selected.insert(s);
        }
    }
    selected_point_indices = std::move(new_selected);
}

void SlaSupportPointsEditing::move_point(size_t idx, const Domain::Vec3d& mesh_pos)
{
    if (idx < points.size()) {
        points[idx].pos = mesh_pos.cast<float>();
    }
}

void SlaSupportPointsEditing::select_point(size_t idx, bool add_to_selection)
{
    if (idx >= points.size()) {
        return;
    }
    if (!add_to_selection) {
        selected_point_indices.clear();
    }
    selected_point_indices.insert(idx);
}

void SlaSupportPointsEditing::deselect_point(size_t idx)
{
    selected_point_indices.erase(idx);
}

void SlaSupportPointsEditing::select_all_points()
{
    selected_point_indices.clear();
    for (size_t i = 0; i < points.size(); ++i) {
        if (!lock_island_supports || !points[i].is_island()) {
            selected_point_indices.insert(i);
        }
    }
}

void SlaSupportPointsEditing::clear_selection()
{
    selected_point_indices.clear();
}

void SlaSupportPointsEditing::delete_selected_points()
{
    if (selected_point_indices.empty()) {
        return;
    }

    std::vector<size_t> indices_to_delete(selected_point_indices.begin(),
                                           selected_point_indices.end());
    std::sort(indices_to_delete.rbegin(), indices_to_delete.rend());

    bool any_deleted = false;
    for (size_t idx : indices_to_delete) {
        if (idx < points.size()) {
            const bool is_island = points[idx].is_island();
            if (!lock_island_supports || !is_island) {
                points.erase(points.begin() + idx);
                any_deleted = true;
            }
        }
    }

    if (any_deleted) {
        selected_point_indices.clear();
    }
}

void SlaSupportPointsEditing::apply_head_diameter_to_selected()
{
    const float new_radius = static_cast<float>(head_diameter_mm / 2.0);
    for (size_t idx : selected_point_indices) {
        if (idx < points.size()) {
            points[idx].head_front_radius = new_radius;
        }
    }
}

std::vector<size_t> SlaSupportPointsEditing::points_in_rectangle(
    const std::vector<Domain::Vec2d>& screen_positions,
    Domain::Vec2d rect_min,
    Domain::Vec2d rect_max)
{
    // Normalize rectangle corners (handle any corner order)
    const Domain::Vec2d normalized_min(
        std::min(rect_min.x(), rect_max.x()),
        std::min(rect_min.y(), rect_max.y()));
    const Domain::Vec2d normalized_max(
        std::max(rect_min.x(), rect_max.x()),
        std::max(rect_min.y(), rect_max.y()));

    std::vector<size_t> result;
    for (size_t i = 0; i < screen_positions.size(); ++i) {
        const Domain::Vec2d& pos = screen_positions[i];
        if (pos.x() >= normalized_min.x() && pos.x() <= normalized_max.x() &&
            pos.y() >= normalized_min.y() && pos.y() <= normalized_max.y()) {
            result.push_back(i);
        }
    }
    return result;
}

} // namespace Slic3r::App::Plater