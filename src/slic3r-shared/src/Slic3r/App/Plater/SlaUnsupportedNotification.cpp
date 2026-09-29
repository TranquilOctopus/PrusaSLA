#include "Slic3r/App/Plater/SlaUnsupportedNotification.hpp"

#include <Slic3r/App/AppServices.hpp>
#include <Slic3r/App/PopNotification/PopNotificationCenter.hpp>
#include <Slic3r/App/PopNotification/PopNotificationData.hpp>

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/ModelInstance.hpp"
#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/Biz/StatusCache.hpp"
#include "Slic3r/Biz/SLAObjectCache.hpp"
#include "libslic3r/SLAResult.hpp"

#include "fmt/format.h"

namespace Slic3r::App::Plater {
using namespace Slic3r;
using namespace Slic3r::App::PopNotification;
using namespace Slic3r::Biz;
using Slic3r::Biz::Slicing::StatusCode;
using Slic3r::Biz::Slicing::Sla::Object;

SlaUnsupportedNotification::SlaUnsupportedNotification(
    ProjectInteractor& project_interactor,
    PopNotification::PopNotificationCenter& notify) :
    m_project_interactor(project_interactor),
    m_notify(notify)
{
    m_project_interactor.sla_result_cache().add_listener<Biz::ISLAResultCacheChangedListener>(this);
    m_project_interactor.status_cache().add_listener<Biz::IStatusCacheChangedListener>(this);
    m_project_interactor.add_listener<Biz::ISelectedProjectChangedListener>(this);
    m_project_interactor.add_listener<Biz::IProjectsChangedListener>(this);
}

SlaUnsupportedNotification::~SlaUnsupportedNotification()
{
    close_notification_if_open();
    m_project_interactor.sla_result_cache().remove_listener<Biz::ISLAResultCacheChangedListener>(this);
    m_project_interactor.status_cache().remove_listener<Biz::IStatusCacheChangedListener>(this);
    m_project_interactor.remove_listener<Biz::ISelectedProjectChangedListener>(this);
    m_project_interactor.remove_listener<Biz::IProjectsChangedListener>(this);
}

void SlaUnsupportedNotification::on_sla_result_cache_changed(const Domain::SlicingId& id)
{
    // Only process results for the currently selected project
    Domain::SelectionId selected_project = m_project_interactor.selected_project_id();
    if (id.project_id != selected_project) {
        return;
    }

    std::optional<Biz::SLAResultRef> sla_result = m_project_interactor.sla_result_cache().get_result(id);
    if (!sla_result.has_value()) {
        close_notification_if_open();
        return;
    }
}

void SlaUnsupportedNotification::on_status_cache_status_code_changed(const Domain::SlicingId id)
{
    // Only process results for the currently selected project
    Domain::SelectionId selected_project = m_project_interactor.selected_project_id();
    if (id.project_id != selected_project) {
        return;
    }

    auto status_opt = m_project_interactor.status_cache().get_status(id);
    if (!status_opt.has_value()) {
        return;
    }

    const auto& status = *status_opt;
    if (status.code == StatusCode::Finished) {
        m_current_slicing_id = id;
        m_dismissed_projects.erase(selected_project);
        recreate_notification(selected_project, /*open_when_closed=*/true);
    } else if (status.code == StatusCode::Running ||
               status.code == StatusCode::Modified ||
               status.code == StatusCode::Updating ||
               status.code == StatusCode::InvalidData ||
               status.code == StatusCode::Empty) {
        if (m_current_slicing_id == id) {
            close_notification_if_open();
            m_current_slicing_id = Domain::SlicingId{};
        }
    }
}

void SlaUnsupportedNotification::on_selected_project_changed(size_t index)
{
    m_current_slicing_id = Domain::SlicingId{};
    recreate_notification(index, /*open_when_closed=*/true);
}

void SlaUnsupportedNotification::on_project_will_be_removed(Domain::SelectionId project_id)
{
    m_dismissed_projects.erase(project_id);
    close_notification_if_open();
    if (m_current_slicing_id.project_id == project_id) {
        m_current_slicing_id = Domain::SlicingId{};
    }
}

void SlaUnsupportedNotification::on_project_changed(Domain::SelectionId project_id)
{
    close_notification_if_open();
    if (m_current_slicing_id.project_id == project_id) {
        m_current_slicing_id = Domain::SlicingId{};
    }
}

std::vector<const Domain::ModelObject*> SlaUnsupportedNotification::collect_unsupported_objects(
    const Domain::SlicingId& slicing_id,
    const Domain::BedInstance& bed_instance,
    const Domain::Project& project)
{
    std::vector<const Domain::ModelObject*> unsupported_objects;
    for (const Domain::ModelInstance* instance : bed_instance.model_instances) {
        if (!instance || !instance->is_printable()) {
            continue;
        }
        const Domain::ModelObject* model_object = project.find_object_by_id(instance->get_object()->id().id);
        if (!model_object) {
            continue;
        }
        // Skip if already added
        if (std::find(unsupported_objects.begin(), unsupported_objects.end(), model_object) != unsupported_objects.end()) {
            continue;
        }
        // Check if the object has no support structure in the slice result
        const SLAObjectCache::Key key{slicing_id, model_object->id()};
        const SLAObjectOptRef opt_ref = m_project_interactor.sla_object_cache().get_instance(key);
        bool unsupported = !opt_ref.has_value() || !opt_ref->get().support_structure || opt_ref->get().support_structure->empty();
        if (unsupported) {
            unsupported_objects.push_back(model_object);
        }
    }
    return unsupported_objects;
}

void SlaUnsupportedNotification::recreate_notification(Domain::SelectionId project_id, bool open_when_closed)
{
    if (project_id != m_project_interactor.selected_project_id()) {
        return;
    }
    if (m_dismissed_projects.contains(project_id)) {
        open_when_closed = false;
    }

    std::optional<Biz::SLAResultRef> sla_result = m_project_interactor.sla_result_cache().get_result(m_current_slicing_id);
    if (!sla_result.has_value() || !sla_result->get().export_data) {
        return;
    }

    // Collect model objects on this bed that have printable instances but no support structure
    const Domain::Project& project = m_project_interactor.project(project_id);
    const Domain::BedInstance* bed_instance = project.find_bed_instance_by_id(m_current_slicing_id.bed_instance_id);
    if (!bed_instance) {
        return;
    }

    std::vector<const Domain::ModelObject*> unsupported_objects = collect_unsupported_objects(m_current_slicing_id, *bed_instance, project);

    if (unsupported_objects.empty()) {
        return;
    }

    bool was_open = close_notification_if_open();
    if (!open_when_closed && !was_open) {
        return;
    }

    // Build notification message
    std::string message = _u8L("Sliced without supports:") + " ";
    const size_t max_names = 5;
    for (size_t i = 0; i < unsupported_objects.size() && i < max_names; ++i) {
        if (i > 0) {
            message += ", ";
        }
        message += unsupported_objects[i]->name;
    }
    if (unsupported_objects.size() > max_names) {
        message += fmt::format(fmt::runtime(_u8L(", … and {0} more")), unsupported_objects.size() - max_names);
    }
    message += "\n";
    message += _u8L("Open the support tool to add supports, or ignore this if the model should sit on the build plate.");

    using namespace Slic3r::App::PopNotification;
    PopNotificationData data{
        .type = PopNotificationType::SlaUnsupportedDetected,
        .level = PopNotificationLevel::Warning,
        .timeout = 0s,
        .layout = PopNotificationLayoutText(message),
        .project_id = project_id,
        .on_user_close = [this, project_id]() { m_dismissed_projects.insert(project_id); }
    };
    auto matcher = [](const PopNotificationPayload&, const PopNotificationPayload&) { return false; };
    m_notify.upsert_notification(data, matcher);
}

bool SlaUnsupportedNotification::close_notification_if_open()
{
    auto& list = m_notify.observable_list();
    bool is_open = false;
    for (size_t i = 0; i < list.size(); i++) {
        if (list.at(i).type == PopNotificationType::SlaUnsupportedDetected) {
            is_open = true;
            break;
        }
    }
    if (!is_open) {
        return false;
    }
    list.close_notifications_of_type(PopNotificationType::SlaUnsupportedDetected);
    return true;
}

} // namespace Slic3r::App::Plater