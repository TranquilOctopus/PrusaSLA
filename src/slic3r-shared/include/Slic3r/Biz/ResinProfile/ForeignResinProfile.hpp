#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::Biz::ResinProfile {

/**
 * @brief Neutral container for a foreign resin profile read from an external format.
 * All keys are kept verbatim in raw_values; no mapping to PrusaSlicer presets happens here.
 */
struct ForeignResinProfile
{
    /// Format identifier, e.g. "chitubox-cfg"
    std::string source_format;
    /// Filesystem path the profile was read from
    std::string source_path;
    /// Printer name if the file names one, otherwise empty
    std::optional<std::string> printer_hint;
    /// Every key/value pair as it appeared in the file, untouched
    std::map<std::string, std::string> raw_values;
    /// Non-fatal issues: unknown keys, garbage values, etc.
    std::vector<std::string> warnings;
};

} // namespace Slic3r::Biz::ResinProfile