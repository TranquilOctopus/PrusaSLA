#include "Slic3r/App/Plater/SlaSupportToolShortcuts.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

#include <fmt/format.h>

#include <utility>

namespace Slic3r::App::Plater {

SupportToolAction support_tool_action_for(const SupportToolKeyEvent& key)
{
    if (!key.tool_active || key.text_field_focus) {
        return SupportToolAction::None;
    }

    // Ctrl+A has been selecting all the points since M2.3b. Any other Ctrl or Alt combination is
    // the app's, not the tool's, so that the shortcuts of the sidebar and of the window keep it.
    if (key.code == Platform::KeyCode::A && Platform::ctrl_down(key.modifiers)) {
        return SupportToolAction::SelectAllPoints;
    }
    if (Platform::ctrl_down(key.modifiers) || Platform::alt_down(key.modifiers)) {
        return SupportToolAction::None;
    }

    // The digits are the presets whatever the shift, because on some layouts a digit is only
    // reachable with one.
    switch (key.code) {
    case Platform::KeyCode::Num1:
        return SupportToolAction::PresetMini;
    case Platform::KeyCode::Num2:
        return SupportToolAction::PresetLight;
    case Platform::KeyCode::Num3:
        return SupportToolAction::PresetMedium;
    case Platform::KeyCode::Num4:
        return SupportToolAction::PresetHeavy;
    case Platform::KeyCode::Delete:
    case Platform::KeyCode::Backspace:
        return SupportToolAction::DeleteSelectedPoints;
    case Platform::KeyCode::A:
        return Platform::shift_down(key.modifiers) ?
            SupportToolAction::AutoSupportAll :
            SupportToolAction::AutoSupportSelection;
    case Platform::KeyCode::G:
        // The switch is written on points, so with nothing selected there is nothing to write.
        return key.has_selection ?
            SupportToolAction::ToggleSupportOnModel :
            SupportToolAction::None;
    case Platform::KeyCode::Escape:
        // Escape drops the selection. With nothing selected it is not the tool's key: it closes the
        // tool, the way Escape closes every other tool.
        return key.has_selection ? SupportToolAction::ClearSelection : SupportToolAction::None;
    default:
        return SupportToolAction::None;
    }
}

SupportOnModel support_tool_toggled_on_model(const std::optional<SupportOnModel>& selected_on_model)
{
    if (!selected_on_model.has_value()) {
        // A mixed selection has no side of the switch to toggle from, so the toggle gives the
        // points a switch of their own, leaving the decision to the object as Inherit does.
        return SupportOnModel::Inherit;
    }
    return *selected_on_model == SupportOnModel::Forbid ?
        SupportOnModel::Allow :
        SupportOnModel::Forbid;
}

std::vector<std::string> support_tool_shortcut_lines()
{
    // The key is not translated, only what it does is. The order is the order of the section: the
    // presets, then the two auto support keys, then the keys of the selection.
    static const std::vector<std::pair<std::string, std::string>> lines{
        {Biz::L("1"), Biz::_u8L("Mini preset for new supports")},
        {Biz::L("2"), Biz::_u8L("Light preset for new supports")},
        {Biz::L("3"), Biz::_u8L("Medium preset for new supports")},
        {Biz::L("4"), Biz::_u8L("Heavy preset for new supports")},
        {Biz::L("A"), Biz::_u8L("Auto support the selected model")},
        {Biz::L("Shift+A"), Biz::_u8L("Auto support all models")},
        {Biz::L("G"), Biz::_u8L("Toggle Support on model of the selection")},
        {Biz::L("Esc"), Biz::_u8L("Clear the selection")},
        {Biz::L("Ctrl+A"), Biz::_u8L("Select all support points")},
        {Biz::L("Delete"), Biz::_u8L("Delete the selected support points")},
    };

    std::vector<std::string> result;
    result.reserve(lines.size());
    for (const auto& [keys, description] : lines) {
        result.push_back(fmt::format("{}: {}", keys, description));
    }
    return result;
}

} // namespace Slic3r::App::Plater
