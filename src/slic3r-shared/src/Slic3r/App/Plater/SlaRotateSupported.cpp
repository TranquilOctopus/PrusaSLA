#include "Slic3r/App/Plater/SlaRotateSupported.hpp"

#include <fmt/format.h>

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/ObjectID.hpp"
#include "Slic3r/App/IsSlaActive.hpp"
#include "Slic3r/App/Plater/SlaSupportPointsClear.hpp"

using Slic3r::Biz::_u8L;
using Slic3r::Domain::ElementRef;
using Slic3r::Domain::ModelObject;
using Slic3r::Domain::ObjectID;

namespace Slic3r::App::Plater {

SlaRotateSupportedCheck
sla_rotate_supported_check(Biz::ProjectInteractor& project_interactor)
{
    SlaRotateSupportedCheck check;

    const auto& selection = project_interactor.scene_interactor().object_selection();
    const Domain::Project& project = project_interactor.workbench().project(
        project_interactor.selected_project_id()
    );

    for (const ElementRef& element : selection.elements) {
        if (!element.has_object()) {
            continue;
        }
        const ModelObject* object = project.find_object_by_id(element.object_id);
        if (object && !object->sla_support_points.empty()) {
            check.objects_with_points.push_back(object);
            check.point_count += object->sla_support_points.size();
        }
    }

    return check;
}

std::string sla_rotate_supported_question(const SlaRotateSupportedCheck& check)
{
    if (check.empty()) {
        return {};
    }

    const std::size_t models = check.objects_with_points.size();
    if (models == 1) {
        const ModelObject* object = check.objects_with_points.front();
        return fmt::format(
            fmt::runtime(_u8L("This part has supports. Rotating it means it has to be supported again, so its supports will be removed. Rotate and remove the supports?")),
            object->name
        );
    }

    return fmt::format(
        fmt::runtime(_u8L("{} selected parts have supports. Rotating them means they have to be supported again, so their supports will be removed. Rotate and remove the supports?")),
        models
    );
}

void sla_rotate_supported_ask_then_apply(
    Biz::ProjectInteractor& project_interactor,
    const SlaRotateSupportedCheck& check,
    SlaRotateCallback rotation_callback
)
{
    // Only ask in SLA mode and when there are objects with supports
    if (!App::is_sla_active(project_interactor) || check.empty()) {
        rotation_callback();
        return;
    }

    const std::string question = sla_rotate_supported_question(check);
    if (question.empty()) {
        rotation_callback();
        return;
    }

    // The answer comes back later; the check names the objects it was made for.
    const std::vector<ObjectID> asked_for_object_ids = [&check]() {
        std::vector<ObjectID> ids;
        ids.reserve(check.objects_with_points.size());
        for (const ModelObject* obj : check.objects_with_points) {
            ids.push_back(obj->id());
        }
        return ids;
    }();

    AppServices::instance().dialog_manager().show_yesno_dialog(
        _u8L("Rotate supported part"),
        question,
        [rotation_callback = std::move(rotation_callback),
         project_interactor = &project_interactor,
         asked_for_object_ids = std::move(asked_for_object_ids)](bool answer) mutable
        {
            if (!answer) {
                return; // User said No: rotation is cancelled, supports stay
            }

            // Verify the selection still matches what we asked about (defensive).
            // The objects are identified by their IDs, which are stable.
            const auto& selection = project_interactor->scene_interactor().object_selection();
            bool still_matches = true;
            if (selection.elements.size() != asked_for_object_ids.size()) {
                still_matches = false;
            } else {
                for (std::size_t i = 0; i < selection.elements.size(); ++i) {
                    if (!selection.elements[i].has_object() ||
                        selection.elements[i].object_id != asked_for_object_ids[i].id) {
                        still_matches = false;
                        break;
                    }
                }
            }
            if (!still_matches) {
                // Selection changed; don't apply silently. The user can try again.
                return;
            }

            // Yes: apply the rotation, then clear the support points in the SAME undo step.
            // The rotation_callback should apply the transform and take its own snapshot.
            // We take the SlaSupportPointsClear snapshot first, so one undo brings back both.
            const SlaSupportPointsClearPlan plan = sla_support_points_clear_plan(check.objects_with_points);
            if (!plan.empty()) {
                project_interactor->undo_provider().take_snapshot(Biz::UndoSnapshotType::SlaSupportPointsClear);
                for (const ElementRef& object_ref : plan.object_refs) {
                    project_interactor->scene_interactor().modify_sla_support_points(
                        object_ref,
                        [](ModelObject& model_object)
                        {
                            model_object.sla_support_points.clear();
                            model_object.sla_points_status = Slic3r::Domain::SLA::PointsStatus::NoPoints;
                        }
                    );
                }
            }

            // Now apply the rotation (it will take its own snapshot, e.g. Rotate/SetRotation/PlaceOnFace).
            rotation_callback();
        }
    );
}

} // namespace Slic3r::App::Plater