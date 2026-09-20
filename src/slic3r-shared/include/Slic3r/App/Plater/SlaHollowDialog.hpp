#pragma once

#include "Slic3r/App/Plater/GizmoWindow.hpp"

namespace Slic3r::App::Yoga {
class SliderWithInput;
class LayoutButton;
class Text;
class ToggleButton;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App::Plater {

class SlaHollowDialog : public GizmoWindow
{
public:
    SlaHollowDialog();

    struct Callbacks
    {
        std::function<void()> preview = []() {};
        std::function<void(bool)> enable_changed = [](bool) {};
        std::function<void(double)> min_thickness_changed = [](double) {};
        std::function<void(double)> quality_changed = [](double) {};
        std::function<void(double)> closing_distance_changed = [](double) {};
    };

    Callbacks& callbacks();

    void set_enable(bool enabled);
    void set_preview_enabled(bool enabled);
    void set_min_thickness(double thickness_mm);
    void set_quality(double quality);
    void set_closing_distance(double distance_mm);
    void set_status(const std::string& status);

private:
    Yoga::ToggleButton* m_enable_checkbox = nullptr;
    Yoga::SliderWithInput* m_min_thickness_slider = nullptr;
    Yoga::SliderWithInput* m_quality_slider = nullptr;
    Yoga::SliderWithInput* m_closing_distance_slider = nullptr;
    Yoga::LayoutButton* m_preview_button = nullptr;
    Yoga::Text* m_status_text = nullptr;

    Callbacks m_callbacks;
};

} // namespace Slic3r::App::Plater