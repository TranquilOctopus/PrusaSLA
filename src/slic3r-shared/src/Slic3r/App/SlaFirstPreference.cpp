#include "Slic3r/App/SlaFirstPreference.hpp"

#include <algorithm>

namespace Slic3r::App {

size_t select_preselected_printer_index(const std::vector<Domain::PrinterTechnology>& technologies, bool sla_first)
{
    if (technologies.empty()) {
        return 0;
    }

    if (!sla_first) {
        return 0;
    }

    auto it = std::find(technologies.begin(), technologies.end(), Domain::PrinterTechnology::SLA);
    if (it != technologies.end()) {
        return static_cast<size_t>(std::distance(technologies.begin(), it));
    }

    return 0;
}

} // namespace Slic3r::App