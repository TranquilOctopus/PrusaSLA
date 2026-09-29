#pragma once

#include <optional>
#include <string>

#include "Slic3r/App/IsSlaActive.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/SLA/RaftPreset.hpp"

namespace Slic3r::App {

// The raft type of the selected print preset, or nullopt when the printer is not an SLA printer
// or the config has no raft_type (an older preset, where pad_enable decides).
inline std::optional<Domain::sla::RaftType>
selected_raft_type(const Biz::ProjectInteractor& project_interactor)
{
    if (!is_sla_active(project_interactor))
        return std::nullopt;

    const Domain::ConfigItem* item =
        project_interactor.preset_interactor()
            .selected_printer_preset()
            .print.config_box()
            .items.find("raft_type");
    if (!item || !item->holds_alternative<Domain::EnumWrapper>())
        return std::nullopt;

    return item->get<Domain::sla::RaftType>();
}

// Whether a raft setting is shown for the currently selected raft type. Settings that are not
// raft settings at all, and every raft setting when the preset has no raft_type, are shown.
inline bool
raft_setting_visible(const Biz::ProjectInteractor& project_interactor, const std::string& key)
{
    if (!Domain::SLA::is_raft_setting(key))
        return true;

    const std::optional<Domain::sla::RaftType> raft_type = selected_raft_type(project_interactor);
    return !raft_type || Domain::SLA::raft_type_uses_setting(*raft_type, key);
}

} // namespace Slic3r::App
