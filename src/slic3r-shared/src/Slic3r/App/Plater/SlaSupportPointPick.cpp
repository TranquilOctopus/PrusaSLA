#include "Slic3r/App/Plater/SlaSupportPointPick.hpp"

#include "Slic3r/App/Scene/Camera.hpp"

#include <algorithm>
#include <limits>

namespace Slic3r::App::Plater {

namespace {

// A marker is grabbed a little wider than it is drawn, so the pointer does not have to be on the
// pixel of the glyph, and it keeps a click target a person can hit at any zoom level.
constexpr double click_radius_slack  = 1.5;
constexpr double min_click_radius_px = 6.;
constexpr double max_click_radius_px = 24.;

// The distance of a point from a segment on the screen, in pixels. A piece of the drawn tree is a
// segment and a click on any part of it (the head, the pillar, the foot where it meets the plate)
// is on the piece, so the distance to the segment is what is tested, not the distance to its ends.
double distance_to_segment(
    const Domain::Vec2d& point,
    const Domain::Vec2d& start,
    const Domain::Vec2d& end
)
{
    const Domain::Vec2d along = end - start;
    const double along_sq     = along.squaredNorm();
    if (along_sq <= 0.) {
        return (point - start).norm();
    }
    const double t = std::clamp((point - start).dot(along) / along_sq, 0., 1.);
    return (point - (start + along * t)).norm();
}

// Whether a candidate is a better pick than the best so far: the one nearer the cursor wins, and
// two of them at the same place are told apart by their depth, so the marker or the piece drawn in
// front is the one picked.
bool is_better_pick(
    double candidate_distance,
    double candidate_depth,
    double best_distance,
    double best_depth
)
{
    if (candidate_distance != best_distance) {
        return candidate_distance < best_distance;
    }
    return candidate_depth < best_depth;
}

} // namespace

double sla_support_point_click_radius_px(double drawn_radius_px)
{
    const double radius = drawn_radius_px * click_radius_slack;
    return std::clamp(radius, min_click_radius_px, max_click_radius_px);
}

std::optional<size_t> sla_support_point_marker_at(
    const std::vector<SlaSupportPointMarker>& markers,
    const Domain::Vec2d& cursor
)
{
    std::optional<size_t> picked;
    double best_distance = std::numeric_limits<double>::max();
    double best_depth    = std::numeric_limits<double>::max();

    for (size_t i = 0; i < markers.size(); ++i) {
        const double distance = (markers[i].screen_pos - cursor).norm();
        if (distance > sla_support_point_click_radius_px(markers[i].drawn_radius_px)) {
            continue;
        }
        if (picked.has_value()
            && !is_better_pick(distance, markers[i].depth_mm, best_distance, best_depth))
        {
            continue;
        }
        picked        = i;
        best_distance = distance;
        best_depth    = markers[i].depth_mm;
    }

    return picked;
}

std::optional<SlaSupportTreePart>
sla_support_tree_part_at(const std::vector<SlaSupportTreePart>& parts, const Domain::Vec2d& cursor)
{
    std::optional<SlaSupportTreePart> picked;
    double best_distance = std::numeric_limits<double>::max();
    double best_depth    = std::numeric_limits<double>::max();

    for (const SlaSupportTreePart& part : parts) {
        const double distance = distance_to_segment(cursor, part.screen_start, part.screen_end);
        const double radius   = sla_support_point_click_radius_px(part.drawn_radius_px);
        if (distance > radius + sla_support_tree_pick_slack_px) {
            continue;
        }
        if (picked.has_value()
            && !is_better_pick(distance, part.depth_mm, best_distance, best_depth))
        {
            continue;
        }
        picked        = part;
        best_distance = distance;
        best_depth    = part.depth_mm;
    }

    return picked;
}

std::optional<SlaSupportPointTarget> sla_support_point_click_target(
    const std::vector<SlaSupportPointMarker>& markers,
    const std::vector<SlaSupportTreePart>& parts,
    const Domain::Vec2d& cursor
)
{
    // The marker of a point is what the user aims at, so it is picked first: a click that is on a
    // marker and on the drawn tree of another point takes the marker.
    if (const std::optional<size_t> marker = sla_support_point_marker_at(markers, cursor);
        marker.has_value())
    {
        return SlaSupportPointTarget{*marker, true};
    }

    if (const std::optional<SlaSupportTreePart> part = sla_support_tree_part_at(parts, cursor);
        part.has_value())
    {
        return SlaSupportPointTarget{part->point_index, false};
    }

    return std::nullopt;
}

double sla_support_point_screen_radius(
    const Scene::Camera& camera,
    const Domain::Vec3d& world_pos,
    double radius_mm
)
{
    const Domain::Vec3d right   = camera.right().normalized();
    const Domain::Vec2d centre  = camera.project_to_screen_space(world_pos);
    const Domain::Vec2d on_edge = camera.project_to_screen_space(world_pos + right * radius_mm);
    return (on_edge - centre).norm();
}

} // namespace Slic3r::App::Plater
