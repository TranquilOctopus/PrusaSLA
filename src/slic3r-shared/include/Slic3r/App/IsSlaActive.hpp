#pragma once

#include "Slic3r/Biz/ProjectInteractor.hpp"

namespace Slic3r::App {

// Whether the selected printer is an SLA printer. Safe to call before any project or
// printer is selected (returns false then), unlike selected_config_container().
inline bool is_sla_active(const Biz::ProjectInteractor& project_interactor)
{
    if (!project_interactor.project_exists(project_interactor.selected_project_id())
        || project_interactor.selected_config_container_id() == Domain::INVALID_ID)
        return false;
    return project_interactor.selected_config_container().print_technology()
        == Domain::PrinterTechnology::SLA;
}

} // namespace Slic3r::App