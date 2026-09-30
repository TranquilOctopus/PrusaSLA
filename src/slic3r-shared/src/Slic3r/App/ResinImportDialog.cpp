#include "Slic3r/App/ResinImportDialog.hpp"

#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/Theme.hpp"
#include "Slic3r/App/Yoga/ComboBox.hpp"
#include "Slic3r/App/Yoga/InputTextField.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"
#include "Slic3r/App/Yoga/Text.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/Preset/PresetSelectionCheck.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

using namespace Slic3r::App::Yoga;
using Slic3r::App::Yoga::operator""_fpx;

namespace Slic3r::App {

namespace {

/// @brief Width of the label column, so the printer, the base and the name line up.
constexpr Unit label_width{110.f};
constexpr Unit min_table_height{120.f};
constexpr Unit max_table_height{240.f};
constexpr Unit row_gap{6.f};
constexpr float full_width_percent = 100.f;
/// @brief How many report rows the dialog builds. A file with more keys than this is not likely, and
/// a bound keeps the table from growing without end.
constexpr std::size_t max_reported_rows = 500;

/// @brief The tokens a badge is drawn in, the "UI color token" column of the mapping table in
/// doc/sla-fork/ROADMAP.md. No colour value is written here: those live in the palette table of
/// Theme.cpp and are read through the theme (PLAN 2.1).
struct BadgeTokens
{
    Platform::Color color;
    Platform::ColorGroup group;
};

BadgeTokens badge_tokens(MappingBadge badge)
{
    switch (badge) {
    case MappingBadge::Exact:
        return {Platform::Color::AccentPrimary, Platform::ColorGroup::Default};
    case MappingBadge::Converted:
        return {Platform::Color::AccentSecondary, Platform::ColorGroup::Default};
    case MappingBadge::Approximated:
        return {Platform::Color::Warning, Platform::ColorGroup::Default};
    case MappingBadge::NotApplicable:
    case MappingBadge::Unknown:
        // The two kinds that write nothing are said in the quiet text colour, the way the rest of
        // the dialog says anything that is only there for the record.
        return {Platform::Color::Text, Platform::ColorGroup::Disabled};
    }
    return {Platform::Color::Text, Platform::ColorGroup::Disabled};
}

/// @brief What a row writes on the target side: the key and its value, empty when it writes nothing.
std::string imported_as(const MappingRow& row)
{
    if (!row.writes_value()) {
        return {};
    }
    return row.value.empty() ? row.target_key : row.target_key + " = " + row.value;
}

} // namespace

ResinImportDialog::ResinImportDialog(
    Biz::ProjectInteractor& project_interactor,
    Navigator& navigator
) :
    Dialog({Biz::_u8L("Import resin profile")}, "ResinImportDialog"),
    m_project_interactor(project_interactor),
    m_navigator(navigator),
    m_importer(project_interactor)
{
    set_closable(true);
    content_item()->set_modal(true);
    content_item()->set_width(520);

    content()->set_orientation(Orientation::Vertical);
    content()->set_gap(row_gap);

    // A row of the form: a fixed width label, then the control that answers it.
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

    // Which file this is, and what it says about itself. Bold because the file is the first thing
    // the user drops and the first thing they check afterwards.
    m_source_line = content()->emplace_back<Text>(std::string{});
    m_source_line->set_width_percent(full_width_percent);
    m_source_line->set_font_type(Render::ImguiFontType::Bold);
    m_source_line->set_text_color(m_theme->color_imgui(Platform::Color::Text));
    m_source_line->set_wrap_mode(Text::WrapMode::Wrap);

    // Why the import cannot go on. Hidden while there is nothing to say, so no line is reserved for
    // it on every import.
    m_error_line = content()->emplace_back<Text>(std::string{});
    m_error_line->set_width_percent(full_width_percent);
    m_error_line->set_text_color(m_theme->color_imgui(Platform::Color::Error));
    m_error_line->set_wrap_mode(Text::WrapMode::Wrap);
    m_error_line->set_visible(false);

    content()->emplace_back<Separator>(Orientation::Horizontal);

    // The printer is the selected one: a resin profile is written into the config container of the
    // selected printer, and the importer refuses any other target rather than importing into a
    // printer the user is not looking at.
    Item* printer_row = labeled_row(Biz::_u8L("Target printer"));
    m_printer_label   = printer_row->emplace_back<Text>(printer_name());
    m_printer_label->set_flex_grow(1.f);
    m_printer_label->set_text_color(m_theme->color_imgui(Platform::Color::Text));

    // The system resin the new preset inherits from. Picking another one re-runs the report, because
    // the base says how the printer separates the layers, which decides the mapping table that is
    // used. The rest of the printer's settings are inherited with it.
    Item* base_row = labeled_row(Biz::_u8L("Base material"));
    m_base_combo   = base_row->emplace_back<ComboBox>("Base material");
    m_base_combo->set_flex_grow(1.f);
    m_base_combo->callbacks().selection_changed = [this](int)
    {
        refresh_report();
        update_name_error();
    };

    Item* name_row = labeled_row(Biz::_u8L("Preset name"));
    m_name_input   = name_row->emplace_back<InputTextField>();
    m_name_input->set_flex_grow(1.f);
    m_name_input->set_hint(Biz::_u8L("Name of the imported resin profile"));
    m_name_input->set_tooltip(Biz::_u8L("The new resin profile is saved under this name."));
    m_name_input->callbacks().text_edited = [this]() { update_name_error(); };

    m_name_error_line = content()->emplace_back<Text>(std::string{});
    m_name_error_line->set_width_percent(full_width_percent);
    m_name_error_line->set_text_color(m_theme->color_imgui(Platform::Color::Error));
    m_name_error_line->set_visible(false);

    content()->emplace_back<Separator>(Orientation::Horizontal);

    // The table: the header names the three parts of a row, the rows below it fill them in.
    Item* header_row = content()->emplace_back<Item>();
    header_row->set_gap(row_gap);
    header_row->set_flex_shrink(0.f);
    header_row->set_width_percent(full_width_percent);
    for (const std::string& header :
         {Biz::_u8L("From the file"), Biz::_u8L("Into the resin profile"), Biz::_u8L("Status")})
    {
        Text* header_item = header_row->emplace_back<Text>(header);
        header_item->set_flex_grow(1.f);
        header_item->set_font_type(Render::ImguiFontType::Bold);
        header_item->set_text_color(m_theme->color_imgui(Platform::Color::Text));
    }

    ScrollArea* scroll = content()->emplace_back<ScrollArea>();
    scroll->set_object_name("ResinImportScrollArea");
    scroll->set_orientation(Orientation::Vertical);
    scroll->set_flex_grow(1.f);
    scroll->set_min_height(min_table_height);
    scroll->set_max_height(max_table_height);
    scroll->set_padding(Paddings(5.f, 5.f));

    m_table = scroll->emplace_back<Item>();
    m_table->set_orientation(Orientation::Vertical);
    m_table->set_gap(3_fpx);

    m_summary_line = content()->emplace_back<Text>(std::string{});
    m_summary_line->set_width_percent(full_width_percent);
    m_summary_line->set_text_color(
        m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled)
    );
    m_summary_line->set_wrap_mode(Text::WrapMode::Wrap);
    m_summary_line->set_visible(false);

