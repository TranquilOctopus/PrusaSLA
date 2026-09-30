#pragma once

#include "Slic3r/App/Yoga/Dialog.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/MaterialSelectionRow.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"
#include "Slic3r/App/Yoga/ButtonGroup.hpp"
#include "Slic3r/App/IAppConfigChangedListener.hpp"

#include "Slic3r/Biz/ObservableListSortFilter.hpp"
#include "Slic3r/Biz/ObservableListSearcher.hpp"
#include "Slic3r/Biz/ProjectScoped.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"

#include <boost/filesystem/path.hpp>

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App::Yoga {
class InputText;
class LayoutButton;
class ScrollArea;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class Navigator;
class MaterialSettingsDialog;
class ResinDatasheetDialog;
class ResinImportDialog;

class MaterialSelectionDialog :
    public Yoga::Dialog,
    public Biz::IListSelectionChangedListener,
    public Biz::IListObserver<Biz::Preset::PresetItemObservableList>,
    public IAppConfigChangedListener
{
public:
    struct Callbacks
    {
        std::function<void(size_t tab_index)> advanced_settings_tab_opened;
    };

    MaterialSelectionDialog(Biz::ProjectInteractor& project_interactor, Navigator& navigator);
    ~MaterialSelectionDialog();

    Callbacks& material_selection_callbacks();

    void set_material_index(size_t material_index);

    void on_list_selection_changed(Domain::SelectionId new_selection) override;

    void on_will_be_reset(std::optional<size_t> new_size = std::nullopt) override;
    void on_reset() override;

    MaterialSettingsDialog& material_settings_dialog();

    /**
     * @brief Open the resin import review dialog for @p path.
     *
     * The dialog itself is M3.10a; the entry points that call this (this dialog's "Import resin
     * profile" button, and a profile dropped onto the window) are M3.10b.
     */
    void open_resin_import(const boost::filesystem::path& path);

    /**
     * @brief Open the "New resin from datasheet" form, which reviews what it built in the same
     * dialog a profile read from a file is reviewed in.
     */
    void open_resin_datasheet();

    /**
     * @brief Ask the platform where to write the selected resin and write it there as a Chitubox
     * .cfg, through ResinProfileExportInteractor (M3.15c).
     *
     * The file dialog has to be the opened one while it is up, so this dialog is the opened one for
     * the duration of the pick, the way pick_resin_profile() does it for the import.
     */
    void export_resin_profile();

    /**
     * @brief Write the selected resin to @p path as a Chitubox .cfg, and say what became of it: the
     * keys that were written, the settings the format has no key for, or why there is no file.
     */
    void export_resin_profile_to(const boost::filesystem::path& path);

    ResinImportDialog& resin_import_dialog();

    /// @brief The "New resin from datasheet" form, which the render modules register in the dialog
    /// navigation as a child of this dialog.
    ResinDatasheetDialog& resin_datasheet_dialog();

    void on_app_config_changed(const std::string &key) override;

protected:
    void close_action() override;

    void update_preset_list();

private:
    struct ProjectContext;
    ProjectContext& context();
    const ProjectContext& context() const;
    void update_current_context();
    void update_type_filter_visibility();
    void on_about_to_show() override;

    /// Ask the platform for a resin profile file and hand it to the review dialog.
    void pick_resin_profile();

    /// The resin preset row the list has selected, nullptr when there is none. The export button
    /// writes this one; with no row the interactor writes the resin that is in the slot instead.
    const Biz::Preset::PresetItem* selected_resin() const;

private:
    using SelectionRowListViewFactory = Yoga::ViewFactory<
        MaterialSelectionRow,
        Biz::Preset::PresetItem,
        MaterialSelectionRow::FnClicked,
        MaterialSelectionRow::FnIndexClicked,
        MaterialSelectionRow::FnChecked,
        MaterialSelectionRow::FnClicked,
        size_t&,
        Biz::Preset::PresetInteractor&>;
    using SelectionRowListView = Yoga::ListView<
        MaterialSelectionRow,
        Biz::Preset::PresetItem,
        SelectionRowListViewFactory,
        Yoga::ScrollArea>;

    Callbacks m_callbacks;

    Biz::ProjectInteractor& m_project_interactor;
    Navigator& m_navigator;
    Biz::Preset::PresetItemCompoundObservableList& m_material_presets;
    Biz::UnsharedPointer<Biz::ObservableListSortFilter<Biz::Preset::PresetItem>> m_material_filter;
    Biz::UnsharedPointer<Biz::ObservableListSearcher<Biz::Preset::PresetItem>> m_material_searcher;

    size_t m_material_index = Domain::INVALID_ID;
    Yoga::ButtonGroup m_material_type_button_group;
    std::map<std::string, Yoga::LayoutButton*> m_type_filter_buttons;
    std::map<Yoga::LayoutButton*, std::string> m_type_filter_values;
    std::vector<Yoga::LayoutButton*> m_fff_type_filter_buttons;
    std::vector<Yoga::LayoutButton*> m_sla_type_filter_buttons;
    Yoga::InputText* m_input_text_search                 = nullptr;
    Yoga::LayoutButton* m_only_favorites_button          = nullptr;
    Yoga::LayoutButton* m_import_resin_profile_button    = nullptr;
    Yoga::LayoutButton* m_export_resin_profile_button    = nullptr;
    Yoga::LayoutButton* m_new_datasheet_resin_button     = nullptr;
    SelectionRowListView* m_selection_row_list_view      = nullptr;
    Biz::Preset::PresetItemObservableList* m_preset_list = nullptr;
    MaterialSettingsDialog* m_material_settings_dialog   = nullptr;
    ResinImportDialog* m_resin_import_dialog             = nullptr;
    ResinDatasheetDialog* m_datasheet_dialog             = nullptr;

    struct ProjectContext
    {
        std::string type_filter = std::string();

        bool operator==(const ProjectContext& other) const
        {
            return type_filter == other.type_filter;
        }
    } m_current_context;

    std::unique_ptr<Biz::ProjectScoped<ProjectContext>> m_project_contexts;
};

} // namespace Slic3r::App
