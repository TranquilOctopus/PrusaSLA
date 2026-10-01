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

    add_row_with_slider(
        settings,
        &m_head_diameter_slider,
        _u8L("Head diameter"),
        _u8L("mm")
    );
    m_head_diameter_slider->set_begin_value(0.1);
    m_head_diameter_slider->set_end_value(5.0);
    m_head_diameter_slider->set_step(0.1);
    m_head_diameter_slider->set_validator_precision(1);
    m_head_diameter_slider->callbacks().value_changed = [this](double value)
    { m_callbacks.head_diameter_changed(value); };

    m_head_diameter_use_global_checkbox = settings->emplace_back<ToggleButton>(_u8L("Use global head diameter"));
    m_head_diameter_use_global_checkbox->callbacks().checked_changed = [this](bool value)
    { m_callbacks.head_diameter_use_global_changed(value); };

    add_row_with_slider(
        settings,
        &m_pillar_diameter_slider,
        _u8L("Stem diameter"),
        _u8L("mm")
    );
    m_pillar_diameter_slider->set_begin_value(0.1);
    m_pillar_diameter_slider->set_end_value(10.0);
    m_pillar_diameter_slider->set_step(0.1);
    m_pillar_diameter_slider->set_validator_precision(1);
    m_pillar_diameter_slider->callbacks().value_changed = [this](double value)
    { m_callbacks.pillar_diameter_changed(value); };

    m_pillar_diameter_use_global_checkbox = settings->emplace_back<ToggleButton>(_u8L("Use global stem diameter"));
    m_pillar_diameter_use_global_checkbox->callbacks().checked_changed = [this](bool value)
    { m_callbacks.pillar_diameter_use_global_changed(value); };

    add_row_with_slider(
        settings,
        &m_base_diameter_slider,
        _u8L("Base diameter"),
        _u8L("mm")
    );
    m_base_diameter_slider->set_begin_value(0.1);
    m_base_diameter_slider->set_end_value(20.0);
    m_base_diameter_slider->set_step(0.1);
    m_base_diameter_slider->set_validator_precision(1);
    m_base_diameter_slider->callbacks().value_changed = [this](double value)
    { m_callbacks.base_diameter_changed(value); };

    m_base_diameter_use_global_checkbox = settings->emplace_back<ToggleButton>(_u8L("Use global base diameter"));
    m_base_diameter_use_global_checkbox->callbacks().checked_changed = [this](bool value)
    { m_callbacks.base_diameter_use_global_changed(value); };

    add_row_with_slider(
        settings,
        &m_base_height_slider,
        _u8L("Base height"),
        _u8L("mm")
    );
    m_base_height_slider->set_begin_value(0.1);
    m_base_height_slider->set_end_value(10.0);
    m_base_height_slider->set_step(0.1);
    m_base_height_slider->set_validator_precision(1);
    m_base_height_slider->callbacks().value_changed = [this](double value)
    { m_callbacks.base_height_changed(value); };

    m_base_height_use_global_checkbox = settings->emplace_back<ToggleButton>(_u8L("Use global base height"));
    m_base_height_use_global_checkbox->callbacks().checked_changed = [this](bool value)
    { m_callbacks.base_height_use_global_changed(value); };

    // The per-point support geometry (M2.16c, M2.24): the tip shape and the knot around the tip,
    // then the cross-section and the taper of the stem. They take no global override, they are the
    // values a new point is placed with. The tip diameter is not here: it is the head diameter
    // slider above, which is one of these fields and the value a new point is placed with.
    this->add_separator(settings);

    add_row_with_combo_box(_u8L("Tip shape"), settings, &m_tip_shape_combo);
    m_tip_shape_combo->set_items({_u8L("Default"), _u8L("Cone"), _u8L("Ball")});
    m_tip_shape_combo->callbacks().selection_changed = [this](int index)
    {
        switch (index) {
        case 1:
            m_callbacks.tip_shape_changed(Domain::SLA::SupportPoint::TipShape::Cone);
            break;
        case 2:
            m_callbacks.tip_shape_changed(Domain::SLA::SupportPoint::TipShape::Ball);
            break;
        default:
            m_callbacks.tip_shape_changed(Domain::SLA::SupportPoint::TipShape::Default);
            break;
        }
    };

    add_row_with_slider(
        settings,
        &m_tip_length_slider,
        _u8L("Tip length"),
        _u8L("mm")
    );
    m_tip_length_slider->set_begin_value(0);
    m_tip_length_slider->set_end_value(20.0);
    m_tip_length_slider->set_step(0.1);
    m_tip_length_slider->set_validator_precision(1);
    m_tip_length_slider->callbacks().value_changed = [this](double value)
    { m_callbacks.tip_length_changed(value); };

    add_row_with_slider(
        settings,
        &m_knot_diameter_slider,
        _u8L("Knot diameter"),
        _u8L("mm")
    );
    m_knot_diameter_slider->set_begin_value(0);
    m_knot_diameter_slider->set_end_value(20.0);
    m_knot_diameter_slider->set_step(0.1);
    m_knot_diameter_slider->set_validator_precision(1);
    m_knot_diameter_slider->callbacks().value_changed = [this](double value)
    { m_callbacks.knot_diameter_changed(value); };

    add_row_with_slider(
        settings,
        &m_stem_sides_slider,
        _u8L("Stem sides")
    );
    m_stem_sides_slider->set_begin_value(0);
    m_stem_sides_slider->set_end_value(64);
    m_stem_sides_slider->set_step(1);
    m_stem_sides_slider->set_validator_precision(0);
    m_stem_sides_slider->callbacks().value_changed = [this](double value)
    { m_callbacks.stem_sides_changed(value); };

    add_row_with_slider(
        settings,
        &m_stem_taper_slider,
        _u8L("Stem taper")
    );
    m_stem_taper_slider->set_begin_value(0.);
    m_stem_taper_slider->set_end_value(1.);
    m_stem_taper_slider->set_step(0.01);
    m_stem_taper_slider->set_validator_precision(2);
    m_stem_taper_slider->callbacks().value_changed = [this](double value)
    { m_callbacks.stem_taper_changed(value); };

    // The per-point "may this support end on the model" switch (M2.26). It is not a dimension of
    // the support but where its pillar ends, so it sits with the geometry of the point, after the
    // stem. Inherit is what a point without a switch of its own gets, i.e. the object's own
    // setting decides.
    add_row_with_combo_box(_u8L("Support on model"), settings, &m_on_model_combo);
    m_on_model_combo->set_items({_u8L("Inherit"), _u8L("Allow"), _u8L("Forbid")});
    m_on_model_combo->callbacks().selection_changed = [this](int index)
    {
        switch (index) {
        case 1:
            m_callbacks.on_model_changed(SupportOnModel::Allow);
            break;
        case 2:
            m_callbacks.on_model_changed(SupportOnModel::Forbid);
            break;
        default:
            m_callbacks.on_model_changed(SupportOnModel::Inherit);
            break;
        }
    };

    // The shape of the foot where the pillar of the selected points meets the raft or the plate
    // (M2.23b). It sits with the rest of the per-point geometry and names the same three shapes as
    // the support_base_shape setting of "Supports & raft", which a new point takes.
    add_row_with_combo_box(_u8L("Foot shape"), settings, &m_base_shape_combo);
    m_base_shape_combo->set_items(
        {_u8L("Default"), _u8L("Cone"), _u8L("Cylinder"), _u8L("Flat disc")});
    m_base_shape_combo->callbacks().selection_changed = [this](int index)
    {
        switch (index) {
        case 1:
            m_callbacks.base_shape_changed(Domain::SLA::SupportPoint::BaseShape::Cone);
            break;
        case 2:
            m_callbacks.base_shape_changed(Domain::SLA::SupportPoint::BaseShape::Cylinder);
            break;
        case 3:
            m_callbacks.base_shape_changed(Domain::SLA::SupportPoint::BaseShape::Flat);
            break;
        default:
            m_callbacks.base_shape_changed(Domain::SLA::SupportPoint::BaseShape::Default);
            break;
        }
    };

    add_row_with_slider(
        content(),
        &m_clipping_plane_slider,
        _u8L("Clipping of view"),
        _u8L("%")
    );
    m_clipping_plane_slider->set_begin_value(0.0);
    m_clipping_plane_slider->set_end_value(1.0);
    m_clipping_plane_slider->set_step(0.01);
    m_clipping_plane_slider->set_validator_precision(2);
    m_clipping_plane_slider->callbacks().value_changed = [this](double value)
    { m_callbacks.clipping_plane_changed(value); };

    this->add_separator(settings);

    Item* preset_row = settings->emplace_back<Item>();
    preset_row->set_orientation(Orientation::Horizontal);
    preset_row->set_justify_content(YGJustifySpaceBetween);
    preset_row->set_gap(gap_size());

    m_preset_mini_button = preset_row->emplace_back<LayoutButton>(_u8L("Mini"));
    m_preset_mini_button->set_checkable(true);
    m_preset_mini_button->callbacks().action = [this]()
    { m_callbacks.preset_mini(); };

    m_preset_light_button = preset_row->emplace_back<LayoutButton>(_u8L("Light"));
    m_preset_light_button->set_checkable(true);
    m_preset_light_button->callbacks().action = [this]()
    { m_callbacks.preset_light(); };

    m_preset_medium_button = preset_row->emplace_back<LayoutButton>(_u8L("Medium"));
    m_preset_medium_button->set_checkable(true);
    m_preset_medium_button->callbacks().action = [this]()
    { m_callbacks.preset_medium(); };

    m_preset_heavy_button = preset_row->emplace_back<LayoutButton>(_u8L("Heavy"));
    m_preset_heavy_button->set_checkable(true);
    m_preset_heavy_button->callbacks().action = [this]()
    { m_callbacks.preset_heavy(); };

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
                          m_head_diameter_slider,
                          m_pillar_diameter_slider,
                          m_base_diameter_slider,
                          m_base_height_slider,
                          m_tip_length_slider,
                          m_knot_diameter_slider,
                          m_stem_sides_slider,
                          m_stem_taper_slider});
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

