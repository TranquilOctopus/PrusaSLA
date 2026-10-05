#pragma once

#include <cstddef>
#include <string>

namespace Slic3r::App::Preview {

/// What the one-line status of the Preview "Supports" section says.
enum class SlaSupportsStatus
{
    NoModels, ///< Nothing printable is on the selected build plate.
    NeedsSupports, ///< At least one printable model has no support points yet.
    Generating, ///< A support generation is running.
    ReadyToSlice, ///< Every printable model has support points, slicing is what is left.
};

/// Which controls of the Preview "Supports" section can be used, and what its status line says.
struct SlaSupportsPanelState
{
    bool edit_supports_enabled{false};
    bool auto_support_selected_enabled{false};
    bool auto_support_all_enabled{false};
    /// "Clear selected" is on when one of the models it works on has support points (M2.32).
    bool clear_selected_enabled{false};
    /// "Clear all" is on when any listed model has support points (M2.32).
    bool clear_all_enabled{false};
    /// Every printable model has support points, so the section asks for the Slice button.
    bool slice_call_to_action{false};
    SlaSupportsStatus status{SlaSupportsStatus::NoModels};
};

/**
 * @brief The rule behind the Preview "Supports" section, free of any UI.
 *
 * @p printable_models are the printable models on the selected build plate, @p selected_printable_models
 * of them the ones the "selected" action can work on, @p models_with_supports of them the ones that
 * already have support points, @p selected_models_with_supports of the selected ones the ones that
 * have points, and @p generating tells whether a generation is running. Every action needs something
 * to work on and nothing may run twice at a time, so only a running generation disables them, while
 * the Slice call to action needs all the models to be supported. Removing points (M2.32) needs
 * points to remove, so it follows the two counts of the models that have them.
 */
SlaSupportsPanelState sla_supports_panel_state(
    std::size_t printable_models,
    std::size_t selected_printable_models,
    std::size_t models_with_supports,
    std::size_t selected_models_with_supports,
    bool generating
);

/**
 * @brief The one line that says the raft type Auto put a raft under the models, empty when it did
 * not (rulebook R6, M7.8.4).
 *
 * R6.1 is that a raft is not built unless the underside of a part would form a suction cup, so
 * nothing is said on a plate where no part has one: the section would only say what the raft type
 * in "Supports & raft" already says. A model that does have one is a decision the user cannot see
 * in any other panel, because the rule reads the underside of the part and nothing else does until
 * the print is sliced, so it gets one line here, under the status of the section.
 *
 * @param models_with_auto_raft How many of the listed models the rule gave a raft.
 */
std::string sla_auto_raft_note(std::size_t models_with_auto_raft);

} // namespace Slic3r::App::Preview
