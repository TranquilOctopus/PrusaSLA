#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "Slic3r/App/Preview/SlaSupportsPanel.hpp"

using Catch::Matchers::ContainsSubstring;
using Slic3r::App::Preview::sla_auto_raft_note;
using Slic3r::App::Preview::sla_supports_panel_state;
using Slic3r::App::Preview::SlaSupportsStatus;

TEST_CASE("sla_supports_panel_state - an empty build plate", "[sla_supports_panel]")
{
    SECTION("nothing to support and nothing to say")
    {
        const auto state = sla_supports_panel_state(0, 0, 0, 0, false);

        CHECK_FALSE(state.edit_supports_enabled);
        CHECK_FALSE(state.auto_support_selected_enabled);
        CHECK_FALSE(state.auto_support_all_enabled);
        CHECK_FALSE(state.slice_call_to_action);
        CHECK(state.status == SlaSupportsStatus::NoModels);
    }

    SECTION("a running generation says so")
    {
        const auto state = sla_supports_panel_state(0, 0, 0, 0, true);

        CHECK(state.status == SlaSupportsStatus::Generating);
    }
}

TEST_CASE("sla_supports_panel_state - models on the build plate", "[sla_supports_panel]")
{
    SECTION("a model without support points still needs them")
    {
        const auto state = sla_supports_panel_state(2, 1, 1, 1, false);

        CHECK(state.edit_supports_enabled);
        CHECK(state.auto_support_selected_enabled);
        CHECK(state.auto_support_all_enabled);
        CHECK_FALSE(state.slice_call_to_action);
        CHECK(state.status == SlaSupportsStatus::NeedsSupports);
    }

    SECTION("no selected model leaves only Auto support all")
    {
        const auto state = sla_supports_panel_state(2, 0, 1, 0, false);

        CHECK(state.auto_support_all_enabled);
        CHECK_FALSE(state.auto_support_selected_enabled);
        CHECK(state.edit_supports_enabled);
    }

    SECTION("a model without points is the one selected, and Auto support all is on it too")
    {
        const auto state = sla_supports_panel_state(1, 1, 0, 0, false);

        CHECK(state.auto_support_selected_enabled);
        CHECK(state.auto_support_all_enabled);
        CHECK(state.status == SlaSupportsStatus::NeedsSupports);
    }

    SECTION("every model with points asks for the Slice button")
    {
        const auto state = sla_supports_panel_state(3, 2, 3, 3, false);

        CHECK(state.slice_call_to_action);
        CHECK(state.status == SlaSupportsStatus::ReadyToSlice);
    }

    SECTION("a count above the model count still counts as every model supported")
    {
        const auto state = sla_supports_panel_state(2, 2, 3, 3, false);

        CHECK(state.slice_call_to_action);
        CHECK(state.status == SlaSupportsStatus::ReadyToSlice);
    }
}

TEST_CASE("sla_supports_panel_state - removing the support points (M2.32)", "[sla_supports_panel]")
{
    SECTION("nothing to remove leaves both rows off")
    {
        const auto state = sla_supports_panel_state(2, 1, 0, 0, false);

        CHECK_FALSE(state.clear_selected_enabled);
        CHECK_FALSE(state.clear_all_enabled);
    }

    SECTION("a model with points is what turns both rows on")
    {
        const auto state = sla_supports_panel_state(2, 1, 1, 1, false);

        CHECK(state.clear_selected_enabled);
        CHECK(state.clear_all_enabled);
    }

    SECTION("points only outside the selection leave Clear selected off")
    {
        const auto state = sla_supports_panel_state(3, 1, 2, 0, false);

        CHECK_FALSE(state.clear_selected_enabled);
        CHECK(state.clear_all_enabled);
    }

    SECTION("no model at all leaves both rows off")
    {
        const auto state = sla_supports_panel_state(0, 0, 0, 0, false);

        CHECK_FALSE(state.clear_selected_enabled);
        CHECK_FALSE(state.clear_all_enabled);
    }

    SECTION("a running generation owns the points and both rows")
    {
        const auto state = sla_supports_panel_state(2, 1, 1, 1, true);

        CHECK_FALSE(state.clear_selected_enabled);
        CHECK_FALSE(state.clear_all_enabled);
    }
}

TEST_CASE("sla_supports_panel_state - a running generation", "[sla_supports_panel]")
{
    SECTION("nothing may start while one is running")
    {
        const auto state = sla_supports_panel_state(2, 2, 1, 1, true);

        CHECK_FALSE(state.edit_supports_enabled);
        CHECK_FALSE(state.auto_support_selected_enabled);
        CHECK_FALSE(state.auto_support_all_enabled);
        CHECK_FALSE(state.slice_call_to_action);
        CHECK(state.status == SlaSupportsStatus::Generating);
    }

    SECTION("the generation wins over every model already having points")
    {
        const auto state = sla_supports_panel_state(2, 2, 2, 2, true);

        CHECK(state.status == SlaSupportsStatus::Generating);
        CHECK_FALSE(state.slice_call_to_action);
    }
}

// M7.8.4, rulebook R6: the Auto raft type reads the underside of a part and puts a raft under it
// where that would seal a pocket against the film. Nothing else shows that decision before the
// print is sliced, so the section gets one line for it, and only where there is something to say.
TEST_CASE("sla_auto_raft_note - the one line about the raft Auto put under the models", "[sla_supports_panel]")
{
    SECTION("no part with a suction cup, nothing to say")
    {
        CHECK(sla_auto_raft_note(0).empty());
    }

    SECTION("one model gets a raft")
    {
        const std::string note = sla_auto_raft_note(1);
        CHECK_FALSE(note.empty());
        CHECK_THAT(note, ContainsSubstring("1 model"));
CHECK_THAT(note, ContainsSubstring("suction cup"));
        // No plural over one model.
        CHECK(note.find("models") == std::string::npos);
    }

    SECTION("several models get a raft, and the count is in the line")
    {
        const std::string note = sla_auto_raft_note(3);
        CHECK_THAT(note, ContainsSubstring("3 models"));
        CHECK_THAT(note, ContainsSubstring("suction"));
    }
}
