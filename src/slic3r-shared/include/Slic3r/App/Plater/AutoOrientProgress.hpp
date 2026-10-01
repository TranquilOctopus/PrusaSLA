#pragma once

#include "Slic3r/Domain/Percentage.hpp"

#include <algorithm>

namespace Slic3r::App::Plater {

// The two scales of the auto orientation progress (M4.15).
//
// The engine counts in per cent: `sla::AutoOrientStatus` hands out 0..100, and -1 when it only asks
// whether to go on. The job manager's `ProgressTracker` carries a fraction, 0..1, which is what
// every reader of it multiplies by 100 - the pop notification of the search included, so it showed
// "5500%" for a search that was at 55%. Both directions of that conversion live here, so it is
// written down once and can be tested without running a search.

// What the search reported, as the fraction the progress tracker is fed. Out of range values are
// clamped, which keeps the tracker's own 0..1 assert from ever firing.
inline Domain::Percentage auto_orient_progress_fraction(int percent)
{
    return Domain::Percentage{std::clamp(percent, 0, 100) / 100.};
}

// The fraction the tracker reported, back as the per cent the progress bar and its "Auto orient
// %1%/100" text show. Rounded, so that a fraction that came from a whole per cent comes back as it.
inline int auto_orient_progress_percent(Domain::Percentage fraction)
{
    return int(std::clamp(fraction.value * 100., 0., 100.) + 0.5);
}

} // namespace Slic3r::App::Plater
