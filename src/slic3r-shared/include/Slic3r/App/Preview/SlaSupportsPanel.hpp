#pragma once

#include <cstddef>

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
    /// Every printable model has support points, so the section asks for the Slice button.
    bool slice_call_to_action{false};
    SlaSupportsStatus status{SlaSupportsStatus::NoModels};
};

/**
 * @brief The rule behind the Preview "Supports" section, free of any UI.
 *
 * @p printable_models are the printable models on the selected build plate, @p selected_printable_models
 * of them the ones the "selected" action can work on, @p models_with_supports of them the ones that
 * already have support points, and @p generating tells whether a generation is running. Every action
 * needs something to work on and nothing may run twice at a time, so only a running generation
 * disables them, while the Slice call to action needs all the models to be supported.
 */
SlaSupportsPanelState sla_supports_panel_state(
    std::size_t printable_models,
    std::size_t selected_printable_models,
    std::size_t models_with_supports,
    bool generating
);

} // namespace Slic3r::App::Preview
