#include "Slic3r/App/Plater/SlaDrainHolesEditing.hpp"

#include <algorithm>

namespace Slic3r::App::Plater {

std::optional<size_t> SlaDrainHolesEditing::find_nearest_hole(
    const Domain::Vec3d& mesh_pos, double max_distance_mm) const
{
    std::optional<size_t> nearest_idx;
    double nearest_dist_sq = max_distance_mm * max_distance_mm;

    for (size_t i = 0; i < holes.size(); ++i) {
        const Domain::Vec3d hole_pos = holes[i].pos.cast<double>();
        double dist_sq = (hole_pos - mesh_pos).squaredNorm();
        if (dist_sq < nearest_dist_sq) {
            nearest_dist_sq = dist_sq;
            nearest_idx = i;
        }
    }

    return nearest_idx;
}

void SlaDrainHolesEditing::add_hole(const Domain::Vec3d& mesh_pos, const Domain::Vec3d& mesh_normal)
{
    Domain::SLA::DrainHole new_hole;
    new_hole.pos = mesh_pos.cast<float>();
    new_hole.normal = mesh_normal.cast<float>();
    new_hole.radius = static_cast<float>(hole_radius_mm);
    new_hole.height = static_cast<float>(hole_height_mm);
    new_hole.failed = false;
    holes.push_back(new_hole);
}

void SlaDrainHolesEditing::remove_hole(size_t idx)
{
    if (idx >= holes.size()) {
        return;
    }

    holes.erase(holes.begin() + idx);
    selected_hole_indices.erase(idx);

    // Adjust indices > idx
    std::unordered_set<size_t> new_selected;
    for (size_t s : selected_hole_indices) {
        if (s > idx) {
            new_selected.insert(s - 1);
        } else if (s < idx) {
            new_selected.insert(s);
        }
    }
    selected_hole_indices = std::move(new_selected);
}

void SlaDrainHolesEditing::move_hole(size_t idx, const Domain::Vec3d& mesh_pos, const Domain::Vec3d& mesh_normal)
{
    if (idx < holes.size()) {
        holes[idx].pos = mesh_pos.cast<float>();
        holes[idx].normal = mesh_normal.cast<float>();
    }
}

void SlaDrainHolesEditing::select_hole(size_t idx, bool add_to_selection)
{
    if (idx >= holes.size()) {
        return;
    }
    if (!add_to_selection) {
        selected_hole_indices.clear();
    }
    selected_hole_indices.insert(idx);
}

void SlaDrainHolesEditing::deselect_hole(size_t idx)
{
    selected_hole_indices.erase(idx);
}

void SlaDrainHolesEditing::toggle_hole(size_t idx)
{
    if (idx >= holes.size()) {
        return;
    }
    if (selected_hole_indices.count(idx)) {
        selected_hole_indices.erase(idx);
    } else {
        selected_hole_indices.insert(idx);
    }
}

void SlaDrainHolesEditing::select_all_holes()
{
    selected_hole_indices.clear();
    for (size_t i = 0; i < holes.size(); ++i) {
        selected_hole_indices.insert(i);
    }
}

void SlaDrainHolesEditing::clear_selection()
{
    selected_hole_indices.clear();
}

void SlaDrainHolesEditing::delete_selected_holes()
{
    if (selected_hole_indices.empty()) {
        return;
    }

    std::vector<size_t> indices_to_delete(selected_hole_indices.begin(),
                                           selected_hole_indices.end());
    std::sort(indices_to_delete.rbegin(), indices_to_delete.rend());

    for (size_t idx : indices_to_delete) {
        if (idx < holes.size()) {
            holes.erase(holes.begin() + idx);
        }
    }

    selected_hole_indices.clear();
}

void SlaDrainHolesEditing::apply_radius_to_selected()
{
    const float new_radius = static_cast<float>(hole_radius_mm);
    for (size_t idx : selected_hole_indices) {
        if (idx < holes.size()) {
            holes[idx].radius = new_radius;
        }
    }
}

void SlaDrainHolesEditing::apply_height_to_selected()
{
    const float new_height = static_cast<float>(hole_height_mm);
    for (size_t idx : selected_hole_indices) {
        if (idx < holes.size()) {
            holes[idx].height = new_height;
        }
    }
}

} // namespace Slic3r::App::Plater