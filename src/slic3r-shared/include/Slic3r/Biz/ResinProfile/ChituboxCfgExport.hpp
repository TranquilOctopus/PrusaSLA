#pragma once

#include "Slic3r/Biz/ResinProfile/ResinProfileMapper.hpp"
#include "Slic3r/Domain/Config.hpp"

#include <string>
#include <vector>

namespace Slic3r::Biz::ResinProfile {

/**
 * @brief What the export did with one resin preset key, the mirror image of one row of the report
 * the import writes (MappedField).
 */
struct ExportedResinKey
{
    /// @brief The resin preset key this row is about.
    std::string material_key;
    /// @brief The Chitubox .cfg key the value goes to, empty when the setting has no equivalent.
    std::string chitubox_key;
    /// @brief The value as it is written into the file, empty when nothing is written.
    std::string value;
    /// @brief The key of the unit the value is in, for a setting that format states as two keys
    /// ("resinPrice" and "resinUnit" are one price). Empty when the row writes one key alone.
    std::string unit_key;
    /// @brief The value written under unit_key, empty when there is none.
    std::string unit_value;
    /// @brief How faithfully the setting survives the trip, the same statuses the import uses.
    MappingStatus status{MappingStatus::NotApplicable};
    /// @brief Why, and for a value left out of the file, what it was.
    std::string note;
};

/**
 * @brief A resin preset written out as a Chitubox .cfg, with the report of every key that was
 * looked at.
 */
struct ChituboxCfgExport
{
    /// @brief The file, in the "key: value" form ChituboxCfgReader reads back.
    std::string text;
    /// @brief One row per key of the reverse mapping table, whether it was written or only reported.
    std::vector<ExportedResinKey> keys;
    /// @brief The keys that carry nothing into the file, in the order of the table.
    std::vector<std::string> skipped;
};

/**
 * @brief The export with its report: every key of the reverse mapping table, what became of it and
 * why. The keys of a resin preset the table does not know are not in the report, exactly as the
 * import reports only the source keys the table knows.
 * @param material The resin preset, as its Domain::ConfigItems.
 * @param printer_class The class of the printer the profile is made for, see
 *                      printer_class_from_config() and export_printer_class().
 * @param profile_name Written as the currProfile key, empty writes no name.
 */
ChituboxCfgExport export_chitubox_cfg_report(
    const Domain::ConfigItems& material,
    TargetPrinterClass printer_class,
    const std::string& profile_name = {}
);

/**
 * @brief Write a resin preset as a Chitubox .cfg, the M3.5/M3.6 mapping table in reverse. The keys
 * the reader of that format reads are the keys this writes, the units are converted back, and a
 * setting Chitubox has no key for is left out of the file and named by the report of
 * export_chitubox_cfg_report() instead of being dropped silently.
 * @param material The resin preset, as its Domain::ConfigItems.
 * @param printer_class The class of the printer the profile is made for.
 * @param profile_name Written as the currProfile key, empty writes no name.
 */
std::string export_chitubox_cfg(
    const Domain::ConfigItems& material,
    TargetPrinterClass printer_class,
    const std::string& profile_name = {}
);

/**
 * @brief Which of the two mapping tables a resin preset is exported with, decided the way the
 * import decides it: the printer model first, use_tilt only as the hint it is for a printer that
 * names no model, so that an export and an import of one preset go through the same table.
 * @param material The resin preset the use_tilt hint is read out of.
 * @param printer_model printer_model of the printer, empty when it names no model.
 */
TargetPrinterClass export_printer_class(const Domain::ConfigItems& material, const std::string& printer_model);

} // namespace Slic3r::Biz::ResinProfile