    content()->emplace_back<Separator>(Orientation::Horizontal);

    Item* buttons_row = content()->emplace_back<Item>();
    buttons_row->set_gap(row_gap);
    buttons_row->set_flex_shrink(0.f);
    buttons_row->set_width_percent(full_width_percent);
    buttons_row->set_justify_content(YGJustifyFlexEnd);

    // Saves the profile and leaves the resin the printer had selected as it is.
    m_save_button = buttons_row->emplace_back<LayoutButton>(Biz::_u8L("Save"));
    m_save_button->set_content_padding(Paddings(15.f, 5.f));
    m_save_button->callbacks().action = [this]() { save(/*select=*/false); };

    // Saves the profile and leaves it in the resin slot, which is what someone who just imported a
    // profile usually wants to do with it right away.
    m_save_and_select_button = buttons_row->emplace_back<LayoutButton>(Biz::_u8L("Save & select"));
    m_save_and_select_button->set_background_color(Platform::Color::AccentPrimary);
    m_save_and_select_button->set_content_padding(Paddings(15.f, 5.f));
    m_save_and_select_button->callbacks().action = [this]() { save(/*select=*/true); };

    LayoutButton* cancel_button = buttons_row->emplace_back<LayoutButton>(Biz::_u8L("Cancel"));
    cancel_button->set_background_color(Platform::Color::ButtonTransparent);
    cancel_button->set_label_color(m_theme->color_imgui(Platform::Color::TextLink));
    cancel_button->set_content_padding(Paddings(15.f, 5.f));
    cancel_button->callbacks().action = [this]() { close_action(); };

