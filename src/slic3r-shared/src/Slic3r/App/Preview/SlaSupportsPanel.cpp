#include "Slic3r/App/Preview/SlaSupportsPanel.hpp"

namespace Slic3r::App::Preview {

SlaSupportsPanelState sla_supports_panel_state(
    std::size_t printable_models,
    std::size_t selected_printable_models,
    std::size_t models_with_supports,
    bool generating
)
{
    SlaSupportsPanelState state;

    const bool idle                 = !generating;
    const bool something_to_support = idle && printable_models > 0;
    // Points can only sit on a listed model, so a bigger count is nonsense the caller cannot have.
    const bool every_model_supported =
        printable_models > 0 && models_with_supports >= printable_models;

    state.edit_supports_enabled         = something_to_support;
    state.auto_support_all_enabled      = something_to_support;
    state.auto_support_selected_enabled = idle && selected_printable_models > 0;
    state.slice_call_to_action          = idle && every_model_supported;

    if (generating) {
        state.status = SlaSupportsStatus::Generating;
    } else if (printable_models == 0) {
        state.status = SlaSupportsStatus::NoModels;
    } else if (every_model_supported) {
        state.status = SlaSupportsStatus::ReadyToSlice;
    } else {
        state.status = SlaSupportsStatus::NeedsSupports;
    }

    return state;
}

} // namespace Slic3r::App::Preview
