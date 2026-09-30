#include "Slic3r/App/ResinDatasheetDialog.hpp"

#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/Theme.hpp"
#include "Slic3r/App/Yoga/InputTextField.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"
#include "Slic3r/App/Yoga/Text.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

#include <string>
#include <utility>
#include <vector>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

namespace {

/// @brief Width of the label column, so the values line up under each other.
constexpr Unit label_width{140.f};
constexpr Unit row_gap{6.f};
constexpr float full_width_percent = 100.f;
/// @brief Size of the line that says which fields may stay empty, so it reads as a note and not as
/// another field.
constexpr float note_font_scale = 0.9f;

} // namespace

ResinDatasheetDialog::ResinDatasheetDialog(Navigator& navigator) :
    Dialog({Biz::_u8L("New resin from datasheet")}, "ResinDatasheetDialog"),
    m_navigator(navigator)
{
    set_closable(true);
    content_item()->set_modal(true);
    content_item()->set_width(460);

    content()->set_orientation(Orientation::Vertical);
    content()->set_gap(row_gap);

    // What this form is for, because a datasheet is not a file and the user may be looking for the
    // import button. Bold: it is the one line that says what the dialog does.
    Text* intro = content()->emplace_back<Text>(Biz::_u8L(
        "Type what the resin datasheet states. The values are mapped and reviewed the same way a "
        "profile read from a file is."
    ));
    intro->set_width_percent(full_width_percent);
    intro->set_font_type(Render::ImguiFontType::Bold);
    intro->set_text_color(m_theme->color_imgui(Platform::Color::Text));
    intro->set_wrap_mode(Text::WrapMode::Wrap);

    // Why the form cannot go on. Hidden while there is nothing to say, so no line is reserved for it
    // on every form.
    m_error_line = content()->emplace_back<Text>(std::string{});
    m_error_line->set_width_percent(full_width_percent);
    m_error_line->set_text_color(m_theme->color_imgui(Platform::Color::Error));
    m_error_line->set_wrap_mode(Text::WrapMode::Wrap);
    m_error_line->set_visible(false);

    content()->emplace_back<Separator>(Orientation::Horizontal);

    // A row of the form: a fixed width label, then the field that answers it.
    auto labeled_row = [this](const std::string& label) -> Item*
    {
        Item* row = content()->emplace_back<Item>();
        row->set_orientation(Orientation::Horizontal);
        row->set_gap(row_gap);
        row->set_align_items(YGAlignCenter);
        row->set_flex_shrink(0.f);
        row->set_width_percent(full_width_percent);
        Text* label_item = row->emplace_back<Text>(label);
        label_item->set_width(label_width);
        label_item->set_flex_shrink(0.f);
        label_item->set_text_color(m_theme->color_imgui(Platform::Color::Text));
        return row;
    };

    // A field of the form, with the error line cleared as soon as the user types, so the message
    // never sits under a form that has been fixed already.
    auto add_field = [this, &labeled_row](const std::string& label) -> Yoga::InputTextField*
    {
        Item* row                   = labeled_row(label);
        Yoga::InputTextField* field = row->emplace_back<InputTextField>();
        field->set_flex_grow(1.f);
        field->callbacks().text_edited = [this]() { set_error(std::string{}); };
        return field;
    };

    // The four settings every datasheet gives, and the two that name the resin.
    m_name_input = add_field(Biz::_u8L("Resin name"));
    m_name_input->set_tooltip(
        Biz::_u8L("The name of the new resin profile. The review step lets you change it.")
    );
    m_vendor_input          = add_field(Biz::_u8L("Vendor"));
    m_layer_height_input    = add_field(Biz::_u8L("Layer height (mm)"));
    m_normal_exposure_input = add_field(Biz::_u8L("Normal exposure (s)"));
    m_bottom_exposure_input = add_field(Biz::_u8L("Bottom exposure (s)"));
    m_bottom_layers_input   = add_field(Biz::_u8L("Number of bottom layers"));

    content()->emplace_back<Separator>(Orientation::Horizontal);

    // The rest only exists for the resins whose datasheet states it. Said once here rather than in
    // every label, so an empty field reads as "not stated" and not as "forgotten".
    Text* optional_note = content()->emplace_back<Text>(Biz::_u8L(
        "Optional: leave a field empty if the datasheet does not state it. A light-off delay and a "
        "price are only asked for because the resin settings have a key for them."
    ));
    optional_note->set_width_percent(full_width_percent);
    optional_note->set_text_color(
        m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled)
    );
    optional_note->set_font_size(note_font_scale);
    optional_note->set_wrap_mode(Text::WrapMode::Wrap);

    m_light_off_delay_input = add_field(Biz::_u8L("Light-off delay (s)"));
    m_price_input           = add_field(Biz::_u8L("Price of a bottle"));
    m_bottle_volume_input   = add_field(Biz::_u8L("Bottle volume (ml)"));

    content()->emplace_back<Separator>(Orientation::Horizontal);

    Item* buttons_row = content()->emplace_back<Item>();
    buttons_row->set_gap(row_gap);
    buttons_row->set_flex_shrink(0.f);
    buttons_row->set_width_percent(full_width_percent);
    buttons_row->set_justify_content(YGJustifyFlexEnd);

    // Carries the values on to the review table. Not "Save": nothing is saved here, which is what
    // the review step is for.
    m_next_button = buttons_row->emplace_back<LayoutButton>(Biz::_u8L("Next"));
    m_next_button->set_background_color(Platform::Color::AccentPrimary);
    m_next_button->set_content_padding(Paddings(15.f, 5.f));
    m_next_button->callbacks().action = [this]() { next(); };

    LayoutButton* cancel_button = buttons_row->emplace_back<LayoutButton>(Biz::_u8L("Cancel"));
    cancel_button->set_background_color(Platform::Color::ButtonTransparent);
    cancel_button->set_label_color(m_theme->color_imgui(Platform::Color::TextLink));
    cancel_button->set_content_padding(Paddings(15.f, 5.f));
    cancel_button->callbacks().action = [this]() { close_action(); };
}

