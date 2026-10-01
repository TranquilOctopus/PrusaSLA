#pragma once

#include "Slic3r/App/Plater/GizmoWindow.hpp"
#include "Slic3r/App/Plater/SlaSupportGeometry.hpp"
#include "Slic3r/App/Plater/SlaSupportOnModel.hpp"

#include <initializer_list>
#include <optional>

namespace Slic3r::App::Yoga {
class SliderWithInput;
class LayoutButton;
class Text;
class ToggleButton;
class CollapsibleWindow;
class ComboBox;
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
        std::function<void(Domain::SLA::SupportPoint::TipShape)> tip_shape_changed = [](
            Domain::SLA::SupportPoint::TipShape) {};
        std::function<void(double)> tip_length_changed = [](double) {};
        std::function<void(double)> knot_diameter_changed = [](double) {};
        std::function<void(double)> stem_sides_changed = [](double) {};
        std::function<void(double)> stem_taper_changed = [](double) {};
        std::function<void(Domain::SLA::SupportPoint::BaseShape)> base_shape_changed = [](
            Domain::SLA::SupportPoint::BaseShape) {};
        std::function<void(SupportOnModel)> on_model_changed = [](SupportOnModel) {};
        std::function<void(bool)> head_diameter_use_global_changed = [](bool) {};
        std::function<void(bool)> pillar_diameter_use_global_changed = [](bool) {};
        std::function<void(bool)> base_diameter_use_global_changed = [](bool) {};
        std::function<void(bool)> base_height_use_global_changed = [](bool) {};
        std::function<void(double)> clipping_plane_changed = [](double) {};
        std::function<void(bool)> lock_island_supports_changed = [](bool) {};
        std::function<void()> clipping_plane_reset = []() {};
        std::function<void()> preset_mini = []() {};
        std::function<void()> preset_light = []() {};
        std::function<void()> preset_medium = []() {};
        std::function<void()> preset_heavy = []() {};
        std::function<void()> auto_support_all = []() {};
        // Take the support points of the model the tool works on away (M2.32). It asks first, like
        // the same action of the Preview sidebar and of the object context menu do.
        std::function<void()> remove_all_points = []() {};

        // The user started and stopped changing one of the value sliders (M2.6b). Every value the
        // tool writes on the points comes from one of them, and a drag of a slider reports a value
        // per frame, so the tool takes its undo snapshot at the start of the edit and not on each of
        // the ticks of it.
        std::function<void()> value_editing_started = []() {};
        std::function<void()> value_editing_ended = []() {};
    };

    Callbacks& callbacks();

    void set_density(int density);
    void set_generate_enabled(bool enabled);
    void set_auto_support_all_enabled(bool enabled);
    void set_apply_enabled(bool enabled);
    /// "Remove all points" is on while the model the tool works on has support points to remove
    /// (M2.32), so the row asks for nothing where there is nothing to clear.
    void set_remove_all_points_enabled(bool enabled);
    void set_point_count(size_t count);
    void set_head_diameter(double diameter_mm);
    void set_pillar_diameter(double diameter_mm);
    void set_base_diameter(double diameter_mm);
    void set_base_height(double height_mm);
    void set_head_diameter_use_global(bool use_global);
    void set_pillar_diameter_use_global(bool use_global);
    void set_base_diameter_use_global(bool use_global);
    void set_base_height_use_global(bool use_global);

    /// Shows the tip shape, tip length, knot, stem cross-section, stem taper and foot shape of the
    /// selected points (M2.16c, M2.24, M2.23b). An empty @p geometry means nothing is selected or
    /// the points disagree on one of the values: the fields are then left empty instead of showing a
    /// value only some of the points have. @p has_selection tells the two apart for the tip
    /// diameter, which is the head diameter control as well: with nothing selected it keeps showing
    /// the diameter a new point takes.
    void set_support_geometry(const std::optional<SlaSupportGeometry>& geometry, bool has_selection);

    /// Shows the per-point "may this support end on the model" switch of the selected points
    /// (M2.26). An empty @p on_model means nothing is selected or the selected points disagree:
    /// the control is then left empty instead of showing a state only some of the points have.
    void set_support_on_model(const std::optional<SupportOnModel>& on_model);

    void set_clipping_plane_position(double pos);
    void set_lock_island_supports(bool locked);
    void set_active_preset(int index);

    /// Whether the section with the point settings is open. The tool opens it, so the settings are
    /// there when one goes into supporting an object (M2.17d4).
    void set_settings_expanded(bool expanded);

