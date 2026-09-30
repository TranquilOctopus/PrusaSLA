#include "Slic3r/App/Plater/SlaIssueNotification.hpp"

#include <Slic3r/App/AppServices.hpp>
#include <Slic3r/App/PopNotification/PopNotificationCenter.hpp>
#include <Slic3r/App/PopNotification/PopNotificationData.hpp>

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/App/Plater/SlaIssueAnalysis.hpp"
#include "Slic3r/Biz/StatusCache.hpp"

#include "fmt/format.h"

namespace Slic3r::App::Plater {
using namespace Slic3r;
using namespace Slic3r::App::PopNotification;
using namespace Slic3r::Biz;
using Slic3r::Biz::Slicing::StatusCode;

SlaIssueNotification::SlaIssueNotification(
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

SlaIssueNotification::~SlaIssueNotification()
{
    close_notification_if_open();
    m_project_interactor.sla_result_cache().remove_listener<Biz::ISLAResultCacheChangedListener>(this);
    m_project_interactor.status_cache().remove_listener<Biz::IStatusCacheChangedListener>(this);
    m_project_interactor.remove_listener<Biz::ISelectedProjectChangedListener>(this);
    m_project_interactor.remove_listener<Biz::IProjectsChangedListener>(this);
}

void SlaIssueNotification::on_sla_result_cache_changed(const Domain::SlicingId& id)
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

void SlaIssueNotification::on_status_cache_status_code_changed(const Domain::SlicingId id)
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

void SlaIssueNotification::on_selected_project_changed(size_t index)
{
    m_current_slicing_id = Domain::SlicingId{};
    recreate_notification(index, /*open_when_closed=*/true);
}

void SlaIssueNotification::on_project_will_be_removed(Domain::SelectionId project_id)
{
    m_dismissed_projects.erase(project_id);
    close_notification_if_open();
    if (m_current_slicing_id.project_id == project_id) {
        m_current_slicing_id = Domain::SlicingId{};
    }
}

void SlaIssueNotification::on_project_changed(Domain::SelectionId project_id)
{
    close_notification_if_open();
    if (m_current_slicing_id.project_id == project_id) {
        m_current_slicing_id = Domain::SlicingId{};
    }
}

void SlaIssueNotification::recreate_notification(Domain::SelectionId project_id, bool open_when_closed)
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

    const auto& issues = sla_result->get().export_data->issues;
    SlaIssueAnalysis info = analyze_sla_issues_for_notification(issues);

    if (info.empty()) {
        return;
    }

    bool was_open = close_notification_if_open();
    if (!open_when_closed && !was_open) {
        return;
    }

    using namespace Slic3r::App::PopNotification;
    PopNotificationData data{
        .type = PopNotificationType::SlaIssueDetected,
        .level = PopNotificationLevel::Warning,
        .timeout = 0s,
        .layout = PopNotificationLayoutText(info.message),
        .project_id = project_id,
        .on_user_close = [this, project_id]() { m_dismissed_projects.insert(project_id); }
    };
    auto matcher = [](const PopNotificationPayload&, const PopNotificationPayload&) { return false; };
    m_notify.upsert_notification(data, matcher);
}

bool SlaIssueNotification::close_notification_if_open()
{
    auto& list = m_notify.observable_list();
    bool is_open = false;
    for (size_t i = 0; i < list.size(); i++) {
        if (list.at(i).type == PopNotificationType::SlaIssueDetected) {
            is_open = true;
            break;
        }
    }
    if (!is_open) {
        return false;
    }
    list.close_notifications_of_type(PopNotificationType::SlaIssueDetected);
    return true;
}

} // namespace Slic3r::App::Plater
