#pragma once

#include <memory>
#include <optional>
#include <string>

#include "Slic3r/App/IsSlaActive.hpp"
#include "Slic3r/Biz/ObjectSettingsObservableList.hpp"
#include "Slic3r/Biz/OverrideItem.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/SLA/RaftPreset.hpp"

namespace Slic3r::App {

// The enum value a config box holds for the given key, or nullopt when the box has no such item or
// the item is not an enum (an older preset that has no raft_type or no raft_infill at all).
template <typename T>
inline std::optional<T> raft_enum_of(const Domain::ConfigBox& config_box, const std::string& key)
{
    const Domain::ConfigItem* item = config_box.items.find(key);
    if (!item || !item->holds_alternative<Domain::EnumWrapper>())
        return std::nullopt;

    return item->get<T>();
}

// The per-object value of a raft enum, or nullopt when no object is selected (the list of object
// settings is empty then), when the objects do not carry the key, or when the key is not overridden
// for them (a disabled override keeps a stale value, so it is not what the object reads). The raft
// keys are overridable per object, so the object panel decides which of them applies from what the
// objects read, not from what the print preset holds.
template <typename T>
inline std::optional<T>
object_raft_enum_of(const Biz::ProjectInteractor& project_interactor, const std::string& key)
{
    if (!is_sla_active(project_interactor))
        return std::nullopt;

    const std::shared_ptr<Biz::ObjectSettingsObservableList> object_settings =
        project_interactor.preset_interactor()
            .object_settings_interactor()
            .object_observable_list()
            .lock();
    if (!object_settings)
        return std::nullopt;

    for (size_t index = 0; index < object_settings->size(); ++index) {
        const Biz::OverrideItem& item = object_settings->at(index);
        if (item.name != key
            || !item.overriden.value_or(false)
            || item.config_item == nullptr
            || !item.config_item->holds_alternative<Domain::EnumWrapper>())
            continue;

        return item.config_item->get<T>();
    }

    return std::nullopt;
}

// The raft type of the selected print preset, or nullopt when the printer is not an SLA printer
// or the config has no raft_type (an older preset, where pad_enable decides).
inline std::optional<Domain::sla::RaftType>
selected_raft_type(const Biz::ProjectInteractor& project_interactor)
{
    if (!is_sla_active(project_interactor))
        return std::nullopt;

    return raft_enum_of<Domain::sla::RaftType>(
        project_interactor.preset_interactor().selected_printer_preset().print.config_box(),
        "raft_type"
    );
}

// The raft infill pattern of the selected print preset, or nullopt when the printer is not an SLA
// printer or the config has no raft_infill (an older preset, where the raft is the solid slab).
inline std::optional<Domain::sla::RaftInfillType>
selected_raft_infill(const Biz::ProjectInteractor& project_interactor)
{
    if (!is_sla_active(project_interactor))
        return std::nullopt;

    return raft_enum_of<Domain::sla::RaftInfillType>(
        project_interactor.preset_interactor().selected_printer_preset().print.config_box(),
        "raft_infill"
    );
}

// Whether a raft setting is shown for the raft type and the raft infill pattern of the selected
// print preset. Settings that are not raft settings at all, and every raft setting when the preset
// has no raft_type or no raft_infill, are shown.
inline bool
raft_setting_visible(const Biz::ProjectInteractor& project_interactor, const std::string& key)
{
    if (!Domain::SLA::is_raft_setting(key))
        return true;

    const std::optional<Domain::sla::RaftType> raft_type = selected_raft_type(project_interactor);
    if (!raft_type)
        return true;

    // A preset without raft_infill has no pattern to shape, so the three knobs are shown like they
    // are for an older config that never knew about them.
    const std::optional<Domain::sla::RaftInfillType> raft_infill =
        selected_raft_infill(project_interactor);
    if (!raft_infill)
        return Domain::SLA::raft_type_uses_setting(*raft_type, key);

    return Domain::SLA::raft_uses_setting(*raft_type, *raft_infill, key);
}

// The same rule for the object settings panel, which lists the raft knobs an object may override:
// the raft type and the raft infill the selected objects read decide there, not the ones of the
// print preset. An object that overrides neither falls back to the print preset, so the panel
// agrees with the print panel until the user overrides the raft for the object.
inline bool raft_setting_visible_for_object(
    const Biz::ProjectInteractor& project_interactor,
    const std::string& key
)
{
    if (!Domain::SLA::is_raft_setting(key))
        return true;

    std::optional<Domain::sla::RaftType> raft_type =
        object_raft_enum_of<Domain::sla::RaftType>(project_interactor, "raft_type");
    if (!raft_type)
        raft_type = selected_raft_type(project_interactor);
    if (!raft_type)
        return true;

    std::optional<Domain::sla::RaftInfillType> raft_infill =
        object_raft_enum_of<Domain::sla::RaftInfillType>(project_interactor, "raft_infill");
    if (!raft_infill)
        raft_infill = selected_raft_infill(project_interactor);
    if (!raft_infill)
        return Domain::SLA::raft_type_uses_setting(*raft_type, key);

    return Domain::SLA::raft_uses_setting(*raft_type, *raft_infill, key);
}

} // namespace Slic3r::App
