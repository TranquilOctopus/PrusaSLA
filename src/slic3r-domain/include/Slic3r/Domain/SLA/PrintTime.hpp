#pragma once

#include <cstddef>
#include <vector>

#include "Slic3r/Domain/Config.hpp"

namespace Slic3r::Domain {

/// Where one printed layer of a masked stereolithography print spends its time, in seconds. A
/// layer is exposed once with the light on, waits with the light off, and is then separated from
/// the vat by the lift and the retract of the build plate, so the three parts are kept apart:
/// which of them a setting moves is then visible in the estimate.
struct SlaLayerTime
{
    /// Light on: the burn-in exposure, the raft interface exposure inside the band, the faded
    /// exposure of a transition layer, or the normal exposure.
    double exposure_s = 0.;
    /// Light off: the wait_* delays of the separation (and the bottom_* ones on a bottom layer).
    double light_off_s = 0.;
    /// The lift and the retract, each at the speed its own setting gives it.
    double motion_s = 0.;

    double total_s() const { return exposure_s + light_off_s + motion_s; }
};

/// The estimated time a whole MSLA print takes, per layer and in total. This is the model for a
/// printer without a tilt: exposure, light-off waits and the lift/retract separation, summed over
/// the layers. It is not the SL1 firmware model, which adds the tilt, the tower microsteps and the
/// screen refresh of a Prusa machine - that one lives in
/// Domain::SLA::PrintStatistics::estimated_print_time and is what the .sl1 export writes.
/// The formula and where every term comes from is written up in
/// doc/sla-fork/profiling/print-time.md.
struct SlaPrintTimeEstimate
{
    /// One entry per printed layer, in print order, counted from the build plate.
    std::vector<SlaLayerTime> layers;
    /// Sum of the layer totals, in seconds. 0 while the estimate is not valid.
    double total_s = 0.;
    /// False when there was nothing to estimate (no layers), which is not the same as a print
    /// that takes no time.
    bool valid = false;

    /// The time spent up to and including each layer, for a layer view. Empty while not valid.
    std::vector<double> running_total_s() const;
};

/// Estimate how long a print with the given settings takes, for a printer that peels by lifting
/// the plate and has no tilt.
///
/// For every layer, counted from the build plate:
///  - exposure: initial_exposure_time for the burn-in layers (bottom_layer_count, or the
///    transition layers plus the first one), the raft interface exposure inside the band of
///    M2.14b3 (which never reaches into the burn-in), and otherwise a linear fade from
///    initial_exposure_time to exposure_time over the transition layers, never below
///    exposure_time;
///  - light-off: wait_before_lift + wait_after_lift + wait_after_retract, or the bottom_* three
///    on a burn-in layer;
///  - motion: lift_height / lift_speed + lift_height / retract_speed for the lift and the
///    retract, plus the same over lift_height_2 and the second-stage speeds when the print sets
///    them. There is no retract distance setting, so the plate returns over the distance it was
///    lifted, which is what the exporters assume too. A speed of zero spends no time on that
///    stage instead of dividing by zero.
///
/// A setting that is missing from the view counts as 0, so a printer-only view estimates the
/// exposures alone.
/// @param cfg A finalized config view.
/// @param layer_count How many layers the print has; 0 or less gives an estimate that is not
///                   valid, which is the "no print yet" state.
SlaPrintTimeEstimate sla_estimate_print_time(const ConfigView& cfg, int layer_count);

} // namespace Slic3r::Domain
