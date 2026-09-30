#pragma once

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/ResinProfile/ResinProfileImportInteractor.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/App/PopNotification/PopNotificationCenter.hpp"

#include <boost/filesystem/path.hpp>

#include <functional>
#include <set>

namespace Slic3r::App::Plater {

/**
 * @brief Offer the print settings of an SLA archive that was just imported.
 *
 * Importing a .sl1/.sl1s puts the model on the plate and nothing else: the archive also carries
 * the profile its layers were rendered for. This listens for the object an archive import adds and,
 * when that archive names at least one material setting, offers a notification whose button opens
 * the resin import review of the archive (M3.10). The user reads there what each value becomes and
 * saves them as a user resin preset. Nothing is written from here and the selected printer is
 * never touched, so the offer is the only thing that happens without asking.
 *
 * A drop of several archives gets one notification per archive, each button reviewing its own
 * archive: the review dialog is for one profile at a time, so this is what makes every archive of
 * a drop reachable.
 */
class SlaArchiveSettingsNotification final : public Biz::Scene::ISceneChangedListener
{
public:
    /// @brief Opens the resin import review of an archive. Supplied by the caller because the
    /// review dialog hangs off a dialog of the module's own sidebar.
    using OpenReviewFn = std::function<void(const boost::filesystem::path&)>;

    SlaArchiveSettingsNotification(
        Biz::ProjectInteractor& project_interactor,
        OpenReviewFn open_review,
        PopNotification::PopNotificationCenter& notify
    );

    ~SlaArchiveSettingsNotification() final;

    /**
     * @brief Offer the settings of the archive the added instances came from, if any.
     * @note Implementation of ISceneChangedListener interface
     */
    void on_instance_added(
        Domain::SelectionId project_id,
        const Domain::ElementRefs& instances
    ) override;

private:
    /// @brief The archive the given instances came from, or an empty path when none of them came
    /// from an archive.
    boost::filesystem::path
    archive_of(Domain::SelectionId project_id, const Domain::ElementRefs& instances);

    Biz::ProjectInteractor& m_project_interactor;
    OpenReviewFn m_open_review;
    PopNotification::PopNotificationCenter& m_notify;
    Biz::ResinProfile::ResinProfileImportInteractor m_importer;

    /// The archives already offered, so that a later scene change that touches them is quiet.
    std::set<boost::filesystem::path> m_offered_archives;
};

} // namespace Slic3r::App::Plater
