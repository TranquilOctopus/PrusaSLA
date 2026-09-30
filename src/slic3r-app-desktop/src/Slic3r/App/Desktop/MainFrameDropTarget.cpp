#include "MainFrameDropTarget.hpp"

#include <boost/filesystem/path.hpp>
#include "Slic3r/App/Navigator.hpp"
#include <Slic3r/App/WX/StringConversions.hpp>

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include <Slic3r/Biz/FileLoadingLogic.hpp>
#include <Slic3r/Log.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace Slic3r::App::Desktop {

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

    std::vector<boost::filesystem::path> resin_profiles;
    std::vector<boost::filesystem::path> paths;
    resin_profiles.reserve(filenames.size());
    paths.reserve(filenames.size());
    for (const wxString& fn : filenames) {
        boost::filesystem::path p{WX::into_u8(fn)};
        if (Biz::FileLoadingLogic::is_resin_profile_file(p.string()))
            resin_profiles.push_back(p);
        else if (Biz::FileLoadingLogic::is_supported_file(p.string()))
            paths.push_back(std::move(p));
    }

    if (!resin_profiles.empty()) {
        // A resin profile is not a model: it goes to the review dialog of the resin import, not to
        // the scene. The dialog reviews one profile, so the first one of the drop is opened and the
        // rest of the drop is written to the log. A model dropped together with a profile is not
        // loaded while that modal dialog is up.
        m_navigator.open_resin_import(resin_profiles.front());
        for (std::size_t i = 1; i < resin_profiles.size(); ++i) {
            SPDLOG_WARN(
                "Resin profile {} was dropped together with {}, the one opened for review.",
                resin_profiles[i].string(),
                resin_profiles.front().string()
            );
        }
        for (const boost::filesystem::path& path : paths) {
            SPDLOG_WARN("{} was dropped with a resin profile and was not loaded.", path.string());
        }
        return true;
    }

    if (paths.empty())
        return false;

    if (paths.size() == 1 && Biz::FileLoadingLogic::is_project_file(paths.front().string()))
        m_project_interactor.load_project(paths.front());
    else {
        m_project_interactor.load_models_to_project(std::move(paths));
        m_navigator.navigate_to_module_type(App::Render::ModuleType::Plater);
    }

    return true;
}

} // namespace Slic3r::App::Desktop
