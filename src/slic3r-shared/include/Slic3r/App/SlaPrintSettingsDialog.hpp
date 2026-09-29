#pragma once

#include "Slic3r/App/Yoga/ComboBoxListViewSelection.hpp"
#include "Slic3r/App/Yoga/Dialog.hpp"

#include "Slic3r/Biz/ConfigItemContext.hpp"
#include "Slic3r/Biz/IListObserver.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"
#include "Slic3r/Biz/Preset/IPresetChangedListener.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {

class MaterialSettingsDialog;
class Navigator;
class PrintSettingsDialog;
class SlaSettingsRows;

namespace Yoga {
class StackLayout;
} // namespace Yoga

/**
 * @brief SLA counterpart of the print preset dropdowns in the sidebar.
 *
 * Both SLA presets live here: the resin profile and the supports & raft profile. Each one has its
 * own tab, the dropdown at the top picks the preset and the most important values of the selected
 * preset are edited in place below it. Everything else stays in the MaterialSettingsDialog /
 * PrintSettingsDialog, which the "More ..." links of the tabs open.
 */
class SlaPrintSettingsDialog :
    public Yoga::Dialog,
    public Biz::IListObserver<Biz::Preset::PresetItemObservableList>,
    public Biz::Preset::IPresetChangedListener
{
public:
    SlaPrintSettingsDialog(
        Biz::ProjectInteractor& project_interactor,
        Navigator& navigator,
        MaterialSettingsDialog* material_settings_dialog,
        PrintSettingsDialog* print_settings_dialog
    );
    ~SlaPrintSettingsDialog();

    /// Opens the dialog when it is closed, closes it otherwise.
    void toggle();

    void on_will_be_reset(std::optional<size_t> new_size = std::nullopt) override;
    void on_reset() override;

    void on_preset_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        Biz::Preset::PresetItemType type
    ) override;

    void on_preset_value_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        const Domain::ConfigItem& item
    ) override;

    void on_config_container_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id
    ) override;

    void on_preset_bundles_loaded() override;

protected:
    void on_tab_selected(int current_index) override;
    void close_action() override;

private:
    void create_resin_tab();
    void create_supports_tab();
    void update_resin_combo_source();

    /// Gathers the config items of the selected resin and supports presets.
    void collect_config_items();
    void refresh_rows();

    bool is_selected(Domain::SelectionId project_id, Domain::SelectionId config_container_id) const;

private:
    Biz::ListenerScope<
        Biz::Preset::IPresetChangedListener,
        Biz::Preset::PresetInteractor,
        SlaPrintSettingsDialog>
        m_preset_changed_listener_scope;

    Biz::ProjectInteractor& m_project_interactor;
    Navigator& m_navigator;
    MaterialSettingsDialog* m_material_settings_dialog{nullptr};
    PrintSettingsDialog* m_print_settings_dialog{nullptr};

    Yoga::StackLayout* m_tabs{nullptr};
    SlaSettingsRows* m_resin_rows{nullptr};
    SlaSettingsRows* m_supports_rows{nullptr};

    /// The config items of the selected presets, the source of the rows above.
    std::vector<Biz::ConfigItemContext> m_config_items;

    Biz::Preset::PresetItemObservableList* m_resin_presets{nullptr};
    Yoga::ComboBoxListViewSelection<Biz::Preset::PresetItem>* m_resin_combo{nullptr};
    Yoga::ComboBoxListViewSelection<Biz::Preset::PresetItem>* m_print_preset_combo{nullptr};

    int m_last_selected_resin{-1};
    int m_last_selected_print_preset{-1};
};

} // namespace Slic3r::App
