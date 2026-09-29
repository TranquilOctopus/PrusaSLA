#pragma once

#include "Slic3r/Domain/PrinterTechnology.hpp"

#include <string>
#include <vector>

namespace Slic3r::App::Hints {

/// Which printers a hint is written for. FFF hints (filament, nozzle, seams, infill, brims) must
/// never reach a user whose selected printer is an SLA printer.
enum class HintAudience
{
    Any,
    Fff,
    Sla
};

struct PlaterHint
{
    std::string text;
    HintAudience audience{HintAudience::Any};
};

/// Every hint the app knows about, in the order they should be shown.
const std::vector<PlaterHint>& all_hints();

/// The hints that are relevant for a printer of the given technology. With an SLA printer
/// selected this is the SLA hints only - no filament, nozzle, seam, infill or brim hints.
std::vector<PlaterHint> hints_for(Domain::PrinterTechnology technology);

/// The hint shown once, when the first model lands on an SLA build plate. It is the first entry of
/// the SLA hints and points at the next step of the workflow.
const PlaterHint& first_model_on_sla_plate_hint();

} // namespace Slic3r::App::Hints
