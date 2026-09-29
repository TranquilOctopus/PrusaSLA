#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::Biz::ResinProfile {

/**
 * @brief The material settings a reader could map out of a foreign file.
 * The unit is part of the field name, and a field left empty means the file did not say:
 * a missing setting is not the same as a setting of zero, and it is the mapper (M3.5)
 * that decides what to do about it.
 */
struct ResinMaterialSettings
{
    std::optional<double>      exposure_time_s;
    std::optional<double>      initial_exposure_time_s;
    std::optional<double>      layer_height_mm;
    std::optional<double>      initial_layer_height_mm;
    std::optional<int>         bottom_layer_count;
    std::optional<int>         faded_layer_count;
    std::optional<int>         slow_layer_count;
    std::optional<int>         fast_layer_count;
    std::optional<std::string> material_name;
    std::optional<std::string> material_vendor;
};

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
    /// The settings the reader recognised, mapped to the units of this struct
    ResinMaterialSettings material;
    /// Every key/value pair as it appeared in the file, untouched
    std::map<std::string, std::string> raw_values;
    /// Non-fatal issues: unknown keys, garbage values, etc.
    std::vector<std::string> warnings;
};

} // namespace Slic3r::Biz::ResinProfile