#pragma once

#include "Slic3r/App/Plater/SlaSupportPointPick.hpp"
#include "Slic3r/Domain/ElementRef.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/Domain/Types.hpp"

#include <cstddef>
#include <optional>

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App::Plater {

/// @brief What a mouse event of the support points tool is, as far as the edit it asks for goes.
///
/// The tool reads three things of an event: which button went down, whether Ctrl or Shift is held,
/// and what the cursor is on. Whether the tool answers the event at all (a wheel over the canvas,
/// a move, a release) is decided before, since those are not clicks.
enum class SlaSupportClickButton
{
    None,
    Left,
    Right
};

/// @brief The modifier that changes what a click of the tool means: Ctrl+click removes the support
/// under the cursor, Shift+click adds it to the selection (or drops it), Shift+click on no point
/// starts a rectangle selection.
enum class SlaSupportClickModifier
{
    None,
    Ctrl,
    Shift
};

struct SlaSupportClick
{
    SlaSupportClickButton button{SlaSupportClickButton::None};
    SlaSupportClickModifier modifier{SlaSupportClickModifier::None};
};

/// @brief What one click of the tool asks for (M2.38). The three steps a person goes through with
/// the supports of a model are here, each of them one action: select a support and change it, remove
/// one support, and add a support on the model where it is drawn.
enum class SlaSupportClickAction
{
    /// The click is not for the tool (a wheel turn, a move, a release of something else).
    None,
    /// The cursor is on a support the lock on island supports keeps out of reach: the click is
    /// swallowed without changing anything, and the tool still owns the event.
    Ignored,
    /// Select this support, so its values are shown in the "Selected supports" group.
    SelectPoint,
    /// Shift+click on a support: add it to the selection, or take it out of it.
    TogglePoint,
    /// Ctrl+click or a right click on a support: take it off the model.
    DeletePoint,
    /// The cursor is on the drawn model where there is no support: put a new one there, with the
    /// values of the "New supports" group.
    AddPoint,
    /// The cursor is on nothing at all: drop the selection.
    ClearSelection,
    /// Shift+click where there is no support: start a rectangle selection.
    RectangleSelect,
};

/// @brief The answer of sla_support_click_action(): what to do and, where the action is about a
/// support, which one.
struct SlaSupportClickResult
{
    SlaSupportClickAction action{SlaSupportClickAction::None};
    /// The support the action is about, for the actions that are about one.
    std::optional<size_t> point_index;
    /// Where on the model the click landed, in the mesh frame of the object, for AddPoint.
    std::optional<Domain::Vec3d> surface_pos;
    /// Whether a drag of this support may start. Only a click on the marker of a point starts one:
    /// a click on the drawn tree of a point selects it and nothing else, or a click on a pillar
    /// would move a support by accident (M2.35).
    bool drag_allowed{false};
};

/// @brief What one click of the support points tool asks for, with no camera, no scene and no gizmo
/// in it (M2.38).
///
/// @param click                  Which button went down and with which modifier.
/// @param point_under_cursor     What the pick of M2.35 found under the cursor, nothing when the
/// cursor is on no marker and no drawn tree.
/// @param points                 The points of the session, for the island lock.
/// @param island_supports_locked Whether the lock of the tool is on.
/// @param surface_pos            Where the cursor landed on the drawn model, in the mesh frame,
/// nothing when the ray hit no volume at all.
SlaSupportClickResult sla_support_click_action(
    const SlaSupportClick& click,
    const std::optional<SlaSupportPointTarget>& point_under_cursor,
    const Domain::SLA::SupportPoints& points,
    bool island_supports_locked,
    const std::optional<Domain::Vec3d>& surface_pos
);

/// @brief The one write of the points an edit session of the tool holds (M2.38).
///
/// Every edit of a session - a support added on the model, one removed, one moved, a value of the
/// "Selected supports" group - reaches the ModelObject through here, so a support is on the model
/// the moment the user changes it, without an Apply, and the M2.21 support preview sees the change
/// and rebuilds the drawn tree. One undo snapshot is taken before it, by the caller, so the snapshot
/// is of the state the edit starts from.
void commit_sla_support_point_edits(
    Biz::ProjectInteractor& project_interactor,
    const Domain::ElementRef& object_ref,
    const Domain::SLA::SupportPoints& points
);

} // namespace Slic3r::App::Plater
