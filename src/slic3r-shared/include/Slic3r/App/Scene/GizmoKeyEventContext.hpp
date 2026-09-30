#pragma once

#include <utility>

#include "Slic3r/App/Scene/Scene.hpp"
#include "Slic3r/App/Platform/KeyboardEvent.hpp"

namespace Slic3r::App::Scene {

class GizmoKeyEventContext {
public:
    GizmoKeyEventContext(const Platform::KeyboardEvent& keyboard_event)
        : m_keyboard_event(keyboard_event)
    {}

    const Platform::KeyboardEvent& keyboard_event() const { return m_keyboard_event; }

    /// Whether a gizmo has answered the key event itself, so that the commands of the render module
    /// are not run on top of it. A tool uses this to keep a key of its own: the support points tool
    /// clears the selection on Escape instead of the tool being closed by it.
    bool consumed() const { return m_consumed; }
    void consume() { m_consumed = true; }

private:
    Platform::KeyboardEvent m_keyboard_event;
    bool m_consumed = false;
};

} // namespace Slic3r::App::Scene
