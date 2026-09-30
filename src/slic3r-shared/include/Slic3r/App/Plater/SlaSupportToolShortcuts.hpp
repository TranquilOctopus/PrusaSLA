#pragma once

#include "Slic3r/App/Plater/SlaSupportOnModel.hpp"
#include "Slic3r/App/Platform/KeyCode.hpp"
#include "Slic3r/App/Platform/KeyModifers.hpp"

#include <optional>
#include <string>
#include <vector>

namespace Slic3r::App::Plater {

/// What a key press asks the support points tool to do (M2.28). The presets on 1 to 4, auto support
/// on A, the per-point "Support on model" on G and the selection on Escape are the keys people who
/// come from Lychee or Chitubox expect; Delete and Ctrl+A, which the tool has answered to since
/// M2.3b, go through here as well, so that every key of the tool is in one place.
enum class SupportToolAction
{
    None, ///< The tool does not act on this key at all.
    PresetMini, ///< Take the Mini preset.
    PresetLight, ///< Take the Light preset.
    PresetMedium, ///< Take the Medium preset.
    PresetHeavy, ///< Take the Heavy preset.
    AutoSupportSelection, ///< Auto support the model the tool works on.
    AutoSupportAll, ///< Auto support every model of the project.
    ToggleSupportOnModel, ///< Flip the "Support on model" of the selected points.
    ClearSelection, ///< Drop the selection of support points.
    SelectAllPoints, ///< Select every support point of the model.
    DeleteSelectedPoints, ///< Delete the selected support points.
};

/// The key the user pressed, together with the state the tool would act in. The keys act only while
/// the tool is active and no text field has the focus, so typing a number into one of the tool's
/// own inputs does not pick a preset.
struct SupportToolKeyEvent
{
    Platform::KeyCode code           = Platform::KeyCode::None;
    Platform::KeyModifiers modifiers = 0;
    /// Whether the support points tool is the tool that is active.
    bool tool_active = false;
    /// Whether a text field has the focus, so its keys belong to the text.
    bool text_field_focus = false;
    /// Whether support points are selected, which the keys on G and Escape need.
    bool has_selection = false;
};

/// The action the key press of @p key asks for, SupportToolAction::None when there is none: the
/// tool is not active, a text field owns the keys, the combination belongs to the app, or the key is
/// not one of the tool's. Only key-down events are to be passed in, a key release asks for nothing.
SupportToolAction support_tool_action_for(const SupportToolKeyEvent& key);

/// The "Support on model" the G shortcut writes on the selected points: Forbid when the selection
/// may rest on the model, Allow when it may not, and Inherit when the selection is mixed, which has
/// no side of the switch to toggle from and so settles on the state of a point that carries no
/// switch of its own.
SupportOnModel
support_tool_toggled_on_model(const std::optional<SupportOnModel>& selected_on_model);

/// The lines of the tool's "Shortcuts" section, one per key, the way the tool shows them next to
/// the point settings. They live here so that the list and the mapping above are written together.
std::vector<std::string> support_tool_shortcut_lines();

} // namespace Slic3r::App::Plater
