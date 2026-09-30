#pragma once

#include "Slic3r/Biz/ResinProfile/ForeignResinProfile.hpp"
#include "Slic3r/Biz/ResinProfile/ResinProfileMapper.hpp"
#include "Slic3r/Biz/ResinProfile/ResinProfileReaderRegistry.hpp"
#include "Slic3r/Domain/SelectionId.hpp"

#include <boost/filesystem/path.hpp>

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r::Biz {

class ProjectInteractor;

namespace Preset {
class PresetInteractor;
} // namespace Preset

namespace ResinProfile {

/// @brief Which printer a profile is imported into.
/// The imported preset is written into the config container of the selected project, so the target
/// names that container; a target that is not the current selection is refused with an error rather
/// than silently imported somewhere else. Domain::INVALID_ID means "whatever is selected".
struct ResinImportTarget
{
    Domain::SelectionId project_id{Domain::INVALID_ID};
    Domain::SelectionId config_container_id{Domain::INVALID_ID};
    /// Resin slot in the container. An SLA printer has exactly one.
    size_t material_slot{0};
};
/// @brief What one imported file became. One per file, also for a file that failed.
struct ResinImportResult
{
    /// The file that was imported.
    std::string file;
    /// False when nothing was written; @ref error then says why.
    bool ok{false};
    /// Why the import failed, empty on success.
    std::string error;
    /// Name of the user preset that was created, or that a dry run would create. Empty on failure.
    std::string preset_name;
    /// Name of the system resin preset the new one inherits from. Empty on failure.
    std::string base_preset;
    /// Id of that system resin preset, the one the import dialog keeps selected in its base picker.
    std::string base_preset_id;
    /// Id of the format the reader recognized, e.g. "chitubox-cfg". Empty when nothing was read.
    std::string source_format;
    /// The resin the file names, empty when it names none. It is also the resin the importer looks
    /// for among the system resins to use as the base, and its name suggests the preset name.
    std::string resin_name;
    /// Vendor of that resin, empty when the file names none.
    std::string resin_vendor;
    /// What became of every key in the file, whether it was written or only reported.
    MappingResult mapping;
};

/**
 * @brief The system resin presets the selected printer offers, as (id, name) pairs, in the order
 * the printer offers them. These are the bases an imported preset can inherit from: a user or
 * runtime preset would give it no system preset to inherit from, and the unnamed shared profiles
 * (*common*, *sl1s_fast*, ...) are not a resin of this printer at all, only the values a resin
 * starts from. Empty when the printer is not an SLA printer or has no resin in that slot.
 * The import dialog offers these so the user can pick the base (M3.10a).
 */
std::vector<std::pair<std::string, std::string>>
system_resin_presets(const Preset::PresetInteractor& presets, Domain::SelectionId project_id, size_t slot);

/**
 * @brief Reads foreign resin profiles and saves them as PrusaSLA user resin presets.
 *
 * The flow of one file: the registry picks a reader by content, a system resin preset of the
 * target printer becomes the base, the mapper turns the profile into PrusaSLA material values for
 * the mapping table that base asks for, and the values are written on top of it and saved as a
 * user preset that inherits from that base. The base is the system resin whose name or vendor
 * matches the resin the profile names; without a match it is the printer's own default resin (the
 * one its print preset names, else the one it has selected, else the first resin it offers).
 * Nothing is written when the import is a dry run.
 *
 * Presets are only saved through PresetInteractor, so the user preset is a real preset: it shows up
 * in the material list, it reloads with the bundle, and it is selected in the target container.
 *
 * The files are untrusted input: the registry caps their size and never evaluates anything in them.
 */
class ResinProfileImportInteractor
{
public:
    /// @brief The date written into the source note, as YYYY-MM-DD. Tests pin it.
    using DateProvider = std::function<std::string()>;

    /// @brief A registry with the readers this fork ships (today: the Chitubox .cfg reader).
    static ResinProfileReaderRegistry default_registry();

    /// @param date_provider The date written into the source note; today by default.
    /// @param registry The readers to pick from; the ones this fork ships by default.
    ResinProfileImportInteractor(Biz::ProjectInteractor& project_interactor,
                                 DateProvider date_provider = {},
                                 ResinProfileReaderRegistry registry = default_registry());

    /**
     * @brief Import one resin profile file into @p target.
     * @param dry_run Report what the import would do without touching a preset. The name is the one
     *                the import would use, so it accounts for the names already taken.
     * @param base_preset_id Id of the system resin preset the new one inherits from, e.g. the one the
     *                       import dialog picked. Empty picks it from the profile as described above;
     *                       an id that is not a system resin of this printer is ignored the same way.
     * @param preset_name Name to save under, e.g. the one the user typed in the import dialog. Empty
     *                    derives it from the profile. It is sanitized and made unique either way, so
     *                    it can never replace a preset that is already there.
     */
    ResinImportResult import_file(const boost::filesystem::path& path,
                                  const ResinImportTarget& target = {},
                                  bool dry_run = false,
                                  const std::string& base_preset_id = {},
                                  const std::string& preset_name = {});

    /// @brief Import every regular file in @p folder, sorted by name. One result per file, errors
    /// collected: a file the registry does not recognise, or that fails, never stops the batch.
    std::vector<ResinImportResult> import_folder(const boost::filesystem::path& folder,
                                                 const ResinImportTarget& target = {},
                                                 bool dry_run = false);

    /// @brief Cap on the files one folder import reads, so a huge folder cannot run away.
    static constexpr std::size_t MAX_BATCH_FILES = 1000;

private:
    Biz::ProjectInteractor& m_project_interactor;
    ResinProfileReaderRegistry m_registry;
    DateProvider m_date_provider;
};

} // namespace ResinProfile
} // namespace Slic3r::Biz
