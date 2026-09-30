#pragma once

#include "Slic3r/Biz/ResinProfile/ResinProfileImportInteractor.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Slic3r::App {

/**
 * @brief The badge one row of the mapping table carries, one per Biz::ResinProfile::MappingStatus.
 *
 * The comment of each value is the Platform::Color token the badge is drawn in, the "UI color token"
 * column of the mapping table in doc/sla-fork/ROADMAP.md. The tokens themselves stay in the dialog,
 * so that this file needs no theme and its tests can check what a row says without a window.
 */
enum class MappingBadge
{
    Exact, ///< Copied as-is. Drawn in AccentPrimary.
    Converted, ///< Unit or shape changed. Drawn in AccentSecondary.
    Approximated, ///< Semantics differ, a closest value was chosen. Drawn in Warning.
    NotApplicable, ///< Does not apply to the target printer. Drawn in Text, disabled.
    Unknown, ///< Key not recognized, kept in the report only. Drawn in Text, disabled.
};

/// @brief One row of the mapping table of the resin import dialog, ready to be shown.
/// A row with an empty @ref target_key writes nothing into the material preset; its badge and its
/// note still say why, so no value of the imported file is lost silently.
struct MappingRow
{
    /// The foreign key this row is about, as it appeared in the file.
    std::string source_key;
    /// The resin preset key the value is written to, empty when nothing is written.
    std::string target_key;
    /// The value as it is written, empty when nothing is written.
    std::string value;
    MappingBadge badge{MappingBadge::Unknown};
    /// Human readable reason, caveats and dropped values.
    std::string note;

    /// @brief Whether the row carries a value into the material preset.
    bool writes_value() const
    {
        return !target_key.empty();
    }
};

/// @brief How many rows carry each badge, for the summary line under the table. The wireframe counts
/// all five kinds, so nothing disappears between the table and the line under it.
struct BadgeCounts
{
    std::size_t exact{0};
    std::size_t converted{0};
    std::size_t approximated{0};
    std::size_t not_applicable{0};
    std::size_t unknown{0};

    /// @brief How many rows there are in total, the sum of the five kinds.
    std::size_t total() const;
};

/// @brief The badge a mapping status is shown with. Every status has one, so a status the mapper
/// ever adds cannot end up without a badge.
MappingBadge badge_of(Biz::ResinProfile::MappingStatus status);

/// @brief The user visible name of a badge, already translated.
std::string badge_label(MappingBadge badge);

/// @brief One row per key of @p result, in the order the mapper reports them: the recognized keys
/// first, the unknown ones last. A result of a failed import has no rows.
std::vector<MappingRow> build_mapping_rows(const Biz::ResinProfile::ResinImportResult& result);

/// @brief How many rows of @p rows carry each badge.
BadgeCounts count_badges(const std::vector<MappingRow>& rows);

/// @brief The line under the table counting the badges, e.g. "3 Exact, 1 Converted, 5 Not
/// applicable". A kind with no rows is left out so the line stays readable, and a report with no
/// rows at all gives an empty line, which the dialog hides.
std::string summary_line(const std::vector<MappingRow>& rows);

/// @brief The one line that says where the profile comes from: the file, the format its reader
/// recognized, and the resin and vendor it names. A profile that was built without a file (the
/// "New resin from datasheet" form) has no file to name, so the line says the format and the resin
/// alone. What the profile does not say is left out, so the line never shows an empty field. Empty
/// when there is nothing to say yet.
std::string source_summary(const Biz::ResinProfile::ResinImportResult& result);

/// @brief The longest preset name the importer can save. The name of a user preset is a file name,
/// and the importer cuts it off beyond this, so the dialog refuses it instead of changing it.
inline constexpr std::size_t MAX_PRESET_NAME_LENGTH = 100;

/**
 * @brief Why @p name cannot be used for the imported preset, empty when it can.
 *
 * The importer replaces the characters a file name may not carry and makes a name that is taken
 * unique, so those are not errors and the dialog does not refuse them. An empty name and a name
 * that is too long are, because the first has nothing to save under and the second would be cut
 * off behind the user's back.
 */
std::string validate_preset_name(std::string_view name);

} // namespace Slic3r::App
