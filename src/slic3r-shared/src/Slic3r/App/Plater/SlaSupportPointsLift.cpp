#include "Slic3r/App/Plater/SlaSupportPointsLift.hpp"

#include "Slic3r/Domain/Transformation.hpp"

namespace Slic3r::App::Plater {

SlaSupportPointsLiftDecision sla_support_points_lift(
    bool tool_open,
    double scene_lift,
    double support_elevation,
    bool tool_owns_lift
)
{
    SlaSupportPointsLiftDecision decision;

    if (!tool_open) {
        // The tool is closed. It hands back the lift it took and nothing else: a model the M2.21
        // support preview lifts has its own lift, which stays.
        if (tool_owns_lift) {
            decision.action         = SlaSupportPointsLiftAction::GiveBack;
            decision.lift           = 0.;
            decision.tool_owns_lift = false;
            return decision;
        }
        decision.action = SlaSupportPointsLiftAction::None;
        decision.lift   = scene_lift;
        return decision;
    }

    if (support_elevation == 0.) {
        // Nothing to stand on: the model sits on the plate, so it is drawn there and raycast there.
        if (tool_owns_lift && scene_lift != 0.) {
            decision.action         = SlaSupportPointsLiftAction::GiveBack;
            decision.lift           = 0.;
            decision.tool_owns_lift = false;
            return decision;
        }
        decision.action = SlaSupportPointsLiftAction::None;
        decision.lift   = 0.;
        return decision;
    }

    if (scene_lift == support_elevation) {
        // The scene already draws the model by exactly this elevation, which is the case once the
        // model has points and the preview service lifts it. The service holds this lift, so the
        // tool never gives it back.
        decision.action = SlaSupportPointsLiftAction::None;
        decision.lift   = scene_lift;
        return decision;
    }

    if (scene_lift == 0. || tool_owns_lift) {
        // The model is drawn on the plate, or by a lift of the tool that an elevation change has
        // made stale: the tool raises it by the elevation the support tree needs, the way Chitubox
        // raises the model while the supports are edited. The same elevation as the service uses,
        // so there is no jump when the first point is added.
        decision.action         = SlaSupportPointsLiftAction::Take;
        decision.lift           = support_elevation;
        decision.tool_owns_lift = true;
        return decision;
    }

    // The scene lifts the model by something else (a model whose own settings ask for another
    // elevation). The tool works with the lift the scene draws with and never takes it away.
    decision.action = SlaSupportPointsLiftAction::None;
    decision.lift   = scene_lift;
    return decision;
}

Domain::Transform3d
sla_support_points_drawing_trafo(const Domain::Transform3d& instance_trafo, double applied_lift)
{
    if (applied_lift == 0.) {
        return instance_trafo;
    }
    return Domain::translation_transform(Domain::Vec3d(0., 0., applied_lift)) * instance_trafo;
}

Domain::Vec3d sla_support_points_hit_position(
    const Domain::Transform3d& volume_trafo,
    const Domain::Vec3d& volume_hit_position
)
{
    return volume_trafo * volume_hit_position;
}

} // namespace Slic3r::App::Plater