void SlaSupportPointsDialog::set_head_diameter(double diameter_mm)
{
    m_head_diameter_slider->set_value(diameter_mm);
}

void SlaSupportPointsDialog::set_clipping_plane_position(double pos)
{
    m_clipping_plane_slider->set_value(pos);
}

void SlaSupportPointsDialog::set_lock_island_supports(bool locked)
{
    m_lock_island_supports_checkbox->set_checked(locked);
}

void SlaSupportPointsDialog::set_pillar_diameter(double diameter_mm)
{
    m_pillar_diameter_slider->set_value(diameter_mm);
}

void SlaSupportPointsDialog::set_base_diameter(double diameter_mm)
{
    m_base_diameter_slider->set_value(diameter_mm);
}

void SlaSupportPointsDialog::set_base_height(double height_mm)
{
    m_base_height_slider->set_value(height_mm);
}

void SlaSupportPointsDialog::set_head_diameter_use_global(bool use_global)
{
    m_head_diameter_use_global_checkbox->set_checked(use_global);
    m_head_diameter_slider->set_enabled(!use_global);
}

void SlaSupportPointsDialog::set_pillar_diameter_use_global(bool use_global)
{
    m_pillar_diameter_use_global_checkbox->set_checked(use_global);
    m_pillar_diameter_slider->set_enabled(!use_global);
}

