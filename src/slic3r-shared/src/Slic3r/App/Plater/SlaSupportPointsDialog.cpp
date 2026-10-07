#include "Slic3r/App/Plater/SlaSupportPointsDialog.hpp"
#include "Slic3r/App/Plater/SlaSupportToolShortcuts.hpp"

#include "Slic3r/App/Yoga/SliderWithInput.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/ComboBox.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"
#include "Slic3r/App/Yoga/CollapsibleWindow.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include <fmt/format.h>

using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;

namespace Slic3r::App::Plater {

SlaSupportPointsDialog::Callbacks& SlaSupportPointsDialog::callbacks()
{
    return m_callbacks;
}

SlaSupportPointsDialog::SlaSupportPointsDialog() : GizmoWindow()
{
    content()->set_padding(20.f);
    content()->set_gap(2.f * gap_size());

    // The point settings live in their own section, which is open when the tool opens, so that
    // going into supporting an object shows the settings right away (M2.17d4).
    m_settings_window = content()->emplace_back<CollapsibleWindow>(
        _u8L("Support point settings"),
        "SlaSupportPointsSettings"
    );
    m_settings_window->set_padding(0.f);
    m_settings_window->set_collapsed(false);
    Item* settings = m_settings_window->content();
    settings->set_padding(0.f);
    settings->set_gap(2.f * gap_size());

    add_row_with_slider(
        settings,
        &m_density_slider,
        _u8L("Support points density"),
        _u8L("%")
    );
    m_density_slider->set_begin_value(0);
    m_density_slider->set_end_value(200);
    m_density_slider->set_step(1);
    m_density_slider->callbacks().value_changed = [this](double value)
    { m_callbacks.density_changed(value); };

    // The support settings of the tool are two groups (M2.33), the way Chitubox shows them. "New
    // supports" is what a clicked point takes and changes no point that is already there, "Selected
    // supports (N)" is what the points of the selection carry and only ever changes those. One set
    // of fields doing both jobs at once is what made it unclear what a changed value touched.
    this->add_separator(settings);

    m_new_supports_window = settings->emplace_back<CollapsibleWindow>(
        _u8L("New supports"),
        "SlaSupportPointsNewSupports"
    );
    m_new_supports_window->set_padding(0.f);
    m_new_supports_window->set_collapsed(false);
    Item* new_supports = m_new_supports_window->content();
    new_supports->set_padding(0.f);
    new_supports->set_gap(2.f * gap_size());
    this->add_support_value_group(new_supports, SlaSupportSettingsGroup::NewSupports, m_new_supports);

    m_selected_supports_window = settings->emplace_back<CollapsibleWindow>(
        _u8L("Selected supports"),
        "SlaSupportPointsSelectedSupports"
    );
    m_selected_supports_window->set_padding(0.f);
    m_selected_supports_window->set_collapsed(false);
    Item* selected_supports = m_selected_supports_window->content();
    selected_supports->set_padding(0.f);
    selected_supports->set_gap(2.f * gap_size());
    this->add_support_value_group(
        selected_supports,
        SlaSupportSettingsGroup::SelectedSupports,
        m_selected_supports
    );
    // There is nothing to edit while no point is selected, so the group is only there with a
    // selection (M2.33).
    m_selected_supports_window->set_visible(false);

    add_row_with_slider(
        content(),
        &m_clipping_plane_slider,
        _u8L("Clipping of view"),
        _u8L("%")
    );
    m_clipping_plane_slider->set_begin_value(0.0);
    m_clipping_plane_slider->set_end_value(100.0);
    m_clipping_plane_slider->set_step(1.0);
    m_clipping_plane_slider->set_validator_precision(0);
    m_clipping_plane_slider->callbacks().value_changed = [this](double value)
    { m_callbacks.clipping_plane_changed(value / 100.0); };

    // The keyboard shortcuts of the tool (M2.28), one line each, at the end of the settings they
    // belong to. The section is closed, so the list is there for the one who looks for it without
    // standing between the point settings and the presets.
    this->add_separator(settings);

    m_shortcuts_window = settings->emplace_back<CollapsibleWindow>(
        _u8L("Shortcuts"),
        "SlaSupportPointsShortcuts"
    );
    m_shortcuts_window->set_padding(0.f);
    m_shortcuts_window->set_collapsed(true);
    Item* shortcuts = m_shortcuts_window->content();
    shortcuts->set_padding(0.f);
    for (const std::string& line : support_tool_shortcut_lines()) {
        Text* shortcut_line = shortcuts->emplace_back<Text>(line);
        shortcut_line->set_flex_shrink(0);
    }

    this->add_separator(this->content());

    add_row_with_button(content(), &m_generate_button, _u8L("Generate"));
    m_generate_button->callbacks().action = [this]()
    { m_callbacks.generate(); };

    add_row_with_button(content(), &m_auto_support_all_button, _u8L("Auto support all"));
    m_auto_support_all_button->callbacks().action = [this]()
    { m_callbacks.auto_support_all(); };

    this->add_separator(this->content());

    Item* button_row = content()->emplace_back<Item>();
    button_row->set_orientation(Orientation::Horizontal);
    button_row->set_justify_content(YGJustifySpaceBetween);
    button_row->set_gap(gap_size());

    m_apply_button = button_row->emplace_back<LayoutButton>(_u8L("Apply"));
    // Leaving the tool applies the generated points on its own, so Apply is for the user who wants
    // them on the model right away and sees the tree right there (M2.31).
    m_apply_button->set_tooltip(_u8L("Optional. The generated points are applied when you leave the tool."));
    m_apply_button->callbacks().action = [this]()
    { m_callbacks.apply(); };

    m_discard_button = button_row->emplace_back<LayoutButton>(_u8L("Discard"));
    m_discard_button->callbacks().action = [this]()
    { m_callbacks.discard(); };

    // Taking every point of the model away is here as well as in the Preview sidebar (M2.32): the
    // tool knows how many points the model has, and the action asks before it removes them.
    add_row_with_button(content(), &m_remove_all_points_button, _u8L("Remove all points"));
    m_remove_all_points_button->callbacks().action = [this]()
    { m_callbacks.remove_all_points(); };
    // Nothing to remove until the tool is on a model that has points.
    m_remove_all_points_button->set_enabled(false);

    this->add_separator(this->content());

    m_lock_island_supports_checkbox = content()->emplace_back<ToggleButton>(_u8L("Lock island supports"));
    m_lock_island_supports_checkbox->callbacks().checked_changed = [this](bool value)
    { m_callbacks.lock_island_supports_changed(value); };

    this->add_separator(this->content());

    Item* clipping_row = content()->emplace_back<Item>();
    clipping_row->set_orientation(Orientation::Horizontal);
    clipping_row->set_justify_content(YGJustifySpaceBetween);
    clipping_row->set_gap(gap_size());

    m_clipping_plane_reset_button = clipping_row->emplace_back<LayoutButton>(_u8L("Reset"));
    m_clipping_plane_reset_button->callbacks().action = [this]()
    { m_callbacks.clipping_plane_reset(); };

    this->add_separator(this->content());

    m_point_count_text = content()->emplace_back<Text>(_u8L("No support points generated yet."));
    m_point_count_text->set_flex_shrink(0);

    // Every slider that writes a value on the points or on the object reports the start and the end
    // of the edit, so a drag of one of them is a single undo step (M2.6b). The clipping plane is not
    // among them: it only moves where the scene is cut and never changes the model.
    report_value_editing({m_density_slider,
                          m_new_supports.tip_diameter_slider,
                          m_new_supports.stem_diameter_slider,
                          m_new_supports.base_diameter_slider,
                          m_new_supports.base_height_slider,
                          m_new_supports.tip_length_slider,
                          m_new_supports.knot_diameter_slider,
                          m_new_supports.stem_sides_slider,
                          m_new_supports.stem_taper_slider,
                          m_selected_supports.tip_diameter_slider,
                          m_selected_supports.stem_diameter_slider,
                          m_selected_supports.base_diameter_slider,
                          m_selected_supports.base_height_slider,
                          m_selected_supports.tip_length_slider,
                          m_selected_supports.knot_diameter_slider,
                          m_selected_supports.stem_sides_slider,
                          m_selected_supports.stem_taper_slider});
}

// One group of support settings: the preset row a user of Chitubox or Lychee starts with (M2.18,
// M2.22), then the four sizes with their "follow the global setting" switches, then the per-point
// tip shape, tip length, knot, stem cross-section, stem taper, foot shape, "support on model" and
// bracing. The "Selected supports" group ends with the button that removes what is selected.
// The presets are the tip classes of the support rulebook (M7.8.1, R3) since: a support is named by
// the size of its contact, so the buttons are the tip sizes in mm and one button is one class.
void SlaSupportPointsDialog::add_support_value_group(
    Yoga::Item*            parent,
    SlaSupportSettingsGroup group,
    SupportValueControls&   controls)
{
    const auto setting_changed = [this, group](SlaSupportPointField field, double value)
    { m_callbacks.support_setting_changed(group, field, value); };
    const auto preset_selected = [this, group](int index)
    { m_callbacks.support_preset_selected(group, index); };

    Item* preset_row = parent->emplace_back<Item>();
    preset_row->set_orientation(Orientation::Horizontal);
    preset_row->set_justify_content(YGJustifySpaceBetween);
    preset_row->set_gap(gap_size());

    // The buttons say what a class is: the diameter of its contact, in mm (M7.8.1).
    preset_row->emplace_back<Text>(_u8L("Tip (mm)"));

    controls.preset_t01_button = preset_row->emplace_back<LayoutButton>(_u8L("0.1"));
    controls.preset_t01_button->set_checkable(true);
    controls.preset_t01_button->callbacks().action = [preset_selected]()
    { preset_selected(0); };

    controls.preset_t02_button = preset_row->emplace_back<LayoutButton>(_u8L("0.2"));
    controls.preset_t02_button->set_checkable(true);
    controls.preset_t02_button->callbacks().action = [preset_selected]()
    { preset_selected(1); };

    controls.preset_t03_button = preset_row->emplace_back<LayoutButton>(_u8L("0.3"));
    controls.preset_t03_button->set_checkable(true);
    controls.preset_t03_button->callbacks().action = [preset_selected]()
    { preset_selected(2); };

    controls.preset_t04_button = preset_row->emplace_back<LayoutButton>(_u8L("0.4"));
    controls.preset_t04_button->set_checkable(true);
    controls.preset_t04_button->callbacks().action = [preset_selected]()
    { preset_selected(3); };

    controls.preset_t06_button = preset_row->emplace_back<LayoutButton>(_u8L("0.6"));
    controls.preset_t06_button->set_checkable(true);
    controls.preset_t06_button->callbacks().action = [preset_selected]()
    { preset_selected(4); };

    // The four sizes. The tip diameter is the head diameter control of the tool (M2.24) and the other
    // three are the stem and the base of the support, the values a point carries or leaves to the
    // global setting.
    add_row_with_slider(
        parent,
        &controls.tip_diameter_slider,
        _u8L("Head diameter"),
        _u8L("mm")
    );
    controls.tip_diameter_slider->set_begin_value(0.1);
    controls.tip_diameter_slider->set_end_value(5.0);
    controls.tip_diameter_slider->set_step(0.1);
    controls.tip_diameter_slider->set_validator_precision(1);
    controls.tip_diameter_slider->callbacks().value_changed =
        [setting_changed](double value) { setting_changed(SlaSupportPointField::TipDiameter, value); };

    controls.tip_diameter_follow_global_checkbox =
        parent->emplace_back<ToggleButton>(_u8L("Use global head diameter"));
    controls.tip_diameter_follow_global_checkbox->callbacks().checked_changed =
        [setting_changed](bool value)
    { setting_changed(SlaSupportPointField::FollowGlobalTipDiameter, value ? 1. : 0.); };

    add_row_with_slider(
        parent,
        &controls.stem_diameter_slider,
        _u8L("Stem diameter"),
        _u8L("mm")
    );
    controls.stem_diameter_slider->set_begin_value(0.1);
    controls.stem_diameter_slider->set_end_value(10.0);
    controls.stem_diameter_slider->set_step(0.1);
    controls.stem_diameter_slider->set_validator_precision(1);
    controls.stem_diameter_slider->callbacks().value_changed =
        [setting_changed](double value) { setting_changed(SlaSupportPointField::StemDiameter, value); };

    controls.stem_diameter_follow_global_checkbox =
        parent->emplace_back<ToggleButton>(_u8L("Use global stem diameter"));
    controls.stem_diameter_follow_global_checkbox->callbacks().checked_changed =
        [setting_changed](bool value)
    { setting_changed(SlaSupportPointField::FollowGlobalStemDiameter, value ? 1. : 0.); };

    add_row_with_slider(
        parent,
        &controls.base_diameter_slider,
        _u8L("Base diameter"),
        _u8L("mm")
    );
    controls.base_diameter_slider->set_begin_value(0.1);
    controls.base_diameter_slider->set_end_value(20.0);
    controls.base_diameter_slider->set_step(0.1);
    controls.base_diameter_slider->set_validator_precision(1);
    controls.base_diameter_slider->callbacks().value_changed =
        [setting_changed](double value) { setting_changed(SlaSupportPointField::BaseDiameter, value); };

    controls.base_diameter_follow_global_checkbox =
        parent->emplace_back<ToggleButton>(_u8L("Use global base diameter"));
    controls.base_diameter_follow_global_checkbox->callbacks().checked_changed =
        [setting_changed](bool value)
    { setting_changed(SlaSupportPointField::FollowGlobalBaseDiameter, value ? 1. : 0.); };

    add_row_with_slider(
        parent,
        &controls.base_height_slider,
        _u8L("Base height"),
        _u8L("mm")
    );
    controls.base_height_slider->set_begin_value(0.1);
    controls.base_height_slider->set_end_value(10.0);
    controls.base_height_slider->set_step(0.1);
    controls.base_height_slider->set_validator_precision(1);
    controls.base_height_slider->callbacks().value_changed =
        [setting_changed](double value) { setting_changed(SlaSupportPointField::BaseHeight, value); };

    controls.base_height_follow_global_checkbox =
        parent->emplace_back<ToggleButton>(_u8L("Use global base height"));
    controls.base_height_follow_global_checkbox->callbacks().checked_changed =
        [setting_changed](bool value)
    { setting_changed(SlaSupportPointField::FollowGlobalBaseHeight, value ? 1. : 0.); };

    // The per-point support geometry (M2.16c, M2.24): the tip shape and the knot around the tip, then
    // the cross-section and the taper of the stem. The tip diameter is not among them: it is the head
    // diameter row above, which is one of these fields.
    add_row_with_combo_box(_u8L("Tip shape"), parent, &controls.tip_shape_combo);
    controls.tip_shape_combo->set_items({_u8L("Default"), _u8L("Cone"), _u8L("Ball")});
    controls.tip_shape_combo->callbacks().selection_changed = [setting_changed](int index)
    {
        Domain::SLA::SupportPoint::TipShape shape = Domain::SLA::SupportPoint::TipShape::Default;
        switch (index) {
        case 1:
            shape = Domain::SLA::SupportPoint::TipShape::Cone;
            break;
        case 2:
            shape = Domain::SLA::SupportPoint::TipShape::Ball;
            break;
        default:
            break;
        }
        setting_changed(SlaSupportPointField::TipShape, sla_support_point_field_value(shape));
    };

    add_row_with_slider(
        parent,
        &controls.tip_length_slider,
        _u8L("Tip length"),
        _u8L("mm")
    );
    controls.tip_length_slider->set_begin_value(0);
    controls.tip_length_slider->set_end_value(20.0);
    controls.tip_length_slider->set_step(0.1);
    controls.tip_length_slider->set_validator_precision(1);
    controls.tip_length_slider->callbacks().value_changed =
        [setting_changed](double value) { setting_changed(SlaSupportPointField::TipLength, value); };

    add_row_with_slider(
        parent,
        &controls.knot_diameter_slider,
        _u8L("Knot diameter"),
        _u8L("mm")
    );
    controls.knot_diameter_slider->set_begin_value(0);
    controls.knot_diameter_slider->set_end_value(20.0);
    controls.knot_diameter_slider->set_step(0.1);
    controls.knot_diameter_slider->set_validator_precision(1);
    controls.knot_diameter_slider->callbacks().value_changed =
        [setting_changed](double value) { setting_changed(SlaSupportPointField::KnotDiameter, value); };

    add_row_with_slider(
        parent,
        &controls.stem_sides_slider,
        _u8L("Stem sides")
    );
    controls.stem_sides_slider->set_begin_value(0);
    controls.stem_sides_slider->set_end_value(64);
    controls.stem_sides_slider->set_step(1);
    controls.stem_sides_slider->set_validator_precision(0);
    controls.stem_sides_slider->callbacks().value_changed =
        [setting_changed](double value) { setting_changed(SlaSupportPointField::StemSides, value); };

    add_row_with_slider(
        parent,
        &controls.stem_taper_slider,
        _u8L("Stem taper")
    );
    controls.stem_taper_slider->set_begin_value(0.);
    controls.stem_taper_slider->set_end_value(1.);
    controls.stem_taper_slider->set_step(0.01);
    controls.stem_taper_slider->set_validator_precision(2);
    controls.stem_taper_slider->callbacks().value_changed =
        [setting_changed](double value) { setting_changed(SlaSupportPointField::StemTaper, value); };

    // The shape of the foot where the pillar meets the raft or the plate (M2.23b), the same three
    // shapes as the support_base_shape setting of "Supports & raft", which a new point takes.
    add_row_with_combo_box(_u8L("Foot shape"), parent, &controls.foot_shape_combo);
    controls.foot_shape_combo->set_items(
        {_u8L("Default"), _u8L("Cone"), _u8L("Cylinder"), _u8L("Flat disc")});
    controls.foot_shape_combo->callbacks().selection_changed = [setting_changed](int index)
    {
        Domain::SLA::SupportPoint::BaseShape shape = Domain::SLA::SupportPoint::BaseShape::Default;
        switch (index) {
        case 1:
            shape = Domain::SLA::SupportPoint::BaseShape::Cone;
            break;
        case 2:
            shape = Domain::SLA::SupportPoint::BaseShape::Cylinder;
            break;
        case 3:
            shape = Domain::SLA::SupportPoint::BaseShape::Flat;
            break;
        default:
            break;
        }
        setting_changed(SlaSupportPointField::FootShape, sla_support_point_field_value(shape));
    };

    // The per-point "may this support end on the model" switch (M2.26). It is not a dimension of the
    // support but where its pillar ends, so it sits with the geometry of the point, after the stem.
    // Inherit is what a point without a switch of its own gets, i.e. the object's own setting
    // decides.
    add_row_with_combo_box(_u8L("Support on model"), parent, &controls.on_model_combo);
    controls.on_model_combo->set_items({_u8L("Inherit"), _u8L("Allow"), _u8L("Forbid")});
    controls.on_model_combo->callbacks().selection_changed = [setting_changed](int index)
    {
        SupportOnModel on_model = SupportOnModel::Inherit;
        switch (index) {
        case 1:
            on_model = SupportOnModel::Allow;
            break;
        case 2:
            on_model = SupportOnModel::Forbid;
            break;
        default:
            break;
        }
        setting_changed(SlaSupportPointField::SupportOnModel, sla_support_point_field_value(on_model));
    };

    // The per-point bracing switch (M2.38), the second of the two values that are not dimensions of
    // the support but what its pillar does in the tree. Inherit is what a point without a switch of
    // its own gets, i.e. the object's own support_brace_enable decides.
    add_row_with_combo_box(_u8L("Bracing"), parent, &controls.brace_combo);
    controls.brace_combo->set_items({_u8L("Inherit"), _u8L("On"), _u8L("Off")});
    controls.brace_combo->tooltip().set_text(
        _u8L("Whether the pillar of this support is braced to its neighbours. Inherit follows the "
             "Bracing setting of Supports & raft. The branching tree has no braces and ignores this.")
    );
    controls.brace_combo->callbacks().selection_changed = [setting_changed](int index)
    {
        SupportBrace brace = SupportBrace::Inherit;
        switch (index) {
        case 1:
            brace = SupportBrace::On;
            break;
        case 2:
            brace = SupportBrace::Off;
            break;
        default:
            break;
        }
        setting_changed(SlaSupportPointField::Bracing, sla_support_point_field_value(brace));
    };

    // Removing what is selected belongs to the group that shows what is selected, so the button sits
    // there and nowhere else (M2.38). The Delete key and Ctrl+click take the same action.
    if (group == SlaSupportSettingsGroup::SelectedSupports) {
        controls.delete_button = parent->emplace_back<LayoutButton>(_u8L("Delete"));
        controls.delete_button->set_tooltip(
            _u8L("Remove the selected support points from this model. One undo brings them back.")
        );
        controls.delete_button->callbacks().action = [this]()
        { m_callbacks.delete_selected_points(); };
        // There is nothing to remove while no point is selected.
        controls.delete_button->set_enabled(false);
    }
}

void SlaSupportPointsDialog::show_size(SliderWithInput* slider, bool has_value, double value_mm)
{
    if (has_value) {
        slider->set_value(value_mm);
    } else {
        slider->set_undef_value();
    }
}

void SlaSupportPointsDialog::show_new_support_values(
    SupportValueControls&      controls,
    const SlaSupportNewValues& values)
{
    controls.tip_diameter_slider->set_value(values.sizes.tip_diameter_mm);
    controls.tip_diameter_follow_global_checkbox->set_checked(values.follow_global.tip_diameter);
    controls.tip_diameter_slider->set_enabled(!values.follow_global.tip_diameter);

    controls.stem_diameter_slider->set_value(values.sizes.stem_diameter_mm);
    controls.stem_diameter_follow_global_checkbox->set_checked(values.follow_global.stem_diameter);
    controls.stem_diameter_slider->set_enabled(!values.follow_global.stem_diameter);

    controls.base_diameter_slider->set_value(values.sizes.base_diameter_mm);
    controls.base_diameter_follow_global_checkbox->set_checked(values.follow_global.base_diameter);
    controls.base_diameter_slider->set_enabled(!values.follow_global.base_diameter);

    controls.base_height_slider->set_value(values.sizes.base_height_mm);
    controls.base_height_follow_global_checkbox->set_checked(values.follow_global.base_height);
    controls.base_height_slider->set_enabled(!values.follow_global.base_height);

    controls.tip_shape_combo->set_override_label(std::string());
    switch (values.geometry.tip_shape) {
    case Domain::SLA::SupportPoint::TipShape::Cone:
        controls.tip_shape_combo->set_current_index(1);
        break;
    case Domain::SLA::SupportPoint::TipShape::Ball:
        controls.tip_shape_combo->set_current_index(2);
        break;
    case Domain::SLA::SupportPoint::TipShape::Default:
    default:
        controls.tip_shape_combo->set_current_index(0);
        break;
    }
    controls.tip_length_slider->set_value(values.geometry.tip_length_mm);
    controls.knot_diameter_slider->set_value(values.geometry.knot_diameter_mm);
    controls.stem_sides_slider->set_value(values.geometry.stem_sides);
    controls.stem_taper_slider->set_value(values.geometry.stem_taper);

    controls.foot_shape_combo->set_override_label(std::string());
    switch (values.geometry.base_shape) {
    case Domain::SLA::SupportPoint::BaseShape::Cone:
        controls.foot_shape_combo->set_current_index(1);
        break;
    case Domain::SLA::SupportPoint::BaseShape::Cylinder:
        controls.foot_shape_combo->set_current_index(2);
        break;
    case Domain::SLA::SupportPoint::BaseShape::Flat:
        controls.foot_shape_combo->set_current_index(3);
        break;
    case Domain::SLA::SupportPoint::BaseShape::Default:
    default:
        controls.foot_shape_combo->set_current_index(0);
        break;
    }

    controls.on_model_combo->set_override_label(std::string());
    switch (values.on_model) {
    case SupportOnModel::Allow:
        controls.on_model_combo->set_current_index(1);
        break;
    case SupportOnModel::Forbid:
        controls.on_model_combo->set_current_index(2);
        break;
    case SupportOnModel::Inherit:
    default:
        controls.on_model_combo->set_current_index(0);
        break;
    }

    controls.brace_combo->set_override_label(std::string());
    switch (values.brace) {
    case SupportBrace::On:
        controls.brace_combo->set_current_index(1);
        break;
    case SupportBrace::Off:
        controls.brace_combo->set_current_index(2);
        break;
    case SupportBrace::Inherit:
    default:
        controls.brace_combo->set_current_index(0);
        break;
    }
}

void SlaSupportPointsDialog::show_selected_support_values(
    SupportValueControls&           controls,
    const SlaSupportSelectionView&  view)
{
    const bool has_sizes = view.sizes.has_value();
    show_size(controls.tip_diameter_slider, has_sizes, has_sizes ? view.sizes->tip_diameter_mm : 0.);
    show_size(controls.stem_diameter_slider, has_sizes, has_sizes ? view.sizes->stem_diameter_mm : 0.);
    show_size(controls.base_diameter_slider, has_sizes, has_sizes ? view.sizes->base_diameter_mm : 0.);
    show_size(controls.base_height_slider, has_sizes, has_sizes ? view.sizes->base_height_mm : 0.);

    // A point carries a size of zero to follow the global setting, so the box says whether the
    // selection leaves it to the settings of Supports & raft.
    controls.tip_diameter_follow_global_checkbox->set_checked(view.follow_global.tip_diameter);
    controls.tip_diameter_follow_global_checkbox->set_enabled(view.count > 0);
    controls.stem_diameter_follow_global_checkbox->set_checked(view.follow_global.stem_diameter);
    controls.stem_diameter_follow_global_checkbox->set_enabled(view.count > 0);
    controls.base_diameter_follow_global_checkbox->set_checked(view.follow_global.base_diameter);
    controls.base_diameter_follow_global_checkbox->set_enabled(view.count > 0);
    controls.base_height_follow_global_checkbox->set_checked(view.follow_global.base_height);
    controls.base_height_follow_global_checkbox->set_enabled(view.count > 0);

    const bool has_geometry = view.geometry.has_value();
    controls.tip_shape_combo->set_override_label(has_geometry ? std::string() : _u8L("Mixed"));
    if (has_geometry) {
        switch (view.geometry->tip_shape) {
        case Domain::SLA::SupportPoint::TipShape::Cone:
            controls.tip_shape_combo->set_current_index(1);
            break;
        case Domain::SLA::SupportPoint::TipShape::Ball:
            controls.tip_shape_combo->set_current_index(2);
            break;
        case Domain::SLA::SupportPoint::TipShape::Default:
        default:
            controls.tip_shape_combo->set_current_index(0);
            break;
        }
        controls.tip_length_slider->set_value(view.geometry->tip_length_mm);
        controls.knot_diameter_slider->set_value(view.geometry->knot_diameter_mm);
        controls.stem_sides_slider->set_value(view.geometry->stem_sides);
        controls.stem_taper_slider->set_value(view.geometry->stem_taper);
    } else {
        controls.tip_length_slider->set_undef_value();
        controls.knot_diameter_slider->set_undef_value();
        controls.stem_sides_slider->set_undef_value();
        controls.stem_taper_slider->set_undef_value();
    }

    controls.foot_shape_combo->set_override_label(has_geometry ? std::string() : _u8L("Mixed"));
    if (has_geometry) {
        switch (view.geometry->base_shape) {
        case Domain::SLA::SupportPoint::BaseShape::Cone:
            controls.foot_shape_combo->set_current_index(1);
            break;
        case Domain::SLA::SupportPoint::BaseShape::Cylinder:
            controls.foot_shape_combo->set_current_index(2);
            break;
        case Domain::SLA::SupportPoint::BaseShape::Flat:
            controls.foot_shape_combo->set_current_index(3);
            break;
        case Domain::SLA::SupportPoint::BaseShape::Default:
        default:
            controls.foot_shape_combo->set_current_index(0);
            break;
        }
    }

    const std::optional<SupportOnModel> on_model = view.on_model;
    controls.on_model_combo->set_override_label(on_model.has_value() ? std::string() : _u8L("Mixed"));
    switch (on_model.value_or(SupportOnModel::Inherit)) {
    case SupportOnModel::Allow:
        controls.on_model_combo->set_current_index(1);
        break;
    case SupportOnModel::Forbid:
        controls.on_model_combo->set_current_index(2);
        break;
    case SupportOnModel::Inherit:
    default:
        controls.on_model_combo->set_current_index(0);
        break;
    }

    const std::optional<SupportBrace> brace = view.brace;
    controls.brace_combo->set_override_label(brace.has_value() ? std::string() : _u8L("Mixed"));
    switch (brace.value_or(SupportBrace::Inherit)) {
    case SupportBrace::On:
        controls.brace_combo->set_current_index(1);
        break;
    case SupportBrace::Off:
        controls.brace_combo->set_current_index(2);
        break;
    case SupportBrace::Inherit:
    default:
        controls.brace_combo->set_current_index(0);
        break;
    }

    // Nothing is selected in a hidden group, so there is nothing to remove either (M2.38).
    if (controls.delete_button != nullptr) {
        controls.delete_button->set_enabled(view.count > 0);
    }
}

void SlaSupportPointsDialog::set_density(int density)
{
    m_density_slider->set_value(static_cast<double>(density));
}

void SlaSupportPointsDialog::set_generate_enabled(bool enabled)
{
    m_generate_button->set_enabled(enabled);
}

void SlaSupportPointsDialog::set_auto_support_all_enabled(bool enabled)
{
    m_auto_support_all_button->set_enabled(enabled);
}

void SlaSupportPointsDialog::set_apply_enabled(bool enabled)
{
    m_apply_button->set_enabled(enabled);
}

void SlaSupportPointsDialog::set_remove_all_points_enabled(bool enabled)
{
    m_remove_all_points_button->set_enabled(enabled);
}

void SlaSupportPointsDialog::set_point_count(size_t count)
{
    if (count == 0) {
        m_point_count_text->set_text(_u8L("No support points generated yet."));
    } else {
        m_point_count_text->set_text(fmt::format("{} support points generated", count));
    }
}

void SlaSupportPointsDialog::set_new_support_values(const SlaSupportNewValues& values)
{
    this->show_new_support_values(m_new_supports, values);
}

void SlaSupportPointsDialog::set_selected_support_values(const SlaSupportSelectionView& view)
{
    this->show_selected_support_values(m_selected_supports, view);
    // The group is only there while points are selected, and its title names how many (M2.33).
    m_selected_supports_window->set_visible(view.count > 0);
    m_selected_supports_window->set_label(
        fmt::format("{} ({})", _u8L("Selected supports"), view.count));
}

void SlaSupportPointsDialog::set_active_preset(int index, SlaSupportSettingsGroup group)
{
    SupportValueControls& controls = group == SlaSupportSettingsGroup::NewSupports ? m_new_supports
                                                                                   : m_selected_supports;
    controls.preset_t01_button->set_checked(index == 0);
    controls.preset_t02_button->set_checked(index == 1);
    controls.preset_t03_button->set_checked(index == 2);
    controls.preset_t04_button->set_checked(index == 3);
    controls.preset_t06_button->set_checked(index == 4);
}

void SlaSupportPointsDialog::set_clipping_plane_position(double pos)
{
    m_clipping_plane_slider->set_value(pos * 100.0);
}

void SlaSupportPointsDialog::set_lock_island_supports(bool locked)
{
    m_lock_island_supports_checkbox->set_checked(locked);
}

void SlaSupportPointsDialog::set_settings_expanded(bool expanded)
{
    m_settings_window->set_collapsed(!expanded);
}

void SlaSupportPointsDialog::report_value_editing(std::initializer_list<SliderWithInput*> sliders)
{
    for (SliderWithInput* slider : sliders) {
        slider->callbacks().value_editing_started = [this]()
        { m_callbacks.value_editing_started(); };
        slider->callbacks().value_editing_ended = [this]()
        { m_callbacks.value_editing_ended(); };
    }
}

} // namespace Slic3r::App::Plater