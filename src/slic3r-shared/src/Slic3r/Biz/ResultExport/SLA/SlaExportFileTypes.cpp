#include "Slic3r/Biz/ResultExport/SLA/SlaExportFileTypes.hpp"
#include "Slic3r/Biz/ResultExport/SLA/SlaArchiveFormat.hpp"

#include <algorithm>
#include <cctype>

namespace Slic3r::Biz::PrintHost::Sla {

namespace {

std::string normalized_extension(std::string extension)
{
    if (!extension.empty() && extension.front() == '.')
        extension.erase(0, 1);
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return extension;
}

// "zip" is an alias the SL1 reader accepts, not a file type to offer when saving.
bool offered(const std::string& extension) { return extension != "zip"; }

} // namespace

std::vector<SlaExportFileType> sla_export_file_types(
    const std::string& archive_format, const std::string& filename_extension
)
{
    register_sla_archive_formats();
    const SlaArchiveFormatRegistry& registry = SlaArchiveFormatRegistry::instance();

    const std::unique_ptr<ISlaArchiveFormat> printer_format =
        registry.find_by_extension(normalized_extension(archive_format));

    std::vector<SlaExportFileType> result;
    std::vector<SlaExportFileType> others;
    for (const std::string& name : registry.names()) {
        const std::unique_ptr<ISlaArchiveFormat> format = registry.get(name);
        const bool matches =
            printer_format && format->file_data_type() == printer_format->file_data_type();
        for (const std::string& raw_extension : format->extensions()) {
            const std::string extension = normalized_extension(raw_extension);
            if (offered(extension))
                (matches ? result : others).push_back({format->description(), extension, matches});
        }
    }

    // The printer's own file type goes first: the filename template's extension when it belongs
    // to the printer's format, otherwise the archive format itself when it is an extension.
    const std::string preferred = sla_default_export_extension(archive_format, filename_extension);
    const auto first = std::ranges::find_if(result, [&](const SlaExportFileType& type) {
        return "." + type.extension == preferred;
    });
    if (first != result.end())
        std::rotate(result.begin(), first, first + 1);

    result.insert(result.end(), others.begin(), others.end());
    return result;
}

std::string sla_default_export_extension(
    const std::string& archive_format, const std::string& filename_extension
)
{
    register_sla_archive_formats();
    const SlaArchiveFormatRegistry& registry = SlaArchiveFormatRegistry::instance();

    const std::string format_extension = normalized_extension(archive_format);
    const std::unique_ptr<ISlaArchiveFormat> printer_format = registry.find_by_extension(format_extension);
    if (!printer_format)
        return {};

    const std::string template_extension = normalized_extension(filename_extension);
    if (offered(template_extension) && sla_extension_matches_format(template_extension, archive_format))
        return "." + template_extension;
    if (offered(format_extension))
        return "." + format_extension;
    for (const std::string& extension : printer_format->extensions()) {
        if (offered(normalized_extension(extension)))
            return "." + normalized_extension(extension);
    }
    return {};
}

bool sla_extension_matches_format(const std::string& extension, const std::string& archive_format)
{
    register_sla_archive_formats();
    const SlaArchiveFormatRegistry& registry = SlaArchiveFormatRegistry::instance();
    const std::unique_ptr<ISlaArchiveFormat> file_format =
        registry.find_by_extension(normalized_extension(extension));
    const std::unique_ptr<ISlaArchiveFormat> printer_format =
        registry.find_by_extension(normalized_extension(archive_format));
    return file_format && printer_format
        && file_format->file_data_type() == printer_format->file_data_type();
}

bool is_sla_export_extension(const std::string& extension)
{
    register_sla_archive_formats();
    return SlaArchiveFormatRegistry::instance().find_by_extension(normalized_extension(extension))
        != nullptr;
}

} // namespace Slic3r::Biz::PrintHost::Sla
