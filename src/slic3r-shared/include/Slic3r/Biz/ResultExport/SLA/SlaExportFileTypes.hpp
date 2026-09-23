#pragma once

#include <string>
#include <vector>

namespace Slic3r::Biz::PrintHost::Sla {

// One entry of the SLA export file dialog.
struct SlaExportFileType
{
    std::string description; // e.g. "Anycubic Photon Mono M5 PM5 format"
    std::string extension;   // without the dot, lower case, e.g. "pm5"
    // Whether the sliced data can be written in this file type. Layers are encoded for the
    // printer's format at slice time, so only that format's extensions can be written without
    // slicing again.
    bool matches_sliced_format{false};
};

// The file types offered when exporting an SLA bed: the printer's own file type first (the
// default), then the other extensions of the same format, then every other registered format.
// `archive_format` is the printer's `sla_archive_format` (an extension such as "pm5" or "SL1").
// `filename_extension` is the extension of the output filename template, which Prusa presets use
// to choose between .sl1 and .sl1s; it only counts when it belongs to the printer's format.
std::vector<SlaExportFileType> sla_export_file_types(
    const std::string& archive_format, const std::string& filename_extension = {}
);

// The extension an SLA export should default to, with the leading dot (e.g. ".pm5"). Empty when
// the printer's archive format is not registered.
std::string sla_default_export_extension(
    const std::string& archive_format, const std::string& filename_extension = {}
);

// Whether a file named with `extension` (with or without the dot, any case) can hold data
// sliced for `archive_format`.
bool sla_extension_matches_format(const std::string& extension, const std::string& archive_format);

// Whether `extension` (with or without the dot, any case) belongs to any registered SLA format.
bool is_sla_export_extension(const std::string& extension);

} // namespace Slic3r::Biz::PrintHost::Sla
