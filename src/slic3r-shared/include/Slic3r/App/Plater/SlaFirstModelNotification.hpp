#pragma once

#include <string>
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp" // ISceneChangedListener
#include "Slic3r/Biz/ISelectedProjectChangedListener.hpp"
#include "Slic3r/Biz/IProjectsChangedListener.hpp"
#include "Slic3r/App/PopNotification/PopNotificationCenter.hpp"

namespace Slic3r::App::Plater {

/**
 * @brief One-time hint shown when the first model lands on an SLA build plate.
 *
 * Supports are an explicit step in the support tool here, they are not added when slicing, so a
 * user coming from Chitubox or Lychee needs to be told what to do next. The hint is shown once per
 * app run and never comes back after the user closed it.
 */
class SlaFirstModelNotification final :
    public Biz::Scene::ISceneChangedListener,
    public Biz::ISelectedProjectChangedListener,
    public Biz::IProjectsChangedListener
{
public:
    SlaFirstModelNotification(
        Biz::ProjectInteractor& project_interactor,
        PopNotification::PopNotificationCenter& notify);

    ~SlaFirstModelNotification() final;

    /**
     * @brief Show the hint when the build plate of the selected SLA project was empty before.
     * @note Implementation of ISceneChangedListener interface
     */
    void on_instance_added(Domain::SelectionId project_id, const Domain::ElementRefs& instances) override;

    /**
     * @brief Drop a hint of the project we are leaving, it must not linger over another plate.
     * @note Implementation of ISelectedProjectChangedListener interface
     */
    void on_selected_project_changed(size_t index) override;

    /**
     * @brief Drop the hint of the removed project.
     * @note Implementation of IProjectsChangedListener interface
     */
    void on_project_will_be_removed(Domain::SelectionId project_id) override;

    void on_project_changed(Domain::SelectionId project_id) override;

private:
    void maybe_show_hint(Domain::SelectionId project_id, const Domain::ElementRefs& instances);
    /// Returns whether a notification was open (and therefore closed).
    bool close_notification_if_open();

    Biz::ProjectInteractor& m_project_interactor;
    PopNotification::PopNotificationCenter& m_notify;

    /// The hint has already been shown during this app run.
    bool m_shown{false};
    /// The user closed the hint, it must not come back during this app run.
    bool m_dismissed{false};
};

} // namespace Slic3r::App::Plater
