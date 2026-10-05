// M2.28: the keyboard shortcuts of the support points tool. Lychee and Chitubox users expect the
// presets on 1 to 5, auto support on A, the per-point "Support on model" on G and Escape to drop
// the selection, and they expect the keys to be dead while a text field has the focus. The mapping
// from a key to the action of the tool is a function of its own, so it is tested here without a
// gizmo, a project or a window: what the gizmo does with the action is its own business.
#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

#include "Slic3r/App/Plater/SlaSupportOnModel.hpp"
#include "Slic3r/App/Plater/SlaSupportToolShortcuts.hpp"
#include "Slic3r/App/Platform/KeyCode.hpp"
#include "Slic3r/App/Platform/KeyModifers.hpp"

using Slic3r::App::Plater::support_tool_action_for;
using Slic3r::App::Plater::support_tool_shortcut_lines;
using Slic3r::App::Plater::support_tool_toggled_on_model;
using Slic3r::App::Plater::SupportOnModel;
using Slic3r::App::Plater::SupportToolAction;
using Slic3r::App::Plater::SupportToolKeyEvent;
using Slic3r::App::Platform::KeyCode;
using Slic3r::App::Platform::KeyModifier;
using Slic3r::App::Platform::KeyModifiers;

namespace {

/// A key press in a tool that is active, on a model the user is working on, with no text field
/// holding the keys and nothing selected yet.
SupportToolKeyEvent key_press(KeyCode code)
{
    SupportToolKeyEvent key;
    key.code        = code;
    key.tool_active = true;
    return key;
}

SupportToolKeyEvent key_press(KeyCode code, KeyModifiers modifiers)
{
    SupportToolKeyEvent key = key_press(code);
    key.modifiers           = modifiers;
    return key;
}

} // namespace

TEST_CASE("The support tool picks a tip class with 1 to 5", "[SlaSupportToolShortcuts]")
{
    // The keys are the preset buttons: the tip class a new point takes, and the class written on the
    // points that are selected. The classes are the five of the support rulebook (M7.8.1, R3), T0.1
    // to T0.6, so the keys go 1, 2, 3, 4 and 5: there is no T0.5.
    CHECK(support_tool_action_for(key_press(KeyCode::Num1)) == SupportToolAction::PresetT01);
    CHECK(support_tool_action_for(key_press(KeyCode::Num2)) == SupportToolAction::PresetT02);
    CHECK(support_tool_action_for(key_press(KeyCode::Num3)) == SupportToolAction::PresetT03);
    CHECK(support_tool_action_for(key_press(KeyCode::Num4)) == SupportToolAction::PresetT04);
    CHECK(support_tool_action_for(key_press(KeyCode::Num5)) == SupportToolAction::PresetT06);

    // A sixth preset does not exist, and neither does a preset behind a modifier that the app uses
    // for something else.
    CHECK(support_tool_action_for(key_press(KeyCode::Num6)) == SupportToolAction::None);
    CHECK(support_tool_action_for(key_press(KeyCode::Num0)) == SupportToolAction::None);
    CHECK(
        support_tool_action_for(key_press(KeyCode::Num1, KeyModifiers(KeyModifier::Ctrl)))
        == SupportToolAction::None
    );
    CHECK(
        support_tool_action_for(key_press(KeyCode::Num2, KeyModifiers(KeyModifier::Alt)))
        == SupportToolAction::None
    );

    // On some layouts a digit is only reachable with the shift, so the presets take one too.
    CHECK(
        support_tool_action_for(key_press(KeyCode::Num3, KeyModifiers(KeyModifier::Shift)))
        == SupportToolAction::PresetT03
    );
}

TEST_CASE("The support tool auto supports with A and Shift+A", "[SlaSupportToolShortcuts]")
{
    CHECK(
        support_tool_action_for(key_press(KeyCode::A)) == SupportToolAction::AutoSupportSelection
    );
    CHECK(
        support_tool_action_for(key_press(KeyCode::A, KeyModifiers(KeyModifier::Shift)))
        == SupportToolAction::AutoSupportAll
    );

    // Ctrl+A has selected all the points since M2.3b, so it stays that and is not the auto support
    // of a single model.
    CHECK(
        support_tool_action_for(key_press(KeyCode::A, KeyModifiers(KeyModifier::Ctrl)))
        == SupportToolAction::SelectAllPoints
    );
}

TEST_CASE("The support tool flips Support on model with G", "[SlaSupportToolShortcuts]")
{
    SupportToolKeyEvent key = key_press(KeyCode::G);

    // The switch is written on points, so with nothing selected the key is not the tool's.
    CHECK(support_tool_action_for(key) == SupportToolAction::None);

    key.has_selection = true;
    CHECK(support_tool_action_for(key) == SupportToolAction::ToggleSupportOnModel);
}

TEST_CASE(
    "The support tool drops the selection with Escape and keeps open",
    "[SlaSupportToolShortcuts]"
)
{
    SupportToolKeyEvent key = key_press(KeyCode::Escape);

    // With an empty selection Escape is not the tool's key: it closes the tool, the way it closes
    // every other tool, so the mapping leaves it alone.
    CHECK(support_tool_action_for(key) == SupportToolAction::None);

    key.has_selection = true;
    CHECK(support_tool_action_for(key) == SupportToolAction::ClearSelection);
}

