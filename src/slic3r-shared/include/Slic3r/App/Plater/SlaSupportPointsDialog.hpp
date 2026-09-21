#pragma once

#include "Slic3r/App/Plater/GizmoWindow.hpp"

namespace Slic3r::App::Yoga {
class SliderWithInput;
class LayoutButton;
class Text;
class ToggleButton;
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
        std::function<void(double)> pillar_diameter_changed = [](double) {};
        std::function<void(double)> base_diameter_changed = [](double) {};
        std::function<void(double)> base_height_changed = [](double) {};
        std::function<void(bool)> head_diameter_use_global_changed = [](bool) {};
        std::function<void(bool)> pillar_diameter_use_global_changed = [](bool) {};
        std::function<void(bool)> base_diameter_use_global_changed = [](bool) {};
        std::function<void(bool)> base_height_use_global_changed = [](bool) {};
        std::function<void(double)> clipping_plane_changed = [](double) {};
        std::function<void(bool)> lock_island_supports_changed = [](bool) {};
        std::function<void()> clipping_plane_reset = []() {};
        std::function<void()> preset_light = []() {};
        std::function<void()> preset_medium = []() {};
        std::function<void()> preset_heavy = []() {};
    };

    Callbacks& callbacks();

    void set_density(int density);
    void set_generate_enabled(bool enabled);
    void set_apply_enabled(bool enabled);
    void set_point_count(size_t count);
    void set_head_diameter(double diameter_mm);
    void set_pillar_diameter(double diameter_mm);
    void set_base_diameter(double diameter_mm);
    void set_base_height(double height_mm);
    void set_head_diameter_use_global(bool use_global);
    void set_pillar_diameter_use_global(bool use_global);
    void set_base_diameter_use_global(bool use_global);
    void set_base_height_use_global(bool use_global);
    void set_clipping_plane_position(double pos);
    void set_lock_island_supports(bool locked);

private:
    Yoga::SliderWithInput* m_density_slider = nullptr;
    Yoga::SliderWithInput* m_head_diameter_slider = nullptr;
    Yoga::SliderWithInput* m_pillar_diameter_slider = nullptr;
    Yoga::SliderWithInput* m_base_diameter_slider = nullptr;
    Yoga::SliderWithInput* m_base_height_slider = nullptr;
    Yoga::ToggleButton* m_head_diameter_use_global_checkbox = nullptr;
    Yoga::ToggleButton* m_pillar_diameter_use_global_checkbox = nullptr;
    Yoga::ToggleButton* m_base_diameter_use_global_checkbox = nullptr;
    Yoga::ToggleButton* m_base_height_use_global_checkbox = nullptr;
    Yoga::LayoutButton* m_generate_button = nullptr;
    Yoga::LayoutButton* m_apply_button = nullptr;
    Yoga::LayoutButton* m_discard_button = nullptr;
    Yoga::LayoutButton* m_clipping_plane_reset_button = nullptr;
    Yoga::ToggleButton* m_lock_island_supports_checkbox = nullptr;
    Yoga::LayoutButton* m_preset_light_button = nullptr;
    Yoga::LayoutButton* m_preset_medium_button = nullptr;
    Yoga::LayoutButton* m_preset_heavy_button = nullptr;
    Yoga::SliderWithInput* m_clipping_plane_slider = nullptr;
    Yoga::Text* m_point_count_text = nullptr;

    Callbacks m_callbacks;
};

} // namespace Slic3r::App::Plater