    clear_table();
    update_source_line();
}

void ResinImportDialog::open(const boost::filesystem::path& path)
{
    m_file = path;
    m_imported_preset_name.clear();
    m_result = Biz::ResinProfile::ResinImportResult{};
    m_name_input->set_text(std::string{});

    fill_base_combo();
    m_printer_label->set_text(printer_name());

    // What the file is, before anything is picked: the base the importer chooses for it and the name
    // it suggests. Both are worked out by the import itself, so the dialog shows what the import
    // will do rather than guessing at it.
    m_result = m_importer.import_file(m_file, import_target(), /*dry_run=*/true);
    if (m_result.ok) {
        const auto picked = std::ranges::find(m_base_ids, m_result.base_preset_id);
        if (picked != m_base_ids.end()) {
            m_base_combo->set_current_index(static_cast<int>(picked - m_base_ids.begin()));
        }
    }

    refresh_report();
    update_name_error();

    // Opened even when the import cannot go on, because then the dialog says why.
    m_navigator.set_opened_dialog(this);
}

void ResinImportDialog::close_action()
{
    m_navigator.set_opened_dialog(nullptr);
}

Biz::ResinProfile::ResinImportTarget ResinImportDialog::import_target() const
{
    // Whatever is selected is the target: the importer refuses anything else, so the dialog names
    // the selected printer and nothing else.
    return Biz::ResinProfile::ResinImportTarget{
        .project_id          = m_project_interactor.selected_project_id(),
        .config_container_id = m_project_interactor.selected_config_container_id(),
        .material_slot       = 0,
    };
}

std::string ResinImportDialog::printer_name() const
{
    return m_project_interactor.preset_interactor().selected_printer_preset().printer.name;
}

void ResinImportDialog::fill_base_combo()
{
    m_base_ids.clear();
    std::vector<std::string> names;
    for (const std::pair<std::string, std::string>& resin : Biz::ResinProfile::system_resin_presets(
             m_project_interactor.preset_interactor(),
             m_project_interactor.selected_project_id(),
             0
         ))
    {
        m_base_ids.push_back(resin.first);
        names.push_back(resin.second);
    }
    m_base_combo->set_items(names);
    m_base_combo->set_current_index(0);
}

std::string ResinImportDialog::base_preset_id() const
{
    const int index = m_base_combo->current_index();
    if (index < 0 || static_cast<std::size_t>(index) >= m_base_ids.size()) {
        return {};
    }
    return m_base_ids[static_cast<std::size_t>(index)];
}

std::string ResinImportDialog::wanted_name() const
{
    const std::string typed = m_name_input->text();
    return typed.empty() ? m_result.preset_name : typed;
}

void ResinImportDialog::refresh_report()
{
    // The name the user typed goes in, so the table and the name always describe the same import.
    // The importer makes that name unique and the input follows it, which is how a name that is
    // already taken is shown before anything is saved.
    m_result = m_importer.import_file(
        m_file,
        import_target(),
        /*dry_run=*/true,
        base_preset_id(),
        wanted_name()
    );
    if (m_result.ok && m_name_input->text() != m_result.preset_name) {
        m_name_input->set_text(m_result.preset_name);
    }

    update_source_line();
    build_table();

    const bool can_import = m_result.ok;
    m_save_button->set_enabled(can_import);
    m_save_and_select_button->set_enabled(can_import);
    m_base_combo->set_enabled(can_import);
    m_name_input->set_enabled(can_import);
    set_error(can_import ? std::string{} : m_result.error);
}

