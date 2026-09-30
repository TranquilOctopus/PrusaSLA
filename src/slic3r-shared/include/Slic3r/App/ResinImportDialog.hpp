#pragma once

#include "Slic3r/App/ResinImportReport.hpp"
#include "Slic3r/App/Yoga/Dialog.hpp"

#include "Slic3r/Biz/ResinProfile/ResinProfileImportInteractor.hpp"

#include <boost/filesystem/path.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {

class Navigator;

namespace Yoga {
class ComboBox;
class InputTextField;
class Item;
class LayoutButton;
class Text;
} // namespace Yoga

/**
 * @brief The "Import resin profile" review dialog of a foreign resin profile (M3.10a).
 *
 * Opening it for a file runs the import as a dry run: the interactor reads the file, maps it for the
 * selected printer and reports what would happen, but writes nothing. The dialog shows the source
 * summary, the printer the profile is imported into, the system resin the new preset inherits from
 * and one row per key of the file with a status badge, so no value is lost silently. The rows, the
 * badges and the name check live in ResinImportReport, so they can be tested without a window.
 *
 * "Save" writes the user preset and leaves the material selection as it was; "Save & select" also
 * leaves the new preset selected in the target container. Both run the import for real, and both
 * keep the dialog open with the reason when the import fails.
 *
 * The entry points (the "Import resin profile" button and drag-and-drop of a profile onto the
 * window) are M3.10b; they call open() with the file the user picked.
 */
class ResinImportDialog : public Yoga::Dialog
{
public:
    ResinImportDialog(Biz::ProjectInteractor& project_interactor, Navigator& navigator);

    /**
     * @brief Fill the dialog for @p path and open it.
     *
     * The dialog opens even when the file cannot be read or the selected printer is not an SLA
     * printer, because then it says why instead of the user seeing nothing happen.
     */
    void open(const boost::filesystem::path& path);

    /**
     * @brief Fill the dialog for a profile that was built without a file and open it.
     *
     * This is the entry point of the "New resin from datasheet" form (M3.11): the form builds a
     * ForeignResinProfile out of what the user typed and hands it over here, so the review table,
     * the base picker and both buttons are the ones of a file import. The dry run and the save
     * behind them are import_profile() of the interactor, which is import_file() without the read.
     */
    void open(const Biz::ResinProfile::ForeignResinProfile& profile);

    /// @brief Name of the preset the last save created, empty before that.
    const std::string& imported_preset_name() const
    {
        return m_imported_preset_name;
    }

protected:
    void close_action() override;

private:
    /// The target the import goes into: the selected project and the selected printer, resin slot 0.
    Biz::ResinProfile::ResinImportTarget import_target() const;

    /// Name of the printer the profile is imported into, empty when there is none.
    std::string printer_name() const;

    /// @brief Re-run the dry run for the base the picker has selected and rebuild the table, the
    /// summary and the source line. The base decides the mapping table (a resin that turns the tilt
    /// off is a Z-lift printer), so picking one changes what the report says.
    void refresh_report();

    /// @brief Read the name the picker holds, empty when it holds no resin.
    std::string base_preset_id() const;

    /// @brief Empty the dialog and fill it for whatever this dialog was opened for: a file, or a
    /// profile that was built without one. Both go on to the same report, the same table and the
    /// same save.
    void fill_for_review();

    /// @brief The import of what this dialog holds, as a dry run or for real. A profile that was
    /// built without a file is already read and goes to import_profile(), a file goes through the
    /// registry in import_file(); everything after that is the same code.
    Biz::ResinProfile::ResinImportResult run_import(
        bool dry_run,
        const std::string& base_id,
        const std::string& preset_name
    );

    void fill_base_combo();
    void build_table();
    void clear_table();
    void update_source_line();

    /// @brief Show @p error, or hide the error line when it is empty. @p error is not translated:
    /// it is either a message of this dialog or the one the interactor wrote for the file.
    void set_error(const std::string& error);

    /// @brief Show what is wrong with the typed name, or hide the line when there is nothing wrong.
    void update_name_error();

    /// @brief The name the dialog would save under: what is typed in, else the suggested one.
    std::string wanted_name() const;

    /// @brief Run the import for real. @p select leaves the new preset selected in the container.
    void save(bool select);

private:
    Biz::ProjectInteractor& m_project_interactor;
    Navigator& m_navigator;
    Biz::ResinProfile::ResinProfileImportInteractor m_importer;

    boost::filesystem::path m_file;
    /// The profile this dialog reviews when it was opened for one that was built without a file,
    /// which is how the "New resin from datasheet" form of M3.11 hands its values over. Empty while
    /// the dialog reviews a file.
    std::optional<Biz::ResinProfile::ForeignResinProfile> m_profile;
    Biz::ResinProfile::ResinImportResult m_result;
    std::vector<MappingRow> m_rows;
    /// Ids of the resins in the base picker, in the same order as its items.
    std::vector<std::string> m_base_ids;
    std::string m_imported_preset_name;

    Yoga::Text* m_source_line{nullptr};
    Yoga::Text* m_error_line{nullptr};
    Yoga::Text* m_printer_label{nullptr};
    Yoga::ComboBox* m_base_combo{nullptr};
    Yoga::InputTextField* m_name_input{nullptr};
    Yoga::Text* m_name_error_line{nullptr};
    Yoga::Item* m_table{nullptr};
    Yoga::Text* m_summary_line{nullptr};
    Yoga::LayoutButton* m_save_button{nullptr};
    Yoga::LayoutButton* m_save_and_select_button{nullptr};
};

} // namespace Slic3r::App
