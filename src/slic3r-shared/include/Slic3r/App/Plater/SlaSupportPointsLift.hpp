#pragma once

#include "Slic3r/Domain/Types.hpp"

namespace Slic3r::App::Plater {

/// What the support points tool asks the scene about the lift of the object it works on (M2.33).
///
/// The scene draws a model lifted only when the M2.21 support preview asked for a lift, which it
/// does for an object that has support points and supports on. The tool used to raycast and draw
/// its point glyphs with the support elevation of the object instead, so on a model with no points
/// every click tested a copy of the mesh 5 mm above the one that is drawn and missed it: no point
/// was added, none was selected, none could be dragged or removed.
enum class SlaSupportPointsLiftAction
{
    /// The scene already draws the object the way the tool needs it, nothing to ask for.
    None,
    /// The tool lifts the object itself while it is open, the way Chitubox raises the model while
    /// the supports are being edited. No jump happens when the first point is added, because by then
    /// the preview service lifts the model by the same elevation and the two agree.
    Take,
    /// The tool hands the lift it took back, so a model with no support points falls onto the plate
    /// again. An object the preview service lifts keeps the service's own lift.
    GiveBack
};

/// The lift the scene draws the object of the tool with, and who is holding it.
struct SlaSupportPointsLiftDecision
{
    SlaSupportPointsLiftAction action{SlaSupportPointsLiftAction::None};
    /// The lift the scene draws the object with after the action, in mm.
    double lift{0.};
    /// Whether the tool is the one holding this lift, and so the only one that gives it back.
    bool tool_owns_lift{false};
};

/// What the tool does about the lift of its object, worked out from what the scene is doing with it.
///
/// @p tool_open is whether the tool is open on an object, @p scene_lift what
/// PlaterScenePresenter::sla_lift() answers for it (the lift the model is drawn with) and
/// @p support_elevation what sla::support_tool_elevation() asks for the object (what the support
/// tree needs, and what a lift is for). @p tool_owns_lift is what the previous call answered, so
/// only the tool gives its own lift back and a lift of the preview service is never taken away.
///
/// An elevation of zero keeps working: the model sits on the plate, the scene draws it there and the
/// raycast tests it there.
SlaSupportPointsLiftDecision sla_support_points_lift(
    bool tool_open,
    double scene_lift,
    double support_elevation,
    bool tool_owns_lift
);

/// The transform every part of the tool uses for the object: the one the scene draws the model with,
/// so a ray hit on the drawn surface is a hit on this transform and a point glyph sits on the model
/// as it is drawn (M2.33). A lift of zero leaves the instance transform as it is.
Domain::Transform3d
sla_support_points_drawing_trafo(const Domain::Transform3d& instance_trafo, double applied_lift);

/// The position of a support point a ray hit of the drawn model gives, in the object's mesh frame,
/// where the support points are stored. The lift is not part of it: it moved the mesh, not the point
/// on it.
Domain::Vec3d sla_support_points_hit_position(
    const Domain::Transform3d& volume_trafo,
    const Domain::Vec3d& volume_hit_position
);

} // namespace Slic3r::App::Plater
