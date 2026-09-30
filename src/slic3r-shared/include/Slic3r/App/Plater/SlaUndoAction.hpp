#pragma once

namespace Slic3r::App::Plater {

/// One user action, one undo snapshot (M2.6b).
///
/// A slider reports a new value on every frame its thumb is dragged, so a drag of N ticks reaches
/// the tool as N value changes. The snapshot has to be taken before the first of them changes the
/// model, and the ticks after it belong to that same action: one snapshot each would push N undo
/// steps for one drag, and undoing would have to be repeated N times to get back to where the drag
/// started. An action that is not a value edit - a button, a combo box, a point added on the canvas
/// - is one action of its own and always takes its snapshot.
class SlaUndoAction
{
public:
    /// The user started changing a value: a click or a drag on a slider, a wheel notch, or a
    /// keystroke in the number field beside it. The next value change owes a snapshot.
    void begin()
    {
        m_open           = true;
        m_snapshot_taken = false;
    }

    /// The value edit is over, so the next change is an action of its own again.
    void end()
    {
        m_open           = false;
        m_snapshot_taken = false;
    }

    /// Whether the caller has to take a snapshot now. True for an action of its own, and for the
    /// first value change of an edit that is running; false for every later value change of it.
    bool take()
    {
        if (m_open && m_snapshot_taken) {
            return false;
        }
        m_snapshot_taken = true;
        return true;
    }

private:
    bool m_open{false};
    bool m_snapshot_taken{false};
};

} // namespace Slic3r::App::Plater
