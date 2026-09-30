#pragma once

#include "Slic3r/Biz/ProjectInteractor.hpp"

namespace Slic3r::App {

// The technology of the selected printer. Safe to call before any project or printer is selected
// (returns FFF then), unlike selected_config_container().
inline Domain::PrinterTechnology
selected_printer_technology(const Biz::ProjectInteractor& project_interactor)
{
    if (!project_interactor.project_exists(project_interactor.selected_project_id())
        || project_interactor.selected_config_container_id() == Domain::INVALID_ID)
        return Domain::PrinterTechnology::FFF;
    return project_interactor.selected_config_container().print_technology();
}

// Whether the selected printer is an SLA printer. Safe to call before any project or
// printer is selected (returns false then), unlike selected_config_container().
inline bool is_sla_active(const Biz::ProjectInteractor& project_interactor)
{
    return selected_printer_technology(project_interactor) == Domain::PrinterTechnology::SLA;
}

// The item of the selected resin (the first material of the selected printer preset), or nullptr
// when no resin is selected or the resin config box does not carry the key.
inline const Domain::ConfigItem*
selected_resin_item(const Biz::ProjectInteractor& project_interactor, const std::string& key)
{
    if (!is_sla_active(project_interactor))
        return nullptr;

    const auto& materials =
        project_interactor.preset_interactor().selected_printer_preset().materials;
    if (materials.empty())
        return nullptr;

    return materials.front().config_box().items.find(key);
}

// Whether the selected resin brings its own layer height (resin_layer_height > 0). That is the
// case for the community SLA bundle, where layer height is a property of the resin and not of
// the Supports & raft preset, so the print layer height must not be shown.
inline bool sla_resin_sets_layer_height(const Biz::ProjectInteractor& project_interactor)
{
    const Domain::ConfigItem* item = selected_resin_item(project_interactor, "resin_layer_height");
    return item && item->get<double>() > 0.;
}

// Whether the selected resin brings its own transition layer count (resin_faded_layers >= 0,
// -1 being the "unset" value). Just like the layer height, the transition layers are then a
// property of the resin and not of the Supports & raft preset, so the print faded_layers must
// not be shown.
inline bool sla_resin_sets_faded_layers(const Biz::ProjectInteractor& project_interactor)
{
    const Domain::ConfigItem* item = selected_resin_item(project_interactor, "resin_faded_layers");
    return item && item->get<int>() >= 0;
}

} // namespace Slic3r::App