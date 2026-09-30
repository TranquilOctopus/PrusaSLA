#include "Slic3r/Biz/ResinProfile/ResinImportReport.hpp"

#include "Slic3r/Biz/ResinProfile/ResinProfileMapper.hpp"

#include <boost/filesystem/path.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Slic3r::Biz::ResinProfile {

namespace {

/// @brief The name of a file, without the folder it was read from. The interactor records the path
/// as it was given, and a report is meant to be comparable, so the folder is left out of it. A path
/// that names no file at all (a folder that is not one) is kept as it is: dropping it would leave
/// the result nameless.
std::string file_name_of(const std::string& path)
{
    const std::string name = boost::filesystem::path{path}.filename().string();
    return name.empty() ? path : name;
}

/// @brief One row of the mapping table, in the key order the report is documented to have.
nlohmann::ordered_json mapping_entry_json(const MappedField& field)
{
    nlohmann::ordered_json entry;
    entry["key"]        = field.source_key;
    entry["target_key"] = field.target_key;
    // The value as the file had it, next to the value that is written to it, so the two can be read
    // against each other (a unit conversion, a clamp) without the note.
    entry["source_value"] = field.source_value;
    entry["value"]        = field.value;
    entry["status"]       = to_string(field.status);
    entry["note"]         = field.note;
    return entry;
}

/// @brief The mapping rows of one file, sorted by the key they are about.
nlohmann::ordered_json mapping_json(const MappingResult& mapping)
{
    // The mapper emits the rows in the order of the mapping table, which is the order the rows have
    // in the ROADMAP. Sorting them by their source key turns the report into a stable listing, so
    // that adding a rule to the table does not reshuffle the rows of every profile.
    std::vector<MappedField> fields = mapping.report;
    std::ranges::sort(fields, [](const MappedField& left, const MappedField& right)
    { return left.source_key < right.source_key; });

    nlohmann::ordered_json rows = nlohmann::ordered_json::array();
    for (const MappedField& field : fields)
        rows.push_back(mapping_entry_json(field));
    return rows;
}

} // namespace

std::string resin_import_report_json(const std::vector<ResinImportResult>& results)
{
    nlohmann::ordered_json report;
    nlohmann::ordered_json entries = nlohmann::ordered_json::array();

    for (const ResinImportResult& result : results) {
        nlohmann::ordered_json entry;
        entry["file"]        = file_name_of(result.file);
        entry["ok"]          = result.ok;
        entry["error"]       = result.error;
        entry["preset_name"] = result.preset_name;
        entry["base_preset"] = result.base_preset;
        entry["mapping"]     = mapping_json(result.mapping);
        entries.push_back(std::move(entry));
    }

    report["results"] = std::move(entries);
    // Compact, like the other JSON the command line writes: the report is a file scripts read, and
    // the per-file summary a person reads is printed to stdout by the caller.
    return report.dump();
}

} // namespace Slic3r::Biz::ResinProfile
