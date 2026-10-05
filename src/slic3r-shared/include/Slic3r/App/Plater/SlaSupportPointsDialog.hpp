#pragma once

#include "Slic3r/App/Plater/GizmoWindow.hpp"
#include "Slic3r/App/Plater/SlaSupportPointsSettings.hpp"

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <string>

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
        std::function<void(double)> clipping_plane_changed = [](double) {};
        std::function<void(bool)> lock_island_supports_changed = [](bool) {};
        std::function<void()> clipping_plane_reset = []() {};
        std::function<void()> auto_support_all = []() {};
        // Take the support points of the model the tool works on away (M2.32). It asks first, like
        // the same action of the Preview sidebar and of the object context menu do.
        std::function<void()> remove_all_points = []() {};

        /// Take the selected support points away (M2.38). This is the "Delete" button of the
        /// "Selected supports" group, so a support can be gone the way a user of Chitubox removes it:
        /// by picking it and pressing the button of the group its values are in, without reaching
        /// for the Delete key. It is the same action the Delete key and Ctrl+click take.
        std::function<void()> delete_selected_points = []() {};

        /// One value of the support settings changed (M2.33). The group says what it changes: the
        /// "New supports" one what a clicked point takes, the "Selected supports" one what the
        /// selected points carry. @p value is the number of a slider and the value of the
        /// enumeration of a dropdown, see sla_support_point_field_value().
        std::function<void(SlaSupportSettingsGroup, SlaSupportPointField, double)> support_setting_changed =
            [](SlaSupportSettingsGroup, SlaSupportPointField, double) {};

        /// A preset button of one of the two groups (M2.18, M2.22, M2.33): 0 Mini, 1 Light, 2 Medium,
        /// 3 Heavy. The "New supports" preset is what a clicked point takes from then on, the
        /// "Selected supports" preset lands on the points that are selected.
        std::function<void(SlaSupportSettingsGroup, int)> support_preset_selected =
            [](SlaSupportSettingsGroup, int) {};

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

    /// The "New supports" group: the values a clicked point takes (M2.33). Changing them never
    /// changes a point that is already there.
    void set_new_support_values(const SlaSupportNewValues& values);

    /// The "Selected supports (N)" group: what the points of the selection carry, with the fields
    /// left empty (or "Mixed") where they disagree, and the group itself only shown while there is
    /// a selection, its title naming how many points are in it (M2.33).
    void set_selected_support_values(const SlaSupportSelectionView& view);

    /// Which preset button of which group is checked, the one that was last chosen there.
    void set_active_preset(int index, SlaSupportSettingsGroup group);

    void set_clipping_plane_position(double pos);
    void set_lock_island_supports(bool locked);

    /// Whether the section with the point settings is open. The tool opens it, so the settings are
    /// there when one goes into supporting an object (M2.17d4).
    void set_settings_expanded(bool expanded);

private:
    /// Lets every value slider of the window report when the user started and stopped editing its
    /// value, so that a drag of one of them is a single undo step (M2.6b).
    void report_value_editing(std::initializer_list<Yoga::SliderWithInput*> sliders);

    /// The controls of one of the two groups of support settings (M2.33): "New supports" (what a
    /// clicked point takes) and "Selected supports (N)" (what the selected points carry). Both have
    /// the same fields and their own preset row.
    struct SupportValueControls
    {
        Yoga::LayoutButton* preset_mini_button = nullptr;
        Yoga::LayoutButton* preset_light_button = nullptr;
        Yoga::LayoutButton* preset_medium_button = nullptr;
        Yoga::LayoutButton* preset_heavy_button = nullptr;
        Yoga::SliderWithInput* tip_diameter_slider = nullptr;
        Yoga::ToggleButton* tip_diameter_follow_global_checkbox = nullptr;
        Yoga::SliderWithInput* stem_diameter_slider = nullptr;
        Yoga::ToggleButton* stem_diameter_follow_global_checkbox = nullptr;
        Yoga::SliderWithInput* base_diameter_slider = nullptr;
        Yoga::ToggleButton* base_diameter_follow_global_checkbox = nullptr;
        Yoga::SliderWithInput* base_height_slider = nullptr;
        Yoga::ToggleButton* base_height_follow_global_checkbox = nullptr;
        Yoga::ComboBox* tip_shape_combo = nullptr;
        Yoga::SliderWithInput* tip_length_slider = nullptr;
        Yoga::SliderWithInput* knot_diameter_slider = nullptr;
        Yoga::SliderWithInput* stem_sides_slider = nullptr;
        Yoga::SliderWithInput* stem_taper_slider = nullptr;
        Yoga::ComboBox* foot_shape_combo = nullptr;
        Yoga::ComboBox* on_model_combo = nullptr;
        Yoga::ComboBox* brace_combo = nullptr;
        // Only the "Selected supports" group has it (M2.38): the other group is about what a
        // clicked point takes, and a point that is not there yet cannot be removed.
        Yoga::LayoutButton* delete_button = nullptr;
    };

    /// Builds one group of support settings: the preset row, the four sizes with their "follow the
    /// global setting" switches, and the per-point tip shape, tip length, knot, stem cross-section,
    /// stem taper, foot shape and "support on model".
    void add_support_value_group(
        Yoga::Item*           parent,
        SlaSupportSettingsGroup group,
        SupportValueControls& controls
    );

    /// Puts the values a clicked point takes into the controls of @p controls, which is one of the
    /// two groups.
    void show_new_support_values(SupportValueControls& controls, const SlaSupportNewValues& values);

    /// Puts what the selected points carry into @p controls. An empty @p geometry or @p sizes leaves
    /// the fields blank instead of showing a value only some of the selected points have, and a
    /// dropdown has no empty state of its own, so it says the one word the app uses for this.
    void show_selected_support_values(SupportValueControls& controls, const SlaSupportSelectionView& view);

    /// Shows one of the four sizes in a slider, or leaves it blank where the selection disagrees.
    static void show_size(Yoga::SliderWithInput* slider, bool has_value, double value_mm);

private:
    Yoga::CollapsibleWindow* m_settings_window = nullptr;
    // The keyboard shortcuts of the tool (M2.28), one line each, in a section of the settings that
    // is closed so that the list does not stand between the point settings and the presets.
    Yoga::CollapsibleWindow* m_shortcuts_window = nullptr;
    Yoga::CollapsibleWindow* m_new_supports_window = nullptr;
    Yoga::CollapsibleWindow* m_selected_supports_window = nullptr;
    Yoga::SliderWithInput* m_density_slider = nullptr;
    SupportValueControls m_new_supports;
    SupportValueControls m_selected_supports;
    Yoga::LayoutButton* m_generate_button = nullptr;
    Yoga::LayoutButton* m_auto_support_all_button = nullptr;
    Yoga::LayoutButton* m_apply_button = nullptr;
    Yoga::LayoutButton* m_discard_button = nullptr;
    Yoga::LayoutButton* m_remove_all_points_button = nullptr;
    Yoga::LayoutButton* m_clipping_plane_reset_button = nullptr;
    Yoga::ToggleButton* m_lock_island_supports_checkbox = nullptr;
    Yoga::SliderWithInput* m_clipping_plane_slider = nullptr;
    Yoga::Text* m_point_count_text = nullptr;

    Callbacks m_callbacks;
};

} // namespace Slic3r::App::Plater