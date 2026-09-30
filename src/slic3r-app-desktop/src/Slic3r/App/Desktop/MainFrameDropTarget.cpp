#include "MainFrameDropTarget.hpp"

#include <boost/filesystem/path.hpp>
#include <Slic3r/App/AppServices.hpp>
#include <Slic3r/App/Navigator.hpp>
#include <Slic3r/App/PopNotification/PopNotificationCenter.hpp>
#include <Slic3r/App/PopNotification/PopNotificationData.hpp>
#include <Slic3r/App/PopNotification/PopNotificationLayout.hpp>
#include <Slic3r/App/PopNotification/PopNotificationObservableList.hpp>
#include <Slic3r/App/WX/StringConversions.hpp>

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include <Slic3r/Biz/FileLoadingLogic.hpp>
#include <Slic3r/Biz/I18N/I18N.hpp>
#include <Slic3r/Log.hpp>

#include <fmt/format.h>

#include <chrono>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r::App::Desktop {

namespace {

/// @brief Tell the user how many resin profiles of the drop were not imported, because the review
/// dialog is one profile at a time and the rest are still on their disk.
void notify_profiles_left_out(Biz::ProjectInteractor& project_interactor, std::size_t count)
{
    if (count == 0) {
        return;
    }
    using namespace std::chrono_literals;
    // TRN: Header of the notification about resin profiles that were dropped but not imported.
    const std::string header = Biz::_u8L("Some resin profiles were not imported");
    // TRN: Body of the notification about resin profiles that were dropped but not imported. {0} is
    // the number of profiles the drop held besides the one that was opened for review.
    const std::string body = fmt::format(
        fmt::runtime(
            Biz::_u8L("{} more resin profiles were not imported: drop them one at a time.")
        ),
        count
    );
    AppServices::instance().pop_notification_center().upsert_notification(
        {PopNotification::PopNotificationType::Custom,
         PopNotification::PopNotificationLevel::Warning,
         10s,
         PopNotification::PopNotificationLayoutHeaderText(header, body),
         {},
         project_interactor.selected_project_id()},
        PopNotification::never_equal_matcher
    );
}

} // namespace

MainFrameDropTarget::MainFrameDropTarget(
    Biz::ProjectInteractor& project_interactor,
    Navigator& navigator,
    std::function<bool()> can_accept
) :
    m_project_interactor(project_interactor),
    m_navigator(navigator),
    m_can_accept(std::move(can_accept))
{}

bool MainFrameDropTarget::OnDropFiles(wxCoord /*x*/, wxCoord /*y*/, const wxArrayString& filenames)
{
    if (!m_can_accept())
        return false;

    std::vector<boost::filesystem::path> dropped;
    dropped.reserve(filenames.size());
    for (const wxString& fn : filenames)
        dropped.emplace_back(WX::into_u8(fn));

    // A drop is one gesture and may carry both models and a resin profile, which go to two
    // different places: the models to the scene, the profile to the review dialog.
    Biz::FileLoadingLogic::DropRouting routing =
        Biz::FileLoadingLogic::route_dropped_files(dropped);

    const bool has_models = !routing.files_to_load.empty();
    if (has_models) {
        if (routing.files_to_load.size() == 1
            && Biz::FileLoadingLogic::is_project_file(routing.files_to_load.front().string()))
            m_project_interactor.load_project(routing.files_to_load.front());
        else {
            m_project_interactor.load_models_to_project(std::move(routing.files_to_load));
            m_navigator.navigate_to_module_type(App::Render::ModuleType::Plater);
        }
    }

    // The models are loaded first, so the review dialog is the last thing the drop leaves on the
    // screen and the user can go on reviewing with the models already on the bed.
    const bool has_profile = !routing.profile_to_review.empty();
    if (has_profile) {
        m_navigator.open_resin_import(routing.profile_to_review);
    }

    for (const boost::filesystem::path& path : routing.profiles_left_out) {
        SPDLOG_WARN(
            "Resin profile {} was dropped with {}, which was opened for review instead.",
            path.string(),
            routing.profile_to_review.string()
        );
    }
    notify_profiles_left_out(m_project_interactor, routing.profiles_left_out.size());

    return has_models || has_profile;
}

} // namespace Slic3r::App::Desktop
