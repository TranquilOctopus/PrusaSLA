#include "Slic3r/App/SlaPrintSettingsDialog.hpp"

#include "Slic3r/App/Config/SlaSettingsRows.hpp"
#include "Slic3r/App/MaterialSettingsDialog.hpp"
#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/PrintSettingsDialog.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/Yoga/StackLayout.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/OverrideItem.hpp"
#include "Slic3r/Biz/OverridableCBIObservableList.hpp"
#include "Slic3r/Biz/OverridableConfigBoxObservableList.hpp"
#include "Slic3r/Biz/Preset/PresetSelectionCheck.hpp"
#include "Slic3r/Biz/PrintToolConfigObservableList.hpp"
#include "Slic3r/Biz/PrintToolItem.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

namespace {

using Key = SlaSettingsRows::Key;

/// The resin values that matter most, in the order they are shown in the "Resin" tab. All of
/// them live in the resin (material) box, the print box only holds the fall-back values the
/// resin does not define. The layer-separation knobs are in the list because the .pwmx/.pm5 and
/// .goo exporters write the lift distance, the lift and retract speeds, the waits and the light
/// PWM into their headers.
std::vector<Key> resin_keys()
{
    return {
        {"resin_layer_height"},
        {"exposure_time"},
        {"initial_exposure_time"},
        {"bottom_layer_count"},
        {"resin_faded_layers"},
        // layer separation: lift, retract, the waits around them and the light
        {"lift_height"},
        {"lift_height_2"},
        {"lift_speed"},
        {"lift_speed_2"},
        {"retract_speed"},
        {"retract_speed_2"},
        {"wait_before_lift"},
        {"wait_after_lift"},
        {"wait_after_retract"},
        {"light_pwm"},
        {"delay_before_exposure"},
        {"delay_after_exposure"},
        {"bottom_lift_height"},
        {"bottom_lift_height_2"},
        {"bottom_lift_speed"},
        {"bottom_lift_speed_2"},
        {"bottom_retract_speed"},
        {"bottom_retract_speed_2"},
        {"bottom_wait_before_lift"},
        {"bottom_wait_after_lift"},
        {"bottom_wait_after_retract"},
        {"bottom_light_pwm"},
    };
}

/// The raft and support values, in the order they are shown in the "Supports & raft" tab. The
/// support presets are labelled only "Mini", "Light", "Medium" and "Heavy", so their row is
/// prefixed with the dimension. The last four rows place the support points and say what the
/// automatic placement puts there (M2.37).
std::vector<Key> supports_keys()
{
    return {
        {"raft_type"},
        {"support_object_elevation"},
        {"support_preset_mini_head_diameter", "", true},
        {"support_preset_mini_pillar_diameter", "", true},
        {"support_preset_mini_base_diameter", "", true},
        {"support_preset_mini_base_height", "", true},
        {"support_preset_light_head_diameter", "", true},
        {"support_preset_light_pillar_diameter", "", true},
        {"support_preset_light_base_diameter", "", true},
        {"support_preset_light_base_height", "", true},
        {"support_preset_medium_head_diameter", "", true},
        {"support_preset_medium_pillar_diameter", "", true},
        {"support_preset_medium_base_diameter", "", true},
        {"support_preset_medium_base_height", "", true},
        {"support_preset_heavy_head_diameter", "", true},
        {"support_preset_heavy_pillar_diameter", "", true},
        {"support_preset_heavy_base_diameter", "", true},
        {"support_preset_heavy_base_height", "", true},
        {"support_points_minimal_distance"},
        {"support_points_overhang_angle"},
        {"support_auto_heavy_base"},
        {"support_auto_detail_preset"},
    };
}

} // namespace

