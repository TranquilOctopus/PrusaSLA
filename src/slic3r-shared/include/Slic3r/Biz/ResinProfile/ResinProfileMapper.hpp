#pragma once

#include "Slic3r/Biz/ResinProfile/ForeignResinProfile.hpp"
#include "Slic3r/Domain/Config.hpp"

#include <map>
#include <string>
#include <vector>

namespace Slic3r::Biz::ResinProfile {

/**
 * @brief How faithfully one foreign key could be carried over into a PrusaSLA resin preset.
 * Every source key of a ForeignResinProfile ends up in the mapping report with one of these.
 */
enum class MappingStatus
{
    Exact, ///< Copied as-is, same meaning and same unit.
    Converted, ///< Unit or shape changed, e.g. mm/min -> mm/s or a per-litre price -> bottle cost.
    Approximated, ///< Semantics differ; a closest value was chosen.
    NotApplicable, ///< Does not apply to the target printer, e.g. lift settings on a tilt printer.
    Unknown, ///< Key not recognized; kept in the report only.
};

/// @brief Stable short name of a status, for the JSON report and the log.
std::string to_string(MappingStatus status);

/**
 * @brief One row of the mapping report: what became of one source key.
 * A row carries the value on both sides: the one the file had and the one that is written. That is
 * what makes a converted or an approximated value auditable without reading the note. target_key and
 * value are empty when nothing is written to the material preset; the status and the note still say
 * why, so a value is never lost silently.
 */
struct MappedField
{
    /// @brief The foreign key this row is about, verbatim as it appeared in the file.
    std::string source_key;
    /// @brief The value as the file had it, verbatim: the value every conversion, every rejection
    /// and every clamp below is about.
    std::string source_value;
    /// @brief The unit @ref source_value is in, as the mapping table states it. Empty when the key
    /// is not a quantity (a name, G-code, a setting of the preview) or when the unit is not settled
    /// yet, which is never spelled out as a guess. The spellings are the ones ConfigDefsSLA.cpp
    /// gives the same quantity, untranslated so that a report does not change with the language.
    std::string source_unit;
    /// @brief The PrusaSLA material (resin) preset key the value is written to, if any.
    std::string target_key;
    /// @brief The value as it is written to the material preset, if any.
    std::string value;
    /// @brief The unit @ref value is written in, empty for the same reason as @ref source_unit and
    /// always empty when nothing is written.
    std::string target_unit;
    MappingStatus status{MappingStatus::Unknown};
    /// @brief Human readable reason, caveats ("verify" in the ROADMAP table) and dropped values.
    std::string note;
};

/// @brief The two kinds of target printer the mapping table distinguishes.
enum class TargetPrinterClass
{
    Tilt, ///< Prusa SL1 / SL1S: separates layers by tilting, has no lift/retract motion.
    GenericMsla ///< Anycubic, Elegoo, ...: separates layers by lifting the build plate.
};

/// @brief Stable short name of a printer class, for the JSON report.
std::string to_string(TargetPrinterClass printer_class);

/**
 * @brief The outcome of mapping one ForeignResinProfile onto one target printer.
 * Values are strings, in the form the config uses in an .ini file: "2.5" for a scalar,
 * "3,3" for a two-element vector. The caller writes them into an sla_material preset.
 */
struct MappingResult
{
    /// @brief PrusaSLA material (resin) preset keys and the values to write, no other keys.
    std::map<std::string, std::string> material_values;
    /// @brief One row per recognized key that is in the profile, then one per unknown key.
    std::vector<MappedField> report;
    /// @brief Preset name suggested by the profile (its own profile name, else the printer hint).
    std::string suggested_name;
};

/**
 * @brief Map a foreign resin profile onto a PrusaSLA resin (material) preset for one target printer.
 * Implements the "Chitubox .cfg -> PrusaSLA mapping" table in doc/sla-fork/ROADMAP.md. Pure: no UI,
 * no IO, no global state. Only keys that exist in the SLA config definitions are written.
 * @param profile The neutral profile as read by a IResinProfileReader.
 * @param printer_class The class of the target printer, see printer_class_from_config().
 */
MappingResult
map_resin_profile(const ForeignResinProfile& profile, TargetPrinterClass printer_class);

/**
 * @brief Decide which mapping table a printer's settings get.
 * The printer model decides: a Prusa machine that separates the layers by tilting (SL1, SL1S, M1,
 * SLX) is a Tilt printer, every other model (Anycubic, Elegoo, ...) is a generic MSLA printer. A
 * settings set that names no model falls back to "use_tilt", and a view with neither hint is
 * treated as the SL1 this fork is built around.
 * @param printer_config The target printer's settings, finalized: options missing from the view
 *                       are ignored, and an unfinalized view carries none.
 */
TargetPrinterClass printer_class_from_config(const Domain::ConfigView& printer_config);

} // namespace Slic3r::Biz::ResinProfile
