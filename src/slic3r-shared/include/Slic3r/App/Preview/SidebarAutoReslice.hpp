#pragma once

#include "Slic3r/App/Yoga/Window.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/AppConfig.hpp"
#include "Slic3r/App/IsSlaActive.hpp"

namespace Slic3r::App::Yoga {
    class ToggleButton;
}

namespace Slic3r::Biz {
    class ProjectInteractor;
}

namespace Slic3r::App::Preview {

class SidebarAutoReslice : public Yoga::Window
{
public:
    SidebarAutoReslice(Biz::ProjectInteractor& project_interactor);

    bool is_enabled() const;
    void update_visibility();

public:
    Yoga::ToggleButton* m_auto_reslice_chb { nullptr };
    AppConfig& m_app_config{AppServices::instance().app_config()};
    Biz::ProjectInteractor& m_project_interactor;
};

} // namespace Slic3r::App::Preview