void ResinDatasheetDialog::set_next_callback(NextCallback callback)
{
    m_next_callback = std::move(callback);
}

void ResinDatasheetDialog::open()
{
    // The values stay in the fields, so coming back to a form to fix one number does not mean typing
    // the other five again.
    set_error(std::string{});
    m_navigator.set_opened_dialog(this);
}

void ResinDatasheetDialog::close_action()
{
    m_navigator.set_opened_dialog(nullptr);
}

Biz::ResinProfile::ResinDatasheet ResinDatasheetDialog::typed_datasheet() const
{
    Biz::ResinProfile::ResinDatasheet datasheet;
    datasheet.resin_name         = m_name_input->text();
    datasheet.vendor             = m_vendor_input->text();
    datasheet.layer_height_mm    = m_layer_height_input->text();
    datasheet.normal_exposure_s  = m_normal_exposure_input->text();
    datasheet.bottom_exposure_s  = m_bottom_exposure_input->text();
    datasheet.bottom_layer_count = m_bottom_layers_input->text();
    datasheet.light_off_delay_s  = m_light_off_delay_input->text();
    datasheet.price_per_bottle   = m_price_input->text();
    datasheet.bottle_volume_ml   = m_bottle_volume_input->text();
    return datasheet;
}

void ResinDatasheetDialog::next()
{
    // The form decides nothing, so what it accepts is one pure rule over what is typed, and the
    // message the user gets is the one that rule names.
    const Biz::ResinProfile::ResinDatasheet typed = typed_datasheet();
    const std::string error                       = Biz::ResinProfile::validate_datasheet(typed);
    set_error(error);
    if (!error.empty())
        return;

    // The form has done its part, so it hands the opened dialog over before it closes itself: the
    // review dialog is the one to be opened from here on, and closing afterwards would close that
    // one instead of this form.
    close_action();
    if (m_next_callback) {
        m_next_callback(Biz::ResinProfile::datasheet_to_profile(typed));
    }
}

void ResinDatasheetDialog::set_error(const std::string& error)
{
    m_error_line->set_text(error);
    m_error_line->set_visible(!error.empty());
}

} // namespace Slic3r::App
