#include "Slic3r/App/SlaPrintSettingsDialog.hpp"

#include "Slic3r/App/MaterialSettingsDialog.hpp"
#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/PrintSettingsDialog.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"
#include "Slic3r/App/Yoga/Text.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/Preset/PresetSelectionCheck.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

SlaPrintSettingsDialog::SlaPrintSettingsDialog(
    Biz::ProjectInteractor& project_interactor,
    Navigator& navigator,
    MaterialSettingsDialog* material_settings_dialog,
    PrintSettingsDialog* print_settings_dialog
) :
    Dialog({"Print settings"}, "SlaPrintSettingsDialog"),
    m_project_interactor(project_interactor),
    m_navigator(navigator),
    m_material_settings_dialog(material_settings_dialog),
    m_print_settings_dialog(print_settings_dialog)
{
    content_item()->set_width(350);

    content()->set_orientation(Orientation::Vertical);
    content()->set_gap(5);

    create_resin_section();
    add_separator();
    create_print_preset_section();

    m_project_interactor.preset_interactor()
        .material_presets()
        .add_listener<Biz::IListObserver<Biz::Preset::PresetItemObservableList>>(this);
}

SlaPrintSettingsDialog::~SlaPrintSettingsDialog()
{
    m_project_interactor.preset_interactor()
        .material_presets()
        .remove_listener<Biz::IListObserver<Biz::Preset::PresetItemObservableList>>(this);
}

void SlaPrintSettingsDialog::toggle()
{
    m_navigator.set_opened_dialog(opened() ? nullptr : this);
}

void SlaPrintSettingsDialog::close_action()
{
    m_navigator.set_opened_dialog(nullptr);
}

void SlaPrintSettingsDialog::create_resin_section()
{
    Item* section = content()->emplace_back<Item>();
    section->set_orientation(Orientation::Vertical);
    section->set_gap(5.f);
    section->set_flex_shrink(0);

    Text* label = section->emplace_back<Text>(Biz::_u8L("Resin"));
    label->set_font_type(Render::ImguiFontType::Bold);

    Item* row = section->emplace_back<Item>();
    row->set_gap(5.f);

    m_resin_combo = row->emplace_back<ComboBoxListViewSelection<Biz::Preset::PresetItem>>();
    m_resin_combo->set_get_name_fn(
        [](const Biz::Preset::PresetItem* item) -> std::string
        { return item->ui_preset_name(); }
    );
    m_resin_combo->set_flex_grow(1);
    m_resin_combo->callbacks().selection_changed = [this](int resin_index)
    {
        if (resin_index < 0 || m_resin_presets == nullptr) {
            return;
        }
        auto& preset_interactor = m_project_interactor.preset_interactor();
        const std::string& preset_id = m_resin_presets->items().at(resin_index).id;
        if (Biz::Preset::PresetSelectionCheck::can_select_material_preset(
                preset_interactor,
                0,
                preset_id
            ))
        {
            preset_interactor.select_material_preset(0, preset_id, true);
            m_last_selected_resin = resin_index;
        } else {
            m_resin_combo->set_current_index(m_last_selected_resin);
        }
    };
    update_resin_combo_source();

    LayoutButton* edit_button = row->emplace_back<LayoutButton>(
        Biz::_u8L("Edit resin settings..."),
        Render::Icon::Cog
    );
    edit_button->set_self_align(YGAlignCenter);
    edit_button->callbacks().action = [this]()
    {
        if (m_material_settings_dialog) {
            m_navigator.set_opened_dialog(m_material_settings_dialog);
        }
    };
}

void SlaPrintSettingsDialog::create_print_preset_section()
{
    Item* section = content()->emplace_back<Item>();
    section->set_orientation(Orientation::Vertical);
    section->set_gap(5.f);
    section->set_flex_shrink(0);

    Text* label = section->emplace_back<Text>(Biz::_u8L("Supports & raft"));
    label->set_font_type(Render::ImguiFontType::Bold);

    Item* row = section->emplace_back<Item>();
    row->set_gap(5.f);

    auto& preset_interactor = m_project_interactor.preset_interactor();
    m_print_preset_combo = row->emplace_back<ComboBoxListViewSelection<Biz::Preset::PresetItem>>();
    m_print_preset_combo->set_get_name_fn(
        [](const Biz::Preset::PresetItem* item) -> std::string
        { return item->ui_preset_name(); }
    );
    m_print_preset_combo->set_source_list(&preset_interactor.print_presets());
    m_print_preset_combo->set_flex_grow(1);
    m_print_preset_combo->callbacks().selection_changed = [this](int print_index)
    {
        if (print_index >= 0) {
            auto& interactor = m_project_interactor.preset_interactor();
            const std::string& preset_id = interactor.print_presets().items().at(print_index).id;
            if (Biz::Preset::PresetSelectionCheck::can_select_print_preset(interactor, preset_id)) {
                interactor.select_print_preset(preset_id, true);
                m_last_selected_print_preset = print_index;
            } else {
                m_print_preset_combo->set_current_index(m_last_selected_print_preset);
            }
        }
    };

    LayoutButton* edit_button = row->emplace_back<LayoutButton>(
        Biz::_u8L("Edit supports & raft..."),
        Render::Icon::Cog
    );
    edit_button->set_self_align(YGAlignCenter);
    edit_button->callbacks().action = [this]()
    {
        if (m_print_settings_dialog) {
            m_navigator.set_opened_dialog(m_print_settings_dialog);
        }
    };
}

void SlaPrintSettingsDialog::update_resin_combo_source()
{
    auto& material_presets = m_project_interactor.preset_interactor().material_presets();
    if (material_presets.size() == 0) {
        m_resin_presets = nullptr;
        m_resin_combo->set_source_list(nullptr);
        return;
    }
    m_resin_presets = &material_presets.at(0);
    m_resin_combo->set_source_list(m_resin_presets);
}

void SlaPrintSettingsDialog::on_will_be_reset(std::optional<size_t> /*new_size*/)
{
    if (m_resin_combo) {
        m_resin_presets = nullptr;
        m_resin_combo->set_source_list(nullptr);
    }
}

void SlaPrintSettingsDialog::on_reset()
{
    update_resin_combo_source();
}

} // namespace Slic3r::App
