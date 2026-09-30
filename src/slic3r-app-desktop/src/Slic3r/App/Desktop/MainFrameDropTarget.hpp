#pragma once

#include <functional>

#include <wx/dnd.h>

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {
class Navigator;
} // namespace Slic3r::App

namespace Slic3r::App::Desktop {

/**
 * wxWidgets adapter that receives OS file drop events and routes them to the
 * appropriate ProjectInteractor method:
 *   - resin profile (.cfg, .cfgx, .lyr) → the resin import review dialog, which opens in
 *     whichever module is on the screen
 *   - single project file (.3mf)       → load_project()
 *   - everything else (model files)   → load_models_to_project()
 * Unsupported file types are silently filtered out.
 *
 * A drop that mixes models and resin profiles loads the models as any other drop of models is and
 * then opens the review dialog of the first profile, so the dialog is the last thing shown. The
 * profiles after the first one are named in a notification rather than loaded, because the
 * review dialog is one profile at a time.
 *
 * @param can_accept Predicate called on each drop; returning false ignores the
 *                   drop entirely (e.g. when a non-slicing tab is active).
 */
class MainFrameDropTarget : public wxFileDropTarget
{
public:
    MainFrameDropTarget(
        Biz::ProjectInteractor& project_interactor,
        Navigator& navigator,
        std::function<bool()> can_accept
    );
    bool OnDropFiles(wxCoord x, wxCoord y, const wxArrayString& filenames) override;

private:
    Biz::ProjectInteractor& m_project_interactor;
    Navigator& m_navigator;
    std::function<bool()> m_can_accept;
};

} // namespace Slic3r::App::Desktop
