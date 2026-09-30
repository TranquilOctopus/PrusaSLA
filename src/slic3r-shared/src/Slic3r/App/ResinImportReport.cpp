#include "Slic3r/App/ResinImportReport.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

#include <boost/filesystem/path.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Slic3r::App {

using Biz::ResinProfile::MappedField;
using Biz::ResinProfile::MappingStatus;
using Biz::ResinProfile::ResinImportResult;

namespace {

/// @brief The UTF-8 bytes of U+00B7, the dot the summary lines separate their parts with. Spelled
/// as bytes rather than as the character itself, because this project is not compiled with /utf-8, so
/// a narrow literal would be encoded in the execution code page instead and arrive as something else.
/// It is kept out of every translatable string as well, see the ellipsis note in SlaPreExportCheck.
constexpr std::string_view part_separator{"  \xC2\xB7  "};

/// @brief @p parts joined by the separator, skipping the ones that are empty.
std::string join_parts(const std::vector<std::string>& parts)
{
    std::string out;
    for (const std::string& part : parts) {
        if (part.empty()) {
            continue;
        }
        if (!out.empty()) {
            out += part_separator;
        }
        out += part;
    }
    return out;
}

} // namespace

MappingBadge badge_of(MappingStatus status)
{
    switch (status) {
    case MappingStatus::Exact:
        return MappingBadge::Exact;
    case MappingStatus::Converted:
        return MappingBadge::Converted;
    case MappingStatus::Approximated:
        return MappingBadge::Approximated;
    case MappingStatus::NotApplicable:
        return MappingBadge::NotApplicable;
    case MappingStatus::Unknown:
        return MappingBadge::Unknown;
    }
    // A status the mapper gains later is reported as Unknown rather than dropped: the row is still
    // shown, and Unknown is the one status that promises nothing was written.
    return MappingBadge::Unknown;
}

std::string badge_label(MappingBadge badge)
{
    switch (badge) {
    case MappingBadge::Exact:
        return Biz::_u8L("Exact");
    case MappingBadge::Converted:
        return Biz::_u8L("Converted");
    case MappingBadge::Approximated:
        return Biz::_u8L("Approximated");
    case MappingBadge::NotApplicable:
        return Biz::_u8L("Not applicable");
    case MappingBadge::Unknown:
        return Biz::_u8L("Unknown");
    }
    return Biz::_u8L("Unknown");
}

std::vector<MappingRow> build_mapping_rows(const ResinImportResult& result)
{
    std::vector<MappingRow> rows;
    rows.reserve(result.mapping.report.size());
    for (const MappedField& field : result.mapping.report) {
        MappingRow row;
        row.source_key = field.source_key;
        row.target_key = field.target_key;
        row.value      = field.value;
        row.badge      = badge_of(field.status);
        row.note       = field.note;
        rows.push_back(std::move(row));
    }
    return rows;
}

std::size_t BadgeCounts::total() const
{
    return exact + converted + approximated + not_applicable + unknown;
}

BadgeCounts count_badges(const std::vector<MappingRow>& rows)
{
    BadgeCounts counts;
    for (const MappingRow& row : rows) {
        switch (row.badge) {
        case MappingBadge::Exact:
            ++counts.exact;
            break;
        case MappingBadge::Converted:
            ++counts.converted;
            break;
        case MappingBadge::Approximated:
            ++counts.approximated;
            break;
        case MappingBadge::NotApplicable:
            ++counts.not_applicable;
            break;
        case MappingBadge::Unknown:
            ++counts.unknown;
            break;
        }
    }
    return counts;
}

std::string summary_line(const std::vector<MappingRow>& rows)
{
    const BadgeCounts counts = count_badges(rows);

    const auto with_count = [](std::size_t count, const std::string& label)
    {
        if (count == 0) {
            return std::string{};
        }
        // TRN: How many mapping rows ended up with this status. {0} counts the rows.
        return fmt::format(fmt::runtime(Biz::_u8L("{0} {1}")), count, label);
    };

    return join_parts({
        with_count(counts.exact, badge_label(MappingBadge::Exact)),
        with_count(counts.converted, badge_label(MappingBadge::Converted)),
        with_count(counts.approximated, badge_label(MappingBadge::Approximated)),
        with_count(counts.not_applicable, badge_label(MappingBadge::NotApplicable)),
        with_count(counts.unknown, badge_label(MappingBadge::Unknown)),
    });
}

std::string source_summary(const ResinImportResult& result)
{
    const std::string file_name = boost::filesystem::path{result.file}.filename().string();
    // A profile typed into the "New resin from datasheet" form has no file behind it, so the format
    // it was built as and the resin it names are all there is to say. Nothing read yet, nothing said.
    if (file_name.empty() && result.source_format.empty()) {
        return {};
    }
    return join_parts({file_name, result.source_format, result.resin_name, result.resin_vendor});
}

std::string validate_preset_name(std::string_view name)
{
    // An empty input is "all whitespace" too, so the blank name is caught here as well.
    if (std::ranges::
            all_of(name, [](unsigned char character) { return std::isspace(character) != 0; }))
    {
        return Biz::_u8L("Enter a name for the resin profile.");
    }
    if (name.size() > MAX_PRESET_NAME_LENGTH) {
        // TRN: Name of the imported resin preset is too long. {0} is the number of characters that
        // are allowed.
        return fmt::format(
            fmt::runtime(Biz::_u8L("The name is too long: {0} characters at most.")),
            MAX_PRESET_NAME_LENGTH
        );
    }
    return {};
}

} // namespace Slic3r::App