SlaPrintSettingsDialog::SlaPrintSettingsDialog(
    Biz::ProjectInteractor& project_interactor,
    Navigator& navigator,
    MaterialSettingsDialog* material_settings_dialog,
    PrintSettingsDialog* print_settings_dialog
) :
    Dialog({Biz::_u8L("Resin"), Biz::_u8L("Supports & raft")}, "SlaPrintSettingsDialog"),
    m_preset_changed_listener_scope(project_interactor.preset_interactor(), *this),
    m_project_interactor(project_interactor),
    m_navigator(navigator),
    m_material_settings_dialog(material_settings_dialog),
    m_print_settings_dialog(print_settings_dialog)
{
    content_item()->set_width(430);
    content_item()->set_height(600);

    content()->set_orientation(Orientation::Vertical);
    content()->set_gap(5);
    content()->set_flex_grow(1);

    m_tabs = content()->emplace_back<StackLayout>();
    m_tabs->set_orientation(Orientation::Vertical);
    m_tabs->set_flex_grow(1);

    create_resin_tab();
    create_supports_tab();

    collect_config_items();
    m_resin_rows->set_items(&m_resin_config_items);
    m_supports_rows->set_items(&m_supports_config_items);

    m_tabs->set_current_index(0);

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

void SlaPrintSettingsDialog::on_tab_selected(int current_index)
{
    if (m_tabs) {
        m_tabs->set_current_index(static_cast<size_t>(current_index));
    }
}

void SlaPrintSettingsDialog::close_action()
{
    m_navigator.set_opened_dialog(nullptr);
}

void SlaPrintSettingsDialog::create_resin_tab()
{
    Item* tab = m_tabs->emplace_back<Item>();
    tab->set_object_name("SlaResinTab");
    tab->set_orientation(Orientation::Vertical);
    tab->set_gap(5.f);
    tab->set_flex_grow(1);

    Item* row = tab->emplace_back<Item>();
    row->set_gap(5.f);
    row->set_flex_shrink(0);

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

    ScrollArea* scroll = tab->emplace_back<ScrollArea>();
    scroll->set_object_name("SlaResinScrollArea");
    scroll->set_orientation(Orientation::Vertical);
    scroll->set_flex_grow(1);

    m_resin_rows = scroll->emplace_back<SlaSettingsRows>(
        resin_keys(),
        m_project_interactor.preset_interactor()
    );

    LayoutButton* more_button = tab->emplace_back<LayoutButton>(
        Biz::_u8L("More resin settings...")
    );
    more_button->set_background_color(Platform::Color::ButtonTransparent);
    more_button->set_label_color(m_theme->color_imgui(Platform::Color::TextLink));
    more_button->set_self_align(YGAlignFlexStart);
    more_button->set_flex_shrink(0);
    more_button->callbacks().action = [this]()
    {
        if (m_material_settings_dialog) {
            m_navigator.set_opened_dialog(m_material_settings_dialog);
        }
    };
}

void SlaPrintSettingsDialog::create_supports_tab()
{
    Item* tab = m_tabs->emplace_back<Item>();
    tab->set_object_name("SlaSupportsTab");
    tab->set_orientation(Orientation::Vertical);
    tab->set_gap(5.f);
    tab->set_flex_grow(1);

    Item* row = tab->emplace_back<Item>();
    row->set_gap(5.f);
    row->set_flex_shrink(0);

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

    ScrollArea* scroll = tab->emplace_back<ScrollArea>();
    scroll->set_object_name("SlaSupportsScrollArea");
    scroll->set_orientation(Orientation::Vertical);
    scroll->set_flex_grow(1);

    m_supports_rows = scroll->emplace_back<SlaSettingsRows>(
        supports_keys(),
        m_project_interactor.preset_interactor()
    );

    LayoutButton* more_button = tab->emplace_back<LayoutButton>(Biz::_u8L("More..."));
    more_button->set_background_color(Platform::Color::ButtonTransparent);
    more_button->set_label_color(m_theme->color_imgui(Platform::Color::TextLink));
    more_button->set_self_align(YGAlignFlexStart);
    more_button->set_flex_shrink(0);
    more_button->callbacks().action = [this]()
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
    refresh_rows();
}

void SlaPrintSettingsDialog::collect_config_items()
{
    m_resin_config_items.clear();
    m_supports_config_items.clear();
    Biz::Preset::PresetInteractor& preset_interactor = m_project_interactor.preset_interactor();

    const Biz::OverridableCBIObservableList& material_cbis = preset_interactor.material_cbi_list();
    for (size_t slot = 0; slot < material_cbis.size(); ++slot) {
        const auto material_list = material_cbis.at(slot).config_box_overridable_list().lock();
        if (!material_list) {
            continue;
        }
        for (size_t index = 0; index < material_list->size(); ++index) {
            const Biz::OverrideItem& item = material_list->at(index);
            if (item.config_item != nullptr) {
                m_resin_config_items.push_back(
                    Biz::ConfigItemContext{item.name, item.config_item, item.original_config_item}
                );
            }
        }
    }

    const auto print_list = preset_interactor.print_tool_cbi().observable_list().lock();
    if (print_list) {
        for (size_t index = 0; index < print_list->size(); ++index) {
            const Biz::PrintToolItem& item = print_list->at(index);
            if (item.print_item != nullptr) {
                m_supports_config_items.push_back(
                    Biz::ConfigItemContext{item.name, item.print_item, item.original_print_item}
                );
            }
        }
    }
}

void SlaPrintSettingsDialog::refresh_rows()
{
    if (m_resin_rows == nullptr || m_supports_rows == nullptr) {
        // The preset listener is already registered while the tabs are being built
        return;
    }
    // The config boxes of a preset are rebuilt when another preset is selected, so the items
    // have to be gathered again instead of trusting the previous ones.
    collect_config_items();
    m_resin_rows->refresh();
    m_supports_rows->refresh();
}

bool SlaPrintSettingsDialog::is_selected(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id
) const
{
    return m_project_interactor.selected_project_id() == project_id
        && m_project_interactor.selected_config_container_id() == config_container_id;
}

void SlaPrintSettingsDialog::on_preset_selection_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    Biz::Preset::PresetItemType /*type*/
)
{
    if (is_selected(project_id, config_container_id)) {
        refresh_rows();
    }
}

void SlaPrintSettingsDialog::on_preset_value_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    const Domain::ConfigItem& /*item*/
)
{
    if (is_selected(project_id, config_container_id)) {
        refresh_rows();
    }
}

void SlaPrintSettingsDialog::on_config_container_selection_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id
)
{
    if (is_selected(project_id, config_container_id)) {
        refresh_rows();
    }
}

void SlaPrintSettingsDialog::on_preset_bundles_loaded()
{
    refresh_rows();
}

} // namespace Slic3r::App