private:
    /// Lets every value slider of the window report when the user started and stopped editing its
    /// value, so that a drag of one of them is a single undo step (M2.6b).
    void report_value_editing(std::initializer_list<Yoga::SliderWithInput*> sliders);

private:
    Yoga::CollapsibleWindow* m_settings_window = nullptr;
    // The keyboard shortcuts of the tool (M2.28), one line each, in a section of the settings that
    // is closed so that the list does not stand between the point settings and the presets.
    Yoga::CollapsibleWindow* m_shortcuts_window = nullptr;
    Yoga::SliderWithInput* m_density_slider = nullptr;
    Yoga::SliderWithInput* m_head_diameter_slider = nullptr;
    Yoga::SliderWithInput* m_pillar_diameter_slider = nullptr;
    Yoga::SliderWithInput* m_base_diameter_slider = nullptr;
    Yoga::SliderWithInput* m_base_height_slider = nullptr;
    Yoga::ToggleButton* m_head_diameter_use_global_checkbox = nullptr;
    Yoga::ToggleButton* m_pillar_diameter_use_global_checkbox = nullptr;
    Yoga::ToggleButton* m_base_diameter_use_global_checkbox = nullptr;
    Yoga::ToggleButton* m_base_height_use_global_checkbox = nullptr;
    // The per-point support geometry (M2.16c, M2.24): the tip diameter (which is the head diameter
    // slider above), the section the tip shape, the tip length and the knot belong to, and the stem
    // the side count and the taper.
    Yoga::ComboBox* m_tip_shape_combo = nullptr;
    Yoga::SliderWithInput* m_tip_length_slider = nullptr;
    Yoga::SliderWithInput* m_knot_diameter_slider = nullptr;
    Yoga::SliderWithInput* m_stem_sides_slider = nullptr;
    Yoga::SliderWithInput* m_stem_taper_slider = nullptr;
    // The per-point "may this support end on the model" switch (M2.26), in the same section as
    // the per-point geometry above.
    Yoga::ComboBox* m_on_model_combo = nullptr;
    // The shape of the foot of the selected points (M2.23b), next to the switch above. Default
    // keeps the shape support_base_shape configures.
    Yoga::ComboBox* m_base_shape_combo = nullptr;
    Yoga::LayoutButton* m_generate_button = nullptr;
    Yoga::LayoutButton* m_auto_support_all_button = nullptr;
    Yoga::LayoutButton* m_apply_button = nullptr;
    Yoga::LayoutButton* m_discard_button = nullptr;
    Yoga::LayoutButton* m_remove_all_points_button = nullptr;
    Yoga::LayoutButton* m_clipping_plane_reset_button = nullptr;
    Yoga::ToggleButton* m_lock_island_supports_checkbox = nullptr;
    Yoga::LayoutButton* m_preset_mini_button = nullptr;
    Yoga::LayoutButton* m_preset_light_button = nullptr;
    Yoga::LayoutButton* m_preset_medium_button = nullptr;
    Yoga::LayoutButton* m_preset_heavy_button = nullptr;
    Yoga::SliderWithInput* m_clipping_plane_slider = nullptr;
    Yoga::Text* m_point_count_text = nullptr;

    Callbacks m_callbacks;
};

} // namespace Slic3r::App::Plater