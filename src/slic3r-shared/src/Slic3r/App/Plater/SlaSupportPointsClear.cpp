#include "Slic3r/App/Plater/SlaSupportPointsClear.hpp"

#include <fmt/format.h>

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/IUndoProvider.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"

using Slic3r::Biz::_u8L;
using Slic3r::Biz::UndoSnapshotType;
using Slic3r::Domain::ElementRef;
using Slic3r::Domain::ModelObject;
using Slic3r::Domain::SLA::PointsStatus;

namespace Slic3r::App::Plater {

SlaSupportPointsClearPlan
sla_support_points_clear_plan(const std::vector<const ModelObject*>& models)
{
    SlaSupportPointsClearPlan plan;

    plan.object_refs.reserve(models.size());
    for (const ModelObject* model : models) {
        // A model without points has nothing to remove, so it is not part of what the action does.
        if (model == nullptr || model->sla_support_points.empty()) {
            continue;
        }
        plan.object_refs.emplace_back(model->id().id);
        plan.point_count += model->sla_support_points.size();
    }

    return plan;
}

std::string sla_support_points_clear_question(const SlaSupportPointsClearPlan& plan)
{
    if (plan.empty()) {
        return {};
    }

    const std::size_t models = plan.object_refs.size();
    if (models == 1) {
        return fmt::format(
            fmt::runtime(
                _u8L("Remove the {} support points of this model? One undo brings them back.")
            ),
            plan.point_count
        );
    }

    return fmt::format(
        fmt::runtime(_u8L("Remove {} support points of {} models? One undo brings them back.")),
        plan.point_count,
        models
    );
}

std::size_t clear_sla_support_points(
    Biz::ProjectInteractor& project_interactor,
    const SlaSupportPointsClearPlan& plan
)
{
    if (plan.empty()) {
        return 0;
    }

    // One snapshot for the whole action, taken before the first model is touched: the Preview
    // sidebar can remove the points of every model of the build plate at once, and that is one user
    // action, so one undo brings all of them back (M2.32).
    project_interactor.undo_provider().take_snapshot(UndoSnapshotType::SlaSupportPointsClear);

    for (const ElementRef& object_ref : plan.object_refs) {
        project_interactor.scene_interactor().modify_sla_support_points(
            object_ref,
            [](ModelObject& model_object)
            {
                model_object.sla_support_points.clear();
                model_object.sla_points_status = PointsStatus::NoPoints;
            }
        );
    }

    return plan.object_refs.size();
}

} // namespace Slic3r::App::Plater
