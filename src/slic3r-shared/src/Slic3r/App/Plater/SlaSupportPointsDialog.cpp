#include "Slic3r/App/Plater/SlaSupportPointsDialog.hpp"

#include "Slic3r/App/Yoga/SliderWithInput.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
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

    this->add_separator(this->content());

    add_row_with_button(content(), &m_generate_button, _u8L("Generate"));
    m_generate_button->callbacks().action = [this]()
    { m_callbacks.generate(); };

    this->add_separator(this->content());

    Item* button_row = content()->emplace<Item>();
    button_row->set_flex_direction(YGDirectionRow);
    button_row->set_justify_content(YGJustifySpaceBetween);
    button_row->set_gap(gap_size());

    m_apply_button = button_row->emplace<LayoutButton>(0, _u8L("Apply"));
    m_apply_button->callbacks().action = [this]()
    { m_callbacks.apply(); };

    m_discard_button = button_row->emplace<LayoutButton>(0, _u8L("Discard"));
    m_discard_button->callbacks().action = [this]()
    { m_callbacks.discard(); };

    this->add_separator(this->content());

    m_point_count_text = content()->emplace<Text>(_u8L("No support points generated yet."));
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
        m_point_count_text->set_text(fmt::format(_u8L("{} support points generated"), count));
    }
}

} // namespace Slic3r::App::Plater