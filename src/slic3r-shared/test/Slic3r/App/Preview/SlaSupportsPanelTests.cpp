#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/Preview/SlaSupportsPanel.hpp"

using Slic3r::App::Preview::sla_supports_panel_state;
using Slic3r::App::Preview::SlaSupportsStatus;

TEST_CASE("sla_supports_panel_state - an empty build plate", "[sla_supports_panel]")
{
    SECTION("nothing to support and nothing to say")
    {
        const auto state = sla_supports_panel_state(0, 0, 0, false);

        CHECK_FALSE(state.edit_supports_enabled);
        CHECK_FALSE(state.auto_support_selected_enabled);
        CHECK_FALSE(state.auto_support_all_enabled);
        CHECK_FALSE(state.slice_call_to_action);
        CHECK(state.status == SlaSupportsStatus::NoModels);
    }

    SECTION("a running generation says so")
    {
        const auto state = sla_supports_panel_state(0, 0, 0, true);

        CHECK(state.status == SlaSupportsStatus::Generating);
    }
}

TEST_CASE("sla_supports_panel_state - models on the build plate", "[sla_supports_panel]")
{
    SECTION("a model without support points still needs them")
    {
        const auto state = sla_supports_panel_state(2, 1, 1, false);

        CHECK(state.edit_supports_enabled);
        CHECK(state.auto_support_selected_enabled);
        CHECK(state.auto_support_all_enabled);
        CHECK_FALSE(state.slice_call_to_action);
        CHECK(state.status == SlaSupportsStatus::NeedsSupports);
    }

    SECTION("no selected model leaves only Auto support all")
    {
        const auto state = sla_supports_panel_state(2, 0, 1, false);

        CHECK(state.auto_support_all_enabled);
        CHECK_FALSE(state.auto_support_selected_enabled);
        CHECK(state.edit_supports_enabled);
    }

    SECTION("a model without points is the one selected, and Auto support all is on it too")
    {
        const auto state = sla_supports_panel_state(1, 1, 0, false);

        CHECK(state.auto_support_selected_enabled);
        CHECK(state.auto_support_all_enabled);
        CHECK(state.status == SlaSupportsStatus::NeedsSupports);
    }

    SECTION("every model with points asks for the Slice button")
    {
        const auto state = sla_supports_panel_state(3, 2, 3, false);

        CHECK(state.slice_call_to_action);
        CHECK(state.status == SlaSupportsStatus::ReadyToSlice);
    }

    SECTION("a count above the model count still counts as every model supported")
    {
        const auto state = sla_supports_panel_state(2, 2, 3, false);

        CHECK(state.slice_call_to_action);
        CHECK(state.status == SlaSupportsStatus::ReadyToSlice);
    }
}

TEST_CASE("sla_supports_panel_state - a running generation", "[sla_supports_panel]")
{
    SECTION("nothing may start while one is running")
    {
        const auto state = sla_supports_panel_state(2, 2, 1, true);

        CHECK_FALSE(state.edit_supports_enabled);
        CHECK_FALSE(state.auto_support_selected_enabled);
        CHECK_FALSE(state.auto_support_all_enabled);
        CHECK_FALSE(state.slice_call_to_action);
        CHECK(state.status == SlaSupportsStatus::Generating);
    }

    SECTION("the generation wins over every model already having points")
    {
        const auto state = sla_supports_panel_state(2, 2, 2, true);

        CHECK(state.status == SlaSupportsStatus::Generating);
        CHECK_FALSE(state.slice_call_to_action);
    }
}
