#pragma once

#include "Slic3r/App/IsSlaActive.hpp"
#include "Slic3r/Biz/ObjectSettingsObservableList.hpp"
#include "Slic3r/Biz/OverrideItem.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/SLA/HollowingLatticeSettings.hpp"

#include <algorithm>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::App {

// The hollowing infill patterns in the order the combobox of the hollowing tool shows them, which
// is the order of hollowing_infill in the config: None first, so a combobox that has not been given
// an index yet shows the plain cavity.
inline const std::vector<Domain::sla::HollowingInfillType>& hollowing_infill_patterns()
{
    static const std::vector<Domain::sla::HollowingInfillType> patterns{
        Domain::sla::HollowingInfillType::None,
        Domain::sla::HollowingInfillType::Grid,
        Domain::sla::HollowingInfillType::Cubic,
    };
    return patterns;
}

// The pattern of a combobox index of hollowing_infill_patterns, or nullopt for an index that is not
// one of them (an empty combobox, which has no current index).
inline std::optional<Domain::sla::HollowingInfillType> hollowing_infill_of_index(int index)
{
    const std::vector<Domain::sla::HollowingInfillType>& patterns = hollowing_infill_patterns();
    if (index < 0 || static_cast<size_t>(index) >= patterns.size())
        return std::nullopt;

    return patterns[static_cast<size_t>(index)];
}

// The combobox index of a hollowing infill pattern, or 0 (None) for a value that is not one of the
// patterns, so a config box that carries something else shows the plain cavity instead of nothing.
inline int hollowing_infill_index(Domain::sla::HollowingInfillType infill)
{
    const std::vector<Domain::sla::HollowingInfillType>& patterns = hollowing_infill_patterns();
    const auto it = std::ranges::find(patterns, infill);

    return it == patterns.end() ? 0 : static_cast<int>(std::distance(patterns.begin(), it));
}

// The hollowing infill pattern the selected objects read, or nullopt when no object is selected
// (the list of object settings is empty then), when the objects do not carry the key, when the key
// is not overridden for them (a disabled override keeps a stale value, so it is not what the
// object reads) or when the print preset they fall back to carries none either (a config from
// before the key existed, which prints the plain cavity).
inline std::optional<Domain::sla::HollowingInfillType>
selected_hollowing_infill(const Biz::ProjectInteractor& project_interactor)
{
    if (!is_sla_active(project_interactor))
        return std::nullopt;

    const std::shared_ptr<Biz::ObjectSettingsObservableList> object_settings =
        project_interactor.preset_interactor()
            .object_settings_interactor()
            .object_observable_list()
            .lock();
    if (object_settings) {
        for (size_t index = 0; index < object_settings->size(); ++index) {
            const Biz::OverrideItem& item = object_settings->at(index);
            if (item.name != "hollowing_infill"
                || !item.overriden.value_or(false)
                || item.config_item == nullptr
                || !item.config_item->holds_alternative<Domain::EnumWrapper>())
                continue;

            return item.config_item->get<Domain::sla::HollowingInfillType>();
        }
    }

    // No object overrides the pattern, so what they read is what the print preset holds.
    const Domain::ConfigItem* item =
        project_interactor.preset_interactor().selected_printer_preset().print.config_box().items
            .find("hollowing_infill");
    if (item == nullptr || !item->holds_alternative<Domain::EnumWrapper>())
        return std::nullopt;

    return item->get<Domain::sla::HollowingInfillType>();
}

// Whether a hollowing infill setting is shown in the object settings panel: the pattern the
// selected objects read decides which of the two knobs are shown, the same way the pattern decides
// which ones the print panel shows. A key that is not a hollowing infill setting is shown whatever
// the pattern is, and so is every one of them when no pattern can be read at all (an older config
// that never knew the key).
inline bool hollowing_infill_setting_visible_for_object(
    const Biz::ProjectInteractor& project_interactor,
    const std::string& key
)
{
    if (!Domain::SLA::is_hollowing_infill_setting(key))
        return true;

    const std::optional<Domain::sla::HollowingInfillType> infill =
        selected_hollowing_infill(project_interactor);
    if (!infill)
        return true;

    return Domain::SLA::hollowing_infill_uses_setting(*infill, key);
}

} // namespace Slic3r::App