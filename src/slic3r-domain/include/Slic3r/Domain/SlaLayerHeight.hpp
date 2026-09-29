#pragma once

#include "Slic3r/Domain/Config.hpp"

namespace Slic3r::Domain {

/// Layer height the SLA pipeline must use: the resin's own layer height when the resin
/// defines one, otherwise the print preset's layer height (Supports & raft).
/// @param cfg A finalized config view. Options missing from the view count as 0, so a
///             printer-only view (no resin settings) falls back to "layer_height".
/// @return The effective layer height in mm, or 0 when neither option is present.
double sla_effective_layer_height(const ConfigView& cfg);

} // namespace Slic3r::Domain
