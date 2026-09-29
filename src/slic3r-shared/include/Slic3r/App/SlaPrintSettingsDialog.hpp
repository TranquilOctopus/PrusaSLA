#pragma once

#include "Slic3r/App/Yoga/ComboBoxListViewSelection.hpp"
#include "Slic3r/App/Yoga/Dialog.hpp"

#include "Slic3r/Biz/IListObserver.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {

class MaterialSettingsDialog;
class Navigator;
class PrintSettingsDialog;

/**
 * @brief SLA counterpart of the print preset dropdowns in the sidebar.
 *
 * Both SLA presets live here: the resin profile and the supports & raft profile. The dropdowns
 * pick a preset, the buttons open the MaterialSettingsDialog / PrintSettingsDialog holding all
 * the values of the selected preset.
 */
class SlaPrintSettingsDialog :
    public Yoga::Dialog,
    public Biz::IListObserver<Biz::Preset::PresetItemObservableList>
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

protected:
    void close_action() override;

private:
    void create_resin_section();
    void create_print_preset_section();
    void update_resin_combo_source();

private:
    Biz::ProjectInteractor& m_project_interactor;
    Navigator& m_navigator;
    MaterialSettingsDialog* m_material_settings_dialog{nullptr};
    PrintSettingsDialog* m_print_settings_dialog{nullptr};

    Biz::Preset::PresetItemObservableList* m_resin_presets{nullptr};
    Yoga::ComboBoxListViewSelection<Biz::Preset::PresetItem>* m_resin_combo{nullptr};
    Yoga::ComboBoxListViewSelection<Biz::Preset::PresetItem>* m_print_preset_combo{nullptr};

    int m_last_selected_resin{-1};
    int m_last_selected_print_preset{-1};
};

} // namespace Slic3r::App
