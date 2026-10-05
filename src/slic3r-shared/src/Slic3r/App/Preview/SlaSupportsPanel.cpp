#include "Slic3r/App/Preview/SlaSupportsPanel.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

#include <fmt/format.h>

#include <string>

namespace Slic3r::App::Preview {

SlaSupportsPanelState sla_supports_panel_state(
    std::size_t printable_models,
    std::size_t selected_printable_models,
    std::size_t models_with_supports,
    std::size_t selected_models_with_supports,
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
    // Removing points needs points to remove, so both rows follow the models that have them and
    // neither of them is on while a generation is running (M2.32).
    state.clear_all_enabled      = idle && models_with_supports > 0;
    state.clear_selected_enabled = idle && selected_models_with_supports > 0;
    state.slice_call_to_action   = idle && every_model_supported;

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

std::string sla_auto_raft_note(std::size_t models_with_auto_raft)
{
    // One model is said in the singular, more than one in the plural, which is what the count is
    // here for. Zero is nothing at all: no part on this plate would form a suction cup, so there is
    // no raft to report (R6.1).
    if (models_with_auto_raft == 0) {
        return {};
    }
    if (models_with_auto_raft == 1) {
        return Biz::_u8L("Auto: 1 model gets a raft, its underside would form a suction cup.");
    }
    return fmt::format(fmt::runtime(Biz::_u8L("Auto: {} models get a raft, their undersides would form suction cups.")),
                       models_with_auto_raft);
}

} // namespace Slic3r::App::Preview
