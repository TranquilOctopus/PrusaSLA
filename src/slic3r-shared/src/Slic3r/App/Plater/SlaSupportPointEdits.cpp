#include "Slic3r/App/Plater/SlaSupportPointEdits.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"

namespace Slic3r::App::Plater {

namespace {

SlaSupportClickResult about_point(
    const std::optional<SlaSupportPointTarget>& point_under_cursor,
    SlaSupportClickAction action,
    bool drag_allowed = false
)
{
    SlaSupportClickResult result;
    result.action       = action;
    result.point_index  = point_under_cursor->index;
    result.drag_allowed = drag_allowed;
    return result;
}

SlaSupportClickResult ignored()
{
    SlaSupportClickResult result;
    result.action = SlaSupportClickAction::Ignored;
    return result;
}

} // namespace

SlaSupportClickResult sla_support_click_action(
    const SlaSupportClick& click,
    const std::optional<SlaSupportPointTarget>& point_under_cursor,
    const Domain::SLA::SupportPoints& points,
    bool island_supports_locked,
    const std::optional<Domain::Vec3d>& surface_pos
)
{
    if (click.button == SlaSupportClickButton::None) {
        return {};
    }

    // Whether the support under the cursor is one the lock keeps out of reach. An island point
    // stands on a piece of the model of its own and falls off the build without its support, so a
    // locked island is neither selected nor removed nor dragged.
    const bool locked = island_supports_locked
        && point_under_cursor.has_value()
        && point_under_cursor->index < points.size()
        && points[point_under_cursor->index].is_island();

    // A click on empty space is the app's own, not the tool's, unless it adds a support where the
    // model is drawn.
    if (!point_under_cursor.has_value()) {
        if (click.button == SlaSupportClickButton::Right
            || click.modifier == SlaSupportClickModifier::Ctrl)
        {
            return {};
        }
        if (click.modifier == SlaSupportClickModifier::Shift) {
            SlaSupportClickResult result;
            result.action = SlaSupportClickAction::RectangleSelect;
            return result;
        }
        if (surface_pos.has_value()) {
            SlaSupportClickResult result;
            result.action      = SlaSupportClickAction::AddPoint;
            result.surface_pos = surface_pos;
            return result;
        }
        SlaSupportClickResult result;
        result.action = SlaSupportClickAction::ClearSelection;
        return result;
    }

    // A support is under the cursor from here on.
    if (locked) {
        return ignored();
    }

    if (click.button == SlaSupportClickButton::Right
        || click.modifier == SlaSupportClickModifier::Ctrl)
    {
        // The right button and Ctrl+click both remove the support under the cursor and nothing else,
        // the way a right click removes a point in the paint tool.
        return about_point(point_under_cursor, SlaSupportClickAction::DeletePoint);
    }

    if (click.modifier == SlaSupportClickModifier::Shift) {
        // Shift+click adds the support to the selection or takes it out of it.
        return about_point(point_under_cursor, SlaSupportClickAction::TogglePoint);
    }

    // A plain left click selects the support, so the "Selected supports" group shows what it carries.
    // A drag starts only from the marker of the point (M2.35): a click on the drawn tree of a point
    // selects it and nothing else, or a click on a pillar would move a support by accident.
    return about_point(
        point_under_cursor,
        SlaSupportClickAction::SelectPoint,
        point_under_cursor->from_marker
    );
}

void commit_sla_support_point_edits(
    Biz::ProjectInteractor& project_interactor,
    const Domain::ElementRef& object_ref,
    const Domain::SLA::SupportPoints& points
)
{
    // PointsStatus::UserModified is what every edit of the tool leaves behind, and it is what the
    // M2.21 support preview watches for next to the points themselves.
    project_interactor.scene_interactor().modify_sla_support_points(
        object_ref,
        [&points](Domain::ModelObject& model_object)
        {
            model_object.sla_support_points = points;
            model_object.sla_points_status  = Domain::SLA::PointsStatus::UserModified;
        }
    );
}

} // namespace Slic3r::App::Plater
