#include "Slic3r/App/Plater/SlaFirstModelNotification.hpp"

#include "Slic3r/App/Hints/SlaHints.hpp"
#include "Slic3r/App/IsSlaActive.hpp"
#include "Slic3r/App/PopNotification/PopNotificationCenter.hpp"
#include "Slic3r/App/PopNotification/PopNotificationData.hpp"

#include <chrono>

namespace Slic3r::App::Plater {

using namespace Slic3r;
using namespace Slic3r::App::PopNotification;
using namespace std::chrono_literals;

namespace {
// Long enough to read the whole sentence, short enough not to cover the plater.
constexpr std::chrono::seconds hint_timeout{20s};
} // namespace

SlaFirstModelNotification::SlaFirstModelNotification(
    Biz::ProjectInteractor& project_interactor,
    PopNotification::PopNotificationCenter& notify) :
    m_project_interactor(project_interactor),
    m_notify(notify)
{
    m_project_interactor.scene_interactor().add_listener<Biz::Scene::ISceneChangedListener>(this);
    m_project_interactor.add_listener<Biz::ISelectedProjectChangedListener>(this);
    m_project_interactor.add_listener<Biz::IProjectsChangedListener>(this);
}

SlaFirstModelNotification::~SlaFirstModelNotification()
{
    close_notification_if_open();
    m_project_interactor.scene_interactor().remove_listener<Biz::Scene::ISceneChangedListener>(this);
    m_project_interactor.remove_listener<Biz::ISelectedProjectChangedListener>(this);
    m_project_interactor.remove_listener<Biz::IProjectsChangedListener>(this);
}

void SlaFirstModelNotification::on_instance_added(Domain::SelectionId project_id, const Domain::ElementRefs& instances)
{
    maybe_show_hint(project_id, instances);
}

void SlaFirstModelNotification::on_selected_project_changed(size_t index)
{
    (void)index;
    close_notification_if_open();
}

void SlaFirstModelNotification::on_project_will_be_removed(Domain::SelectionId project_id)
{
    (void)project_id;
    close_notification_if_open();
}

void SlaFirstModelNotification::on_project_changed(Domain::SelectionId project_id)
{
    (void)project_id;
    close_notification_if_open();
}

void SlaFirstModelNotification::maybe_show_hint(Domain::SelectionId project_id, const Domain::ElementRefs& instances)
{
    if (m_shown || m_dismissed) {
        return;
    }
    if (instances.empty() || project_id != m_project_interactor.selected_project_id()) {
        return;
    }
    if (!App::is_sla_active(m_project_interactor)) {
        return;
    }
    // Only the very first model of the plate is a first-run moment.
    if (m_project_interactor.project(project_id).model().objects.size() > instances.size()) {
        return;
    }

    m_shown = true;
    PopNotificationData data{
        .type = PopNotificationType::SlaHint,
        .level = PopNotificationLevel::Regular,
        .timeout = hint_timeout,
        .layout = PopNotificationLayoutText(Hints::first_model_on_sla_plate_hint().text),
        .project_id = project_id,
        .on_user_close = [this]() { m_dismissed = true; }
    };
    m_notify.upsert_notification(std::move(data), never_equal_matcher);
}

bool SlaFirstModelNotification::close_notification_if_open()
{
    auto& list = m_notify.observable_list();
    bool is_open = false;
    for (size_t i = 0; i < list.size(); i++) {
        if (list.at(i).type == PopNotificationType::SlaHint) {
            is_open = true;
            break;
        }
    }
    if (!is_open) {
        return false;
    }
    list.close_notifications_of_type(PopNotificationType::SlaHint);
    return true;
}

} // namespace Slic3r::App::Plater