void ResinImportDialog::build_table()
{
    clear_table();

    m_rows = build_mapping_rows(m_result);
    if (m_rows.size() > max_reported_rows) {
        m_rows.resize(max_reported_rows);
    }

    for (const MappingRow& row : m_rows) {
        // One key per block: what the file said, what becomes of it and the status of that, then
        // the note of the row under it. The note is what makes an approximation auditable, so it is
        // shown rather than kept in a tooltip.
        Item* block = m_table->emplace_back<Item>();
        block->set_orientation(Orientation::Vertical);
        block->set_gap(0.f);

        Item* line = block->emplace_back<Item>();
        line->set_gap(row_gap);
        line->set_align_items(YGAlignCenter);

        Text* source = line->emplace_back<Text>(row.source_key);
        source->set_flex_grow(1.f);
        source->set_text_color(m_theme->color_imgui(Platform::Color::Text));

        const std::string target_text = imported_as(row);
        Text* target                  = line->emplace_back<Text>(target_text);
        target->set_flex_grow(1.f);
        // A row that writes nothing is dimmed, so the values that do land read first.
        target->set_text_color(
            row.writes_value() ?
                m_theme->color_imgui(Platform::Color::Text) :
                m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled)
        );

        const BadgeTokens tokens = badge_tokens(row.badge);
        Rectangle* badge         = line->emplace_back<Rectangle>();
        badge->set_flex_shrink(0.f);
        badge->set_align_items(YGAlignCenter);
        badge->set_padding(Paddings(5.f, 2.f));
        badge->set_fill(m_theme->color_imgui(Platform::Color::WindowBgAlternate));
        Text* badge_text = badge->emplace_back<Text>(badge_label(row.badge));
        badge_text->set_text_color(m_theme->color_imgui(tokens.color, tokens.group));

        if (!row.note.empty()) {
            Text* note = block->emplace_back<Text>(row.note);
            note->set_text_color(
                m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled)
            );
            note->set_font_size(0.85_rem);
            note->set_wrap_mode(Text::WrapMode::Wrap);
        }
    }

    const std::string summary = summary_line(m_rows);
    m_summary_line->set_text(summary);
    m_summary_line->set_visible(!summary.empty());
}

void ResinImportDialog::clear_table()
{
    // Copied first: the rows are removed after this render pass, and the report is rebuilt whenever
    // the base or the name changes.
    const std::vector<Item*> rows = m_table->items();
    for (Item* row : rows) {
        m_table->remove_later(row);
    }
}

void ResinImportDialog::update_source_line()
{
    m_source_line->set_text(source_summary(m_result));
}

void ResinImportDialog::set_error(const std::string& error)
{
    m_error_line->set_text(error);
    m_error_line->set_visible(!error.empty());
}

void ResinImportDialog::update_name_error()
{
    const std::string error = validate_preset_name(m_name_input->text());
    m_name_error_line->set_text(error);
    m_name_error_line->set_visible(!error.empty());
}

void ResinImportDialog::save(bool select)
{
    if (!validate_preset_name(m_name_input->text()).empty()) {
        update_name_error();
        return;
    }

    // What the resin slot shows before the import, so "Save" can put it back: the import selects the
    // base and then saves over it, which is what leaves the new preset selected.
    Biz::Preset::PresetInteractor& presets = m_project_interactor.preset_interactor();
    const std::string previous_material    = presets.selected_printer_preset().materials[0].id;

    m_result = m_importer.import_file(
        m_file,
        import_target(),
        /*dry_run=*/false,
        base_preset_id(),
        m_name_input->text()
    );
    if (!m_result.ok) {
        set_error(m_result.error);
        return;
    }

    const bool selection_moved =
        previous_material != presets.selected_printer_preset().materials[0].id;
    if (!select
        && selection_moved
        && Biz::Preset::PresetSelectionCheck::
            can_select_material_preset(presets, 0, previous_material))
    {
        // Not a user driven change: the resin slot ends up showing exactly what it showed before.
        presets.select_material_preset(0, previous_material, /*user=*/false);
    }

    m_imported_preset_name = m_result.preset_name;
    close_action();
}

} // namespace Slic3r::App
