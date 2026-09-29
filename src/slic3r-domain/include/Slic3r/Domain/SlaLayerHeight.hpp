#pragma once

#include "Slic3r/Domain/Config.hpp"

namespace Slic3r::Domain {

/// Layer height the SLA pipeline must use: the resin's own layer height when the resin
/// defines one, otherwise the print preset's layer height (Supports & raft).
/// @param cfg A finalized config view. Options missing from the view count as 0, so a
///             printer-only view (no resin settings) falls back to "layer_height".
/// @return The effective layer height in mm, or 0 when neither option is present.
double sla_effective_layer_height(const ConfigView& cfg);

/// Number of layers over which the SLA pipeline fades the exposure from initial_exposure_time
/// down to exposure_time: the resin's own transition layer count when the resin defines one,
/// otherwise the print preset's "faded_layers" (Supports & raft).
/// @param cfg A finalized config view. Options missing from the view count as "not set", so a
///             printer-only view (no resin settings) falls back to "faded_layers".
/// @return The effective transition layer count, or 0 when neither option is present.
int sla_effective_faded_layers(const ConfigView& cfg);

/// Number of layers an exported file has to expose at initial_exposure_time. The engine has no
/// burn-in count of its own: it exposes the first layer at initial_exposure_time and fades to
/// exposure_time over the transition layers, so the default is the transition layers plus that
/// first one. A resin that states its own burn-in (bottom_layer_count, the number of layers
/// using the bottom_* parameters, which the engine does not read) wins over that default.
/// @return The burn-in layer count, never negative.
int sla_bottom_layer_count(const ConfigView& cfg);

} // namespace Slic3r::Domain
