#include "Slic3r/Biz/PrintHost/PrintHostFormats.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp" // translations

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <type_traits>
#include <variant>

namespace Slic3r::Biz::PrintHost {

namespace {

std::string normalized_extension(std::string extension)
{
    if (!extension.empty() && extension.front() == '.')
        extension.erase(0, 1);
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); }
    );
    return extension;
}

bool is_gcode(const std::string& extension)
{
    return extension == "gcode" || extension == "bgcode";
}

bool is_sl1(const std::string& extension)
{
    return extension == "sl1" || extension == "sl1s";
}

// Prusa Connect fronts the same firmware a PrusaLink entry does, so the SL1 is the printer that
// can be on it and does not print G-Code.
bool connect_upload_accepts_extension(const std::string& extension)
{
    return is_gcode(extension) || is_sl1(extension);
}

// How the destination is named in a message: the name the user gave the printer if there is one,
// the kind of host otherwise.
std::string destination_name(const PhysicalPrinter::PhysicalPrinterConfig& destination)
{
    if (!destination.name.empty())
        return destination.name;
    return std::visit(
        [](const auto& payload) -> std::string
        {
            using Payload = std::remove_cvref_t<decltype(payload)>;
            if constexpr (std::is_same_v<Payload, PhysicalPrinter::PrinterUpload>) {
                return print_host_type_to_string(payload.type);
            } else if constexpr (std::is_same_v<Payload, PhysicalPrinter::ConnectUpload>) {
                return _u8L("Prusa Connect");
            } else {
                return {};
            }
        },
        destination.payload
    );
}

} // namespace

bool print_host_accepts_extension(Domain::PrintHostType type, const std::string& extension)
{
    const std::string ext = normalized_extension(extension);
    switch (type) {
    case Domain::PrintHostType::PrusaLink:
        return is_gcode(ext) || is_sl1(ext);
    case Domain::PrintHostType::SL1Host:
        return is_sl1(ext);
    case Domain::PrintHostType::OctoPrint:
    case Domain::PrintHostType::Moonraker:
    case Domain::PrintHostType::Duet:
    case Domain::PrintHostType::FlashAir:
    case Domain::PrintHostType::AstroBox:
    case Domain::PrintHostType::Repetier:
    case Domain::PrintHostType::MKS:
        return is_gcode(ext);
    case Domain::PrintHostType::PrusaLinkStorage:
        // A storage query, not an upload, so it carries no file at all.
        return false;
    }
    return false;
}

bool destination_accepts_extension(
    const PhysicalPrinter::PhysicalPrinterConfig& destination,
    const std::string& extension
)
{
    const std::string ext = normalized_extension(extension);
    return std::visit(
        [&ext](const auto& payload) -> bool
        {
            using Payload = std::remove_cvref_t<decltype(payload)>;
            if constexpr (std::is_same_v<Payload, PhysicalPrinter::PrinterUpload>) {
                return print_host_accepts_extension(payload.type, ext);
            } else if constexpr (std::is_same_v<Payload, PhysicalPrinter::ConnectUpload>) {
                return connect_upload_accepts_extension(ext);
            } else {
                // A filesystem destination writes the file and talks to no printer, so it takes
                // whatever the build plate was sliced for.
                return true;
            }
        },
        destination.payload
    );
}

std::string destination_rejects_extension_message(
    const PhysicalPrinter::PhysicalPrinterConfig& destination,
    const std::string& extension
)
{
    if (destination_accepts_extension(destination, extension))
        return {};

    const std::string ext = normalized_extension(extension);
    if (ext.empty())
        return fmt::format(
            fmt::runtime(
                _u8L("{} needs a file name with an extension it accepts, for example .gcode")
            ),
            destination_name(destination)
        );

    return fmt::format(
        fmt::runtime(_u8L("{} does not accept {} files")),
        destination_name(destination),
        "." + ext
    );
}

} // namespace Slic3r::Biz::PrintHost
