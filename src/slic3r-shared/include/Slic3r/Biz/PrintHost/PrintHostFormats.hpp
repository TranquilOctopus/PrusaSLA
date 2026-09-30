#pragma once

#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterConfig.hpp"
#include "Slic3r/Domain/ConfigPhysical.hpp"

#include <string>

namespace Slic3r::Biz::PrintHost {

// Whether a print host takes a file with this extension (with or without the leading dot, in any
// case). The SL1 runs PrusaLink's firmware and speaks the same REST API, so a PrusaLink entry takes
// the SL1 archives and the SL1's own host entry takes nothing but those. The remaining hosts are
// FFF print servers, so they take G-Code. Everything else, an unknown extension included, is
// refused: uploading a file the host cannot read only leaves it in the printer's storage for
// nobody to print.
bool print_host_accepts_extension(Domain::PrintHostType type, const std::string& extension);

// Whether an export destination takes a file with this extension. A filesystem destination writes
// the file and talks to no printer, so it takes anything; the print hosts answer
// print_host_accepts_extension(), and Prusa Connect takes what PrusaLink takes.
bool destination_accepts_extension(
    const PhysicalPrinter::PhysicalPrinterConfig& destination,
    const std::string& extension
);

// The reason to show the user when the destination refuses the file, for example
// "OctoPrint does not accept .pm5 files". Empty when the destination accepts it.
std::string destination_rejects_extension_message(
    const PhysicalPrinter::PhysicalPrinterConfig& destination,
    const std::string& extension
);

} // namespace Slic3r::Biz::PrintHost
