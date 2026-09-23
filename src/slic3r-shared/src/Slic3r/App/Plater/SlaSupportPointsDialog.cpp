#include "Slic3r/App/Plater/SlaSupportPointsDialog.hpp"

#include "Slic3r/App/Yoga/SliderWithInput.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"
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

    add_row_with_slider(
        content(),
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
        content(),
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

    m_head_diameter_use_global_checkbox = content()->emplace_back<ToggleButton>(_u8L("Use global head diameter"));
    m_head_diameter_use_global_checkbox->callbacks().checked_changed = [this](bool value)
    { m_callbacks.head_diameter_use_global_changed(value); };

    add_row_with_slider(
        content(),
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

    m_pillar_diameter_use_global_checkbox = content()->emplace_back<ToggleButton>(_u8L("Use global stem diameter"));
    m_pillar_diameter_use_global_checkbox->callbacks().checked_changed = [this](bool value)
    { m_callbacks.pillar_diameter_use_global_changed(value); };

    add_row_with_slider(
        content(),
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

    m_base_diameter_use_global_checkbox = content()->emplace_back<ToggleButton>(_u8L("Use global base diameter"));
    m_base_diameter_use_global_checkbox->callbacks().checked_changed = [this](bool value)
    { m_callbacks.base_diameter_use_global_changed(value); };

    add_row_with_slider(
        content(),
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

    m_base_height_use_global_checkbox = content()->emplace_back<ToggleButton>(_u8L("Use global base height"));
    m_base_height_use_global_checkbox->callbacks().checked_changed = [this](bool value)
    { m_callbacks.base_height_use_global_changed(value); };

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

    this->add_separator(this->content());

    Item* preset_row = content()->emplace_back<Item>();
    preset_row->set_orientation(Orientation::Horizontal);
    preset_row->set_justify_content(YGJustifySpaceBetween);
    preset_row->set_gap(gap_size());

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

    this->add_separator(this->content());

    add_row_with_button(content(), &m_generate_button, _u8L("Generate"));
    m_generate_button->callbacks().action = [this]()
    { m_callbacks.generate(); };

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
}

void SlaSupportPointsDialog::set_density(int density)
{
    m_density_slider->set_value(static_cast<double>(density));
}

void SlaSupportPointsDialog::set_generate_enabled(bool enabled)
{
    m_generate_button->set_enabled(enabled);
}

void SlaSupportPointsDialog::set_apply_enabled(bool enabled)
{
    m_apply_button->set_enabled(enabled);
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

void SlaSupportPointsDialog::set_active_preset(int index)
{
    m_preset_light_button->set_checked(index == 0);
    m_preset_medium_button->set_checked(index == 1);
    m_preset_heavy_button->set_checked(index == 2);
}

} // namespace Slic3r::App::Plater