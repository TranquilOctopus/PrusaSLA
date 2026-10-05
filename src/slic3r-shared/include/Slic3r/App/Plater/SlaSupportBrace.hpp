#pragma once

#include "Slic3r/Domain/SLA/SupportPoint.hpp"

#include <optional>
#include <unordered_set>

namespace Slic3r::App::Plater {

/// The three states of the per-point "may this support be braced" switch (M2.38), as the support
/// point carries them: Inherit leaves the decision to the object's support_brace_enable, On asks
/// for a brace even where the object has bracing off, Off keeps this one pillar out of every brace
/// even where the object has bracing on.
using SupportBrace = Domain::SLA::SupportPoint::Brace;

/// The state the control is shown with, or empty when nothing is selected or the selected points
/// disagree. The control is then left blank ("Mixed") rather than showing a state only some of the
/// selected points have, the same way the other per-point fields of the "Selected supports" group
/// behave (M2.16c).
std::optional<SupportBrace> selection_support_brace(
    const Domain::SLA::SupportPoints& points,
    const std::unordered_set<size_t>& selected_point_indices
);

} // namespace Slic3r::App::Plater
