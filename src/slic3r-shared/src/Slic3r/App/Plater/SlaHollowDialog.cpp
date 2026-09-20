#include "Slic3r/App/Plater/SlaHollowDialog.hpp"

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

SlaHollowDialog::Callbacks& SlaHollowDialog::callbacks()
{
    return m_callbacks;
}

SlaHollowDialog::SlaHollowDialog() : GizmoWindow()
{
    content()->set_padding(20.f);
    content()->set_gap(2.f * gap_size());

    add_row_with_toggle_button(
        _u8L("Enable hollowing"),
        content(),
        &m_enable_checkbox
    );
    m_enable_checkbox->callbacks().checked_changed = [this](bool value)
    { m_callbacks.enable_changed(value); };

    this->add_separator(this->content());

    add_row_with_slider(
        content(),
        &m_min_thickness_slider,
        _u8L("Wall thickness"),
        _u8L("mm")
    );
    m_min_thickness_slider->set_begin_value(1.0);
    m_min_thickness_slider->set_end_value(10.0);
    m_min_thickness_slider->set_step(0.1);
    m_min_thickness_slider->set_validator_precision(1);
    m_min_thickness_slider->callbacks().value_changed = [this](double value)
    { m_callbacks.min_thickness_changed(value); };

    add_row_with_slider(
        content(),
        &m_quality_slider,
        _u8L("Accuracy"),
        std::string()
    );
    m_quality_slider->set_begin_value(0.0);
    m_quality_slider->set_end_value(1.0);
    m_quality_slider->set_step(0.01);
    m_quality_slider->set_validator_precision(2);
    m_quality_slider->callbacks().value_changed = [this](double value)
    { m_callbacks.quality_changed(value); };

    add_row_with_slider(
        content(),
        &m_closing_distance_slider,
        _u8L("Closing distance"),
        _u8L("mm")
    );
    m_closing_distance_slider->set_begin_value(0.0);
    m_closing_distance_slider->set_end_value(10.0);
    m_closing_distance_slider->set_step(0.1);
    m_closing_distance_slider->set_validator_precision(1);
    m_closing_distance_slider->callbacks().value_changed = [this](double value)
    { m_callbacks.closing_distance_changed(value); };

    this->add_separator(this->content());

    add_row_with_button(content(), &m_preview_button, _u8L("Preview"));
    m_preview_button->callbacks().action = [this]()
    { m_callbacks.preview(); };

    this->add_separator(this->content());

    add_row_with_slider(
        content(),
        &m_hole_radius_slider,
        _u8L("Hole radius"),
        _u8L("mm")
    );
    m_hole_radius_slider->set_begin_value(0.5);
    m_hole_radius_slider->set_end_value(25.0);
    m_hole_radius_slider->set_step(0.1);
    m_hole_radius_slider->set_validator_precision(1);
    m_hole_radius_slider->callbacks().value_changed = [this](double value)
    { m_callbacks.hole_radius_changed(value); };

    add_row_with_slider(
        content(),
        &m_hole_height_slider,
        _u8L("Hole depth"),
        _u8L("mm")
    );
    m_hole_height_slider->set_begin_value(0.0);
    m_hole_height_slider->set_end_value(50.0);
    m_hole_height_slider->set_step(0.1);
    m_hole_height_slider->set_validator_precision(1);
    m_hole_height_slider->callbacks().value_changed = [this](double value)
    { m_callbacks.hole_height_changed(value); };

    this->add_separator(this->content());

    Item* hole_buttons_row = content()->emplace_back<Item>();
    hole_buttons_row->set_orientation(Orientation::Horizontal);
    hole_buttons_row->set_justify_content(YGJustifySpaceBetween);
    hole_buttons_row->set_gap(gap_size());

    m_remove_selected_button = hole_buttons_row->emplace_back<LayoutButton>(_u8L("Remove selected"));
    m_remove_selected_button->callbacks().action = [this]()
    { m_callbacks.remove_selected_holes(); };

    m_remove_all_button = hole_buttons_row->emplace_back<LayoutButton>(_u8L("Remove all"));
    m_remove_all_button->callbacks().action = [this]()
    { m_callbacks.remove_all_holes(); };

    this->add_separator(this->content());

    m_hole_count_text = content()->emplace_back<Text>(_u8L("No drain holes."));
    m_hole_count_text->set_flex_shrink(0);

    m_status_text = content()->emplace_back<Text>(_u8L("No preview generated yet."));
    m_status_text->set_flex_shrink(0);
}

void SlaHollowDialog::set_enable(bool enabled)
{
    m_enable_checkbox->set_checked(enabled);
    m_min_thickness_slider->set_enabled(enabled);
    m_quality_slider->set_enabled(enabled);
    m_closing_distance_slider->set_enabled(enabled);
}

void SlaHollowDialog::set_preview_enabled(bool enabled)
{
    m_preview_button->set_enabled(enabled);
}

void SlaHollowDialog::set_min_thickness(double thickness_mm)
{
    m_min_thickness_slider->set_value(thickness_mm);
}

void SlaHollowDialog::set_quality(double quality)
{
    m_quality_slider->set_value(quality);
}

void SlaHollowDialog::set_closing_distance(double distance_mm)
{
    m_closing_distance_slider->set_value(distance_mm);
}

void SlaHollowDialog::set_hole_radius(double radius_mm)
{
    m_hole_radius_slider->set_value(radius_mm);
}

void SlaHollowDialog::set_hole_height(double height_mm)
{
    m_hole_height_slider->set_value(height_mm);
}

void SlaHollowDialog::set_hole_count(size_t count)
{
    if (count == 0) {
        m_hole_count_text->set_text(_u8L("No drain holes."));
    } else {
        m_hole_count_text->set_text(fmt::format("{} drain hole{}", count, count == 1 ? "" : "s"));
    }
}

void SlaHollowDialog::set_status(const std::string& status)
{
    m_status_text->set_text(status);
}

void SlaHollowDialog::set_holes_controls_enabled(bool enabled)
{
    m_hole_radius_slider->set_enabled(enabled);
    m_hole_height_slider->set_enabled(enabled);
    m_remove_selected_button->set_enabled(enabled);
    m_remove_all_button->set_enabled(enabled);
}

void SlaHollowDialog::set_remove_selected_enabled(bool enabled)
{
    m_remove_selected_button->set_enabled(enabled);
}

void SlaHollowDialog::set_remove_all_enabled(bool enabled)
{
    m_remove_all_button->set_enabled(enabled);
}

} // namespace Slic3r::App::Plater