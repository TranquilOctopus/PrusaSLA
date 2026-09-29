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

// Whether the selected resin brings its own layer height (resin_layer_height > 0). That is the
// case for the community SLA bundle, where layer height is a property of the resin and not of
// the Supports & raft preset, so the print layer height must not be shown.
inline bool sla_resin_sets_layer_height(const Biz::ProjectInteractor& project_interactor)
{
    if (!is_sla_active(project_interactor))
        return false;

    const auto& materials =
        project_interactor.preset_interactor().selected_printer_preset().materials;
    if (materials.empty())
        return false;

    const Domain::ConfigItem* item =
        materials.front().config_box().items.find("resin_layer_height");
    return item && item->get<double>() > 0.;
}

} // namespace Slic3r::App