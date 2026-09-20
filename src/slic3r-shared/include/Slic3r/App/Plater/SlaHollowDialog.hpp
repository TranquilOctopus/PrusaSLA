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
        std::function<void(double)> hole_radius_changed = [](double) {};
        std::function<void(double)> hole_height_changed = [](double) {};
        std::function<void()> remove_selected_holes = []() {};
        std::function<void()> remove_all_holes = []() {};
    };

    Callbacks& callbacks();

    void set_enable(bool enabled);
    void set_preview_enabled(bool enabled);
    void set_min_thickness(double thickness_mm);
    void set_quality(double quality);
    void set_closing_distance(double distance_mm);
    void set_hole_radius(double radius_mm);
    void set_hole_height(double height_mm);
    void set_hole_count(size_t count);
    void set_status(const std::string& status);
    void set_holes_controls_enabled(bool enabled);
    void set_remove_selected_enabled(bool enabled);
    void set_remove_all_enabled(bool enabled);

private:
    Yoga::ToggleButton* m_enable_checkbox = nullptr;
    Yoga::SliderWithInput* m_min_thickness_slider = nullptr;
    Yoga::SliderWithInput* m_quality_slider = nullptr;
    Yoga::SliderWithInput* m_closing_distance_slider = nullptr;
    Yoga::SliderWithInput* m_hole_radius_slider = nullptr;
    Yoga::SliderWithInput* m_hole_height_slider = nullptr;
    Yoga::LayoutButton* m_preview_button = nullptr;
    Yoga::LayoutButton* m_remove_selected_button = nullptr;
    Yoga::LayoutButton* m_remove_all_button = nullptr;
    Yoga::Text* m_status_text = nullptr;
    Yoga::Text* m_hole_count_text = nullptr;

    Callbacks m_callbacks;
};

} // namespace Slic3r::App::Plater