void SlaSupportPointsDialog::set_base_diameter_use_global(bool use_global)
{
    m_base_diameter_use_global_checkbox->set_checked(use_global);
    m_base_diameter_slider->set_enabled(!use_global);
}

void SlaSupportPointsDialog::set_base_height_use_global(bool use_global)
{
    m_base_height_use_global_checkbox->set_checked(use_global);
    m_base_height_slider->set_enabled(!use_global);
}

void SlaSupportPointsDialog::set_support_geometry(
    const std::optional<SlaSupportGeometry>& geometry,
    bool has_selection)
{
    if (!geometry.has_value()) {
        // Nothing is selected, or the selected points disagree on one of the values: the fields
        // show no value, so the user is never shown a value only some of the points have. A
        // dropdown has no empty state of its own, so it says the one word the app uses for this.
        m_tip_shape_combo->set_override_label(_u8L("Mixed"));
        m_base_shape_combo->set_override_label(_u8L("Mixed"));
        m_tip_length_slider->set_undef_value();
        m_knot_diameter_slider->set_undef_value();
        m_stem_sides_slider->set_undef_value();
        m_stem_taper_slider->set_undef_value();
        // The tip diameter is the head diameter control as well. While the control takes
        // the global diameter it shows that one, so only a selection that disagrees blanks it, and
        // with nothing selected it keeps showing the diameter a new point takes.
        if (has_selection && !m_head_diameter_use_global_checkbox->checked()) {
            m_head_diameter_slider->set_undef_value();
        }
        return;
    }

    m_tip_shape_combo->set_override_label(std::string());
    switch (geometry->tip_shape) {
    case Domain::SLA::SupportPoint::TipShape::Cone:
        m_tip_shape_combo->set_current_index(1);
        break;
    case Domain::SLA::SupportPoint::TipShape::Ball:
        m_tip_shape_combo->set_current_index(2);
        break;
    case Domain::SLA::SupportPoint::TipShape::Default:
    default:
        m_tip_shape_combo->set_current_index(0);
        break;
    }
    // While the control takes the global diameter it shows that one, the way the stem, base diameter
    // and base height sliders do; otherwise it shows what the selected points carry.
    if (!m_head_diameter_use_global_checkbox->checked()) {
        m_head_diameter_slider->set_value(geometry->tip_diameter_mm);
    }
    m_tip_length_slider->set_value(geometry->tip_length_mm);
    m_knot_diameter_slider->set_value(geometry->knot_diameter_mm);
    m_stem_sides_slider->set_value(geometry->stem_sides);
    m_stem_taper_slider->set_value(geometry->stem_taper);
    m_base_shape_combo->set_override_label(std::string());
    switch (geometry->base_shape) {
    case Domain::SLA::SupportPoint::BaseShape::Cone:
        m_base_shape_combo->set_current_index(1);
        break;
    case Domain::SLA::SupportPoint::BaseShape::Cylinder:
        m_base_shape_combo->set_current_index(2);
        break;
    case Domain::SLA::SupportPoint::BaseShape::Flat:
        m_base_shape_combo->set_current_index(3);
        break;
    case Domain::SLA::SupportPoint::BaseShape::Default:
    default:
        m_base_shape_combo->set_current_index(0);
        break;
    }
}

void SlaSupportPointsDialog::set_support_on_model(const std::optional<SupportOnModel>& on_model)
{
    if (!on_model.has_value()) {
        // Nothing is selected, or the selected points disagree: a dropdown has no empty state of
        // its own, so it says the one word the app uses for this.
        m_on_model_combo->set_override_label(_u8L("Mixed"));
        return;
    }

    m_on_model_combo->set_override_label(std::string());
    switch (*on_model) {
    case SupportOnModel::Allow:
        m_on_model_combo->set_current_index(1);
        break;
    case SupportOnModel::Forbid:
        m_on_model_combo->set_current_index(2);
        break;
    case SupportOnModel::Inherit:
    default:
        m_on_model_combo->set_current_index(0);
        break;
    }
}

void SlaSupportPointsDialog::set_active_preset(int index)
{
    m_preset_mini_button->set_checked(index == 0);
    m_preset_light_button->set_checked(index == 1);
    m_preset_medium_button->set_checked(index == 2);
    m_preset_heavy_button->set_checked(index == 3);
}

void SlaSupportPointsDialog::set_settings_expanded(bool expanded)
{
    m_settings_window->set_collapsed(!expanded);
}

} // namespace Slic3r::App::Plater
