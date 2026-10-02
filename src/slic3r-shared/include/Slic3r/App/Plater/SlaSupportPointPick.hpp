#pragma once

#include "Slic3r/Domain/ElementRef.hpp"
#include "Slic3r/Domain/Types.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace Slic3r::App::Scene {
class Camera;
} // namespace Slic3r::App::Scene

namespace Slic3r::App::Plater {

/// @brief One support point marker of the support points tool, as it is on the screen.
///
/// The marker is the glyph the tool draws at a support point. A click used to be tested against the
/// surface of the model alone (a raycast, then the nearest point within two tip diameters of the hit),
/// which made a point under an overhang unreachable without looking at it from below, and made a
/// click miss a marker that stands in front of the surface (M2.35). The markers are picked on the
/// screen instead, by the size they are drawn with.
struct SlaSupportPointMarker
{
    /// Where the marker is drawn, in screen pixels.
    Domain::Vec2d screen_pos{Domain::Vec2d::Zero()};
    /// How large the marker is drawn, in screen pixels. The click radius comes from this and not
    /// from the tip diameter in mm, so a glyph is grabbed the same way at any zoom level.
    double drawn_radius_px{0.};
    /// How far the marker is from the camera, in mm. Two markers drawn on top of each other are
    /// told apart by this: the one in front is the one the user sees.
    double depth_mm{0.};
};

/// @brief One drawn piece of a support tree: the pillar of one support point, as a segment on the
/// screen, with the point it belongs to.
///
/// The M2.21 support preview draws the tree of a model as one merged mesh and without an AABB, on
/// purpose: the tree never steals a pick from the model and a single click keeps selecting the
/// object. A click that means to take a support has to find the piece of the tree it is on anyway,
/// so the pieces are described here as the segment each point's pillar runs along (from the head of
/// the point down to where the pillar stands), which is what a click on the drawn support is on
/// (M2.35).
struct SlaSupportTreePart
{
    /// The model this piece is drawn for. Its points belong to this object, so an index into them
    /// is an index into the points of this object.
    Domain::ElementRef object;
    /// The index of the support point this piece belongs to, in the points of the object above.
    size_t point_index{0};
    /// The two ends of the drawn piece on the screen: the head end and the base end.
    Domain::Vec2d screen_start{Domain::Vec2d::Zero()};
    Domain::Vec2d screen_end{Domain::Vec2d::Zero()};
    /// How thick the piece is drawn, in screen pixels.
    double drawn_radius_px{0.};
    /// How far the piece is from the camera, in mm, told by its nearest end (the head of the point).
    double depth_mm{0.};
};

/// @brief What a click of the tool is on: the support point under the cursor, and which of the two
/// things that are drawn for it the cursor is on.
struct SlaSupportPointTarget
{
    /// The index of the support point, in the points of the object it belongs to.
    size_t index{0};
    /// Whether the cursor is on the marker of the point. False when it is on the drawn tree of the
    /// point, which only selects it: a drag is started from the marker, so a click on the tree
    /// never moves a point by accident.
    bool from_marker{true};
};

/// @brief The radius in screen pixels around a marker that a click grabs: the size the marker is
/// drawn with, with a little slack so a small glyph is still easy to hit, and never smaller than a
/// comfortable click target or so large that a click takes a point nobody aimed at.
double sla_support_point_click_radius_px(double drawn_radius_px);

/// @brief Extra room around a drawn tree piece, so a pillar thinner than a pixel is still a
/// clickable thing.
constexpr double sla_support_tree_pick_slack_px = 3.;

/// @brief The marker under the cursor: the nearest one to it, and of two markers at the same place
/// the one nearer the camera, since that is the one drawn in front and the one the user aims at.
/// Nothing when the cursor is on no marker, which is what tells the tool to fall back to the surface
/// of the model.
std::optional<size_t> sla_support_point_marker_at(
    const std::vector<SlaSupportPointMarker>& markers,
    const Domain::Vec2d& cursor
);

/// @brief The drawn tree piece under the cursor: the nearest one to it, and of two pieces at the same
/// place the one nearer the camera. The piece carries the point it belongs to, so a click on a
/// drawn support maps back to a support point.
std::optional<SlaSupportTreePart>
sla_support_tree_part_at(const std::vector<SlaSupportTreePart>& parts, const Domain::Vec2d& cursor);

/// @brief What a click of the support points tool is on, the way the user clicks supports in
/// Chitubox: the marker of a point first, and the drawn tree of a point when no marker is there
/// (M2.35).
///
/// Nothing means the click hit no point at all, neither a marker nor a piece of the drawn tree, and
/// the tool falls back to what it did before: the surface of the model under the cursor, which is
/// where a new support point goes.
std::optional<SlaSupportPointTarget> sla_support_point_click_target(
    const std::vector<SlaSupportPointMarker>& markers,
    const std::vector<SlaSupportTreePart>& parts,
    const Domain::Vec2d& cursor
);

/// @brief How large a sphere of @p radius_mm around @p world_pos is drawn on the screen, in pixels.
///
/// Not a pure function: it needs the camera the tool is drawn in, which is why the picking above
/// takes the size of the markers as they are and stays testable.
double sla_support_point_screen_radius(
    const Scene::Camera& camera,
    const Domain::Vec3d& world_pos,
    double radius_mm
);

} // namespace Slic3r::App::Plater
