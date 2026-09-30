// M2.6b: one user action, one undo snapshot. A slider reports a new value on every frame its thumb
// is dragged, so a drag of N ticks reaches the support tool as N value changes. The tool has to take
// its undo snapshot once, before the first of them changes the model, and the ticks after it belong
// to the same action: a snapshot each would push N undo steps for one drag and the user would have
// to press undo N times to get back to where the drag started.
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <initializer_list>

#include "Slic3r/App/Plater/SlaUndoAction.hpp"

using Slic3r::App::Plater::SlaUndoAction;

TEST_CASE("A drag of any number of slider ticks is one undo step", "[SlaUndoAction][undo]")
{
    for (const std::size_t ticks :
         {std::size_t{1}, std::size_t{2}, std::size_t{5}, std::size_t{40}})
    {
        CAPTURE(ticks);

        std::size_t snapshots = 0;
        SlaUndoAction action;
        action.begin(); // the mouse went down on the slider
        for (std::size_t tick = 0; tick < ticks; ++tick) {
            // The slider reported a value and the model was not touched yet: this is where the
            // snapshot is taken.
            if (action.take()) {
                ++snapshots;
            }
        }
        action.end(); // the mouse came up

        CHECK(snapshots == 1u);
    }
}

TEST_CASE("Two drags of the same slider are two undo steps", "[SlaUndoAction][undo]")
{
    std::size_t snapshots = 0;
    SlaUndoAction action;

    for (int drag = 0; drag < 2; ++drag) {
        CAPTURE(drag);
        action.begin();
        for (int tick = 0; tick < 3; ++tick) {
            if (action.take()) {
                ++snapshots;
            }
        }
        action.end();
    }

    CHECK(snapshots == 2u);
}

TEST_CASE("A value edited while no drag is running takes its own snapshot", "[SlaUndoAction][undo]")
{
    // A combo box, a button or a point added on the canvas never opens a value edit, so each of
    // them is an action of its own and has to be undoable on its own.
    std::size_t snapshots = 0;
    SlaUndoAction action;

    for (int click = 0; click < 3; ++click) {
        CAPTURE(click);
        if (action.take()) {
            ++snapshots;
        }
    }

    CHECK(snapshots == 3u);
}

TEST_CASE(
    "A value edit that reported nothing still leaves the next action alone",
    "[SlaUndoAction][undo]"
)
{
    // A click on the thumb without moving opens an edit and reports no value, so the tool takes no
    // snapshot for it. The next change must not be swallowed by the edit that is still open.
    SlaUndoAction action;
    action.begin();
    action.end();

    CHECK(action.take());
}
