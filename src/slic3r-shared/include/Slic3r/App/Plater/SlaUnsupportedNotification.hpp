#pragma once

#include <string>
#include <functional>
#include <set>
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/ISelectedProjectChangedListener.hpp"
#include "Slic3r/Biz/IProjectsChangedListener.hpp"
#include "Slic3r/Biz/SLAResultCache.hpp"
#include "Slic3r/Biz/StatusCache.hpp"
#include "Slic3r/App/PopNotification/PopNotificationCenter.hpp"

namespace Slic3r::App::Plater {

/**
 * @brief Manage notification about models sliced without supports.
 *
 * Listens to SLA result cache changes and shows a notification when a bed finishes
 * slicing and has model objects with instances on that bed but no support points.
 * The notification is closed when the project changes or a new slice starts.
 */
class SlaUnsupportedNotification final :
    public Biz::ISLAResultCacheChangedListener,
    public Biz::IStatusCacheChangedListener,
    public Biz::ISelectedProjectChangedListener,
    public Biz::IProjectsChangedListener
{
public:
    SlaUnsupportedNotification(
        Biz::ProjectInteractor& project_interactor,
        PopNotification::PopNotificationCenter& notify);

    ~SlaUnsupportedNotification() final;

    /**
     * @brief Called when SLA result cache changes for a slicing job.
     * Closes notification if the result disappears.
     */
    void on_sla_result_cache_changed(const Domain::SlicingId& id) override;

    /**
     * @brief Called when slicing status code changes.
     * Shows notification when slicing finishes, closes when a new slice starts.
     */
    void on_status_cache_status_code_changed(const Domain::SlicingId id) override;

    void on_status_cache_progress_changed(const Domain::SlicingId) override {}
    void on_status_cache_warnings_changed(const Domain::SlicingId) override {}
    void on_status_cache_errors_changed(const Domain::SlicingId) override {}

    /**
     * @brief Rebuild the notification for the newly active project.
     * @note Implementation of ISelectedProjectChangedListener interface
     */
    void on_selected_project_changed(size_t index) override;

    /**
     * @brief Drop tracked state for the removed project.
     * @note Implementation of IProjectsChangedListener interface
     */
    void on_project_will_be_removed(Domain::SelectionId project_id) override;

    /**
     * @brief Close notification when project changes (new slice or project switch).
     * @note Implementation of IProjectsChangedListener interface
     */
    void on_project_changed(Domain::SelectionId project_id) override;

private:
    void recreate_notification(Domain::SelectionId project_id, bool open_when_closed = false);
    /// Returns whether a notification was open (and therefore closed).
    bool close_notification_if_open();

    Biz::ProjectInteractor& m_project_interactor;
    PopNotification::PopNotificationCenter& m_notify;

    std::set<Domain::SelectionId> m_dismissed_projects;
    Domain::SlicingId m_current_slicing_id{};
};

} // namespace Slic3r::App::Plater