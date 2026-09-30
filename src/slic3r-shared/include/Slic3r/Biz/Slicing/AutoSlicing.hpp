#pragma once

#include <Slic3r/Domain/PrinterTechnology.hpp>

namespace Slic3r::Biz::Slicing {

/** Whether a bed of the given printer technology may start slicing without the user asking for it.
 *  For SLA this is never the case: an SLA bed is only sliced from the explicit Slice button, so
 *  supports stay a separate, manual step (M2.17d1, M2.17d5). The check sits with the auto slice
 *  request itself, so that no caller (Preview activation, bed selection, the auto-reslice toggle)
 *  can leave an SLA bed auto slicing. */
constexpr bool is_auto_slicing_allowed(
    const Domain::PrinterTechnology technology,
    const bool requested
)
{
    return requested && technology != Domain::PrinterTechnology::SLA;
}

} // namespace Slic3r::Biz::Slicing
