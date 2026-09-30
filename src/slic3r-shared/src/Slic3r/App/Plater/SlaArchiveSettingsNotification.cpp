#include "Slic3r/App/Plater/SlaArchiveSettingsNotification.hpp"

#include "Slic3r/App/Plater/SlaArchiveSettings.hpp"

#include "Slic3r/Biz/FileLoadingLogic.hpp"
#include "Slic3r/App/PopNotification/PopNotificationObservableList.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/ModelInstance.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/ModelVolume.hpp"

#include <optional>
#include <string>
#include <utility>

namespace Slic3r::App::Plater {

using namespace Slic3r::App::PopNotification;

SlaArchiveSettingsNotification::SlaArchiveSettingsNotification(
    Biz::ProjectInteractor& project_interactor,
    OpenReviewFn open_review,
    PopNotification::PopNotificationCenter& notify
) :
    m_project_interactor(project_interactor),
    m_open_review(std::move(open_review)),
    m_notify(notify),
    m_importer(project_interactor)
{
    m_project_interactor.scene_interactor().add_listener<Biz::Scene::ISceneChangedListener>(this);
}

SlaArchiveSettingsNotification::~SlaArchiveSettingsNotification()
{
    m_project_interactor.scene_interactor().remove_listener<Biz::Scene::ISceneChangedListener>(this
    );
}

boost::filesystem::path SlaArchiveSettingsNotification::
    archive_of(Domain::SelectionId project_id, const Domain::ElementRefs& instances)
{
    const auto& objects = m_project_interactor.project(project_id).model().objects;
    for (const Domain::ElementRef& ref : instances) {
        for (const auto& object : objects) {
            if (object->id().id != ref.object_id)
                continue;
            for (const auto& volume : object->volumes) {
                const std::string& input_file{volume->source.input_file};
                if (!input_file.empty() && Biz::FileLoadingLogic::is_sla_archive_file(input_file))
                    return boost::filesystem::path{input_file};
            }
        }
    }
    return {};
}

void SlaArchiveSettingsNotification::
    on_instance_added(Domain::SelectionId project_id, const Domain::ElementRefs& instances)
{
    if (instances.empty() || project_id != m_project_interactor.selected_project_id())
        return;

    boost::filesystem::path archive = archive_of(project_id, instances);
    if (archive.empty())
        return;
    if (!m_offered_archives.insert(archive).second)
        return;

    // The dry run is exactly the work the review dialog does when it opens, down to the base resin
    // it would offer, so what is named below is what that dialog writes. Nothing is saved.
    const Biz::ResinProfile::ResinImportResult result = m_importer.import_file(
        archive,
        Biz::ResinProfile::ResinImportTarget{
            .project_id          = project_id,
            .config_container_id = m_project_interactor.selected_config_container_id(),
            .material_slot       = 0
        },
        /*dry_run=*/true
    );

    SlaArchiveSettings settings = analyze_archive_settings(result);
    if (!settings.has_material()) {
        // Nothing to review: the archive names no exposure and no layer height, or the target is
        // not an SLA printer. The model stays on the plate and the presets stay as they were.
        return;
    }

    const boost::filesystem::path to_review{archive};
    std::optional<PopNotificationData> data = build_archive_settings_notification(
        settings,
        project_id,
        [this, to_review]()
        {
            m_open_review(to_review);
            return true;
        }
    );
    if (!data)
        return;

    // A new notification every time: two archives imported one after the other each get their own
    // offer, and neither replaces the other.
    m_notify.upsert_notification(std::move(*data), never_equal_matcher);
}

} // namespace Slic3r::App::Plater
