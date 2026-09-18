#pragma once

#include "Slic3r/App/Plater/GizmoWindow.hpp"

namespace Slic3r::App::Yoga {
class SliderWithInput;
class LayoutButton;
class Text;
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
    };

    Callbacks& callbacks();

    void set_density(int density);
    void set_generate_enabled(bool enabled);
    void set_apply_enabled(bool enabled);
    void set_point_count(size_t count);

private:
    Yoga::SliderWithInput* m_density_slider = nullptr;
    Yoga::LayoutButton* m_generate_button = nullptr;
    Yoga::LayoutButton* m_apply_button = nullptr;
    Yoga::LayoutButton* m_discard_button = nullptr;
    Yoga::Text* m_point_count_text = nullptr;

    Callbacks m_callbacks;
};

} // namespace Slic3r::App::Plater