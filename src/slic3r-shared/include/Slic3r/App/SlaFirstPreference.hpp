#pragma once

#include "Slic3r/Domain/PrinterTechnology.hpp"
#include <vector>

namespace Slic3r::App {

/**
 * @brief Returns the index of the printer that should be preselected based on the sla_first flag.
 *
 * @param technologies Vector of printer technologies in the order they appear.
 * @param sla_first If true and at least one SLA printer exists, returns the index of the first SLA printer.
 *                  Otherwise returns 0 (the first printer).
 * @return Index of the printer to preselect, or 0 if the list is empty.
 */
size_t select_preselected_printer_index(const std::vector<Domain::PrinterTechnology>& technologies, bool sla_first);

} // namespace Slic3r::App