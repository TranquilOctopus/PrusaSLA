#pragma once

#include "Slic3r/App/Plater/GizmoWindow.hpp"

namespace Slic3r::App::Yoga {
class SliderWithInput;
class LayoutButton;
class Text;
class Checkbox;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App::Plater {

class SlaSupportPointsDialog : public GizmoWindow
{
public:
    SlaSupportPointsDialog();

    struct Callbacks
    {
        std::function<void()> generate = []() {};
        std::function<void()> apply = []() {};
        std::function<void()> discard = []() {};
        std::function<void(double)> density_changed = [](double) {};
        std::function<void(double)> head_diameter_changed = [](double) {};
        std::function<void(double)> clipping_plane_changed = [](double) {};
        std::function<void(bool)> lock_island_supports_changed = [](bool) {};
        std::function<void()> clipping_plane_reset = []() {};
    };

    Callbacks& callbacks();

    void set_density(int density);
    void set_generate_enabled(bool enabled);
    void set_apply_enabled(bool enabled);
    void set_point_count(size_t count);
    void set_head_diameter(double diameter_mm);
    void set_clipping_plane_position(double pos);
    void set_lock_island_supports(bool locked);

private:
    Yoga::SliderWithInput* m_density_slider = nullptr;
    Yoga::SliderWithInput* m_head_diameter_slider = nullptr;
    Yoga::SliderWithInput* m_clipping_plane_slider = nullptr;
    Yoga::LayoutButton* m_generate_button = nullptr;
    Yoga::LayoutButton* m_apply_button = nullptr;
    Yoga::LayoutButton* m_discard_button = nullptr;
    Yoga::LayoutButton* m_clipping_plane_reset_button = nullptr;
    Yoga::Checkbox* m_lock_island_supports_checkbox = nullptr;
    Yoga::Text* m_point_count_text = nullptr;

    Callbacks m_callbacks;
};

} // namespace Slic3r::App::Plater