TEST_CASE(
    "The support tool keeps deleting and selecting with Delete and Ctrl+A",
    "[SlaSupportToolShortcuts]"
)
{
    CHECK(
        support_tool_action_for(key_press(KeyCode::Delete))
        == SupportToolAction::DeleteSelectedPoints
    );
    CHECK(
        support_tool_action_for(key_press(KeyCode::Backspace))
        == SupportToolAction::DeleteSelectedPoints
    );
    CHECK(
        support_tool_action_for(key_press(KeyCode::A, KeyModifiers(KeyModifier::Ctrl)))
        == SupportToolAction::SelectAllPoints
    );
}

TEST_CASE(
    "The support tool shortcuts are dead while a text field has the focus",
    "[SlaSupportToolShortcuts]"
)
{
    // Typing a number into the head diameter, or a letter into a text field of the window, is
    // typing: the key must not reach the points.
    const std::vector<KeyCode> keys{
        KeyCode::Num1,
        KeyCode::Num2,
        KeyCode::Num3,
        KeyCode::Num4,
        KeyCode::Num5,
        KeyCode::A,
        KeyCode::G,
        KeyCode::Escape,
        KeyCode::Delete,
        KeyCode::Backspace,
    };
    for (KeyCode code : keys) {
        SupportToolKeyEvent key = key_press(code);
        key.text_field_focus    = true;
        key.has_selection       = true;
        CHECK(support_tool_action_for(key) == SupportToolAction::None);
    }
}

TEST_CASE(
    "The support tool shortcuts are dead while another tool is active",
    "[SlaSupportToolShortcuts]"
)
{
    // The gizmos are asked for every key, not only while they are the tool on, so a shortcut of
    // this tool must stay quiet when another one is up.
    const std::vector<KeyCode> keys{
        KeyCode::Num1,
        KeyCode::Num2,
        KeyCode::Num3,
        KeyCode::Num4,
        KeyCode::Num5,
        KeyCode::A,
        KeyCode::G,
        KeyCode::Escape,
        KeyCode::Delete,
    };
    for (KeyCode code : keys) {
        SupportToolKeyEvent key = key_press(code);
        key.tool_active         = false;
        key.has_selection       = true;
        CHECK(support_tool_action_for(key) == SupportToolAction::None);
    }
}

TEST_CASE(
    "A key that is not a shortcut of the support tool does nothing",
    "[SlaSupportToolShortcuts]"
)
{
    CHECK(support_tool_action_for(key_press(KeyCode::B)) == SupportToolAction::None);
    CHECK(support_tool_action_for(key_press(KeyCode::P)) == SupportToolAction::None);
    CHECK(support_tool_action_for(key_press(KeyCode::Space)) == SupportToolAction::None);
    CHECK(support_tool_action_for(key_press(KeyCode::F5)) == SupportToolAction::None);
    CHECK(support_tool_action_for(key_press(KeyCode::None)) == SupportToolAction::None);
}

TEST_CASE(
    "The G shortcut toggles Support on model between Allow and Forbid",
    "[SlaSupportToolShortcuts]"
)
{
    // Allow and Forbid are the two sides of the switch, so the toggle goes from one to the other.
    CHECK(support_tool_toggled_on_model(SupportOnModel::Allow) == SupportOnModel::Forbid);
    CHECK(support_tool_toggled_on_model(SupportOnModel::Forbid) == SupportOnModel::Allow);

    // A point that follows the object has not been given a switch, so the toggle gives it one that
    // forbids the model, the side a support on a plate-only object needs.
    CHECK(support_tool_toggled_on_model(SupportOnModel::Inherit) == SupportOnModel::Forbid);

    // A mixed selection has no side to toggle from, so the toggle leaves the points to the object.
    CHECK(support_tool_toggled_on_model(std::nullopt) == SupportOnModel::Inherit);

    const std::optional<SupportOnModel> allow = SupportOnModel::Allow;
    CHECK(support_tool_toggled_on_model(allow) == SupportOnModel::Forbid);
}

TEST_CASE("The support tool lists its shortcuts, one line per key", "[SlaSupportToolShortcuts]")
{
    const std::vector<std::string> lines = support_tool_shortcut_lines();

    // Every key the mapping answers to is in the list the tool shows, and nothing else is. The
    // five presets are the tip classes of the support rulebook (M7.8.1), so they say their size.
    const std::vector<std::string> expected_keys{
        "1",
        "2",
        "3",
        "4",
        "5",
        "A",
        "Shift+A",
        "G",
        "Esc",
        "Ctrl+A",
        "Delete",
    };
    REQUIRE(lines.size() == expected_keys.size());
    for (size_t i = 0; i < expected_keys.size(); ++i) {
        const std::string prefix = expected_keys[i] + ": ";
        REQUIRE(lines[i].compare(0, prefix.size(), prefix) == 0);
        REQUIRE(lines[i].size() > prefix.size());
    }
}
