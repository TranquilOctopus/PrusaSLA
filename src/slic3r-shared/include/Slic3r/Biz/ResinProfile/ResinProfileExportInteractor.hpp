#pragma once

#include "Slic3r/Biz/ResinProfile/ChituboxCfgExport.hpp"
#include "Slic3r/Domain/SelectionId.hpp"

#include <boost/filesystem/path.hpp>

#include <cstddef>
#include <string>

namespace Slic3r::Biz {

namespace Preset {
class PresetInteractor;
} // namespace Preset

namespace ResinProfile {

/// @brief What became of one resin preset that was written out as a Chitubox .cfg.
struct ResinProfileExportResult
{
    /// @brief False when there is no file to write; @ref error then says why.
    bool ok{false};
    /// @brief Why nothing was exported, empty on success.
    std::string error;
    /// @brief The resin that was exported, empty on failure. It is also the name written into the
    /// file as currProfile.
    std::string preset_name;
    /// @brief The name a file dialog should offer: the resin name, made into a legal file name, with
    /// the extension of the format.
    std::string suggested_file_name;
    /// @brief The file and the report of every key of the reverse mapping table.
    ChituboxCfgExport exported;
};

/**
 * @brief Writes a resin preset of the selected printer out as a Chitubox .cfg (M3.15, M3.15c).
 *
 * This is the half of the export the command line does not do: it resolves the resin preset out of
 * the presets of the selected printer, so a caller with a selection (the material dialog's export
 * button) and a caller with a name (--export-resin-profile) ask the same questions of the same
 * place. Which of the two mapping tables the resin is written with is decided by the printer model
 * and the preset's own use_tilt, the same rule the import maps with, so a preset that goes out into
 * a file and comes back in is mapped the same way both times.
 *
 * Building the file and writing it are two steps on purpose: the report says what was written and
 * what the format has no key for, and a caller that wants to show that (the button does) can do it
 * before the file is on disk, and a caller that does not can write it in one go through
 * @ref export_preset_to_file.
 */
class ResinProfileExportInteractor
{
public:
    explicit ResinProfileExportInteractor(const Preset::PresetInteractor& preset_interactor)
        : m_preset_interactor(preset_interactor)
    {}

    /**
     * @brief Build the .cfg of one resin preset, without writing anything.
     * @param project_id The project whose container the presets are read from.
     * @param preset_name Name or id of the resin preset, looked up among the resins of the selected
     *                    printer. Empty exports the resin preset that is selected in @p material_slot.
     * @param material_slot Resin slot in the container. An SLA printer has exactly one.
     */
    ResinProfileExportResult export_preset(
        Domain::SelectionId project_id,
        const std::string& preset_name = {},
        size_t material_slot = 0
    ) const;

    /**
     * @brief Build the .cfg of one resin preset and write it to @p path.
     * @param error Why the file was not written, empty when it was. Set from the result and from the
     *              write itself, so a path that cannot be opened says so here.
     * @return The result of export_preset, whose text is the file that was written.
     */
    ResinProfileExportResult export_preset_to_file(
        const boost::filesystem::path& path,
        Domain::SelectionId project_id,
        const std::string& preset_name = {},
        size_t material_slot = 0,
        std::string* error = nullptr
    ) const;

    /// @brief The extension of the file this writes, without the dot.
    static constexpr const char* FILE_EXTENSION = "cfg";

private:
    const Preset::PresetInteractor& m_preset_interactor;
};

/**
 * @brief A preset name as a file name: the characters a file name may not have become underscores,
 * so the name a file dialog offers is a name that can be written. The extension is not added here,
 * the caller decides which file the user is asked for.
 */
std::string suggested_cfg_file_name(const std::string& preset_name);

} // namespace ResinProfile
} // namespace Slic3r::Biz
