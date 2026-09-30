#pragma once

#include "Slic3r/Domain/SLA/SupportPoint.hpp"

#include <optional>
#include <unordered_set>

namespace Slic3r::App::Plater {

/// The three states of the per-point "may this support end on the model" switch (M2.26), as the
/// support point carries them: Inherit leaves the decision to the object's "supports must not end
/// on the model", Allow lets this one support be anchored on the model even then, Forbid keeps it
/// off the model even when the object allows it.
using SupportOnModel = Domain::SLA::SupportPoint::OnModel;

/// The state the control is shown with, or empty when nothing is selected or the selected points
/// disagree. The control is then left blank rather than showing a state only some of the selected
/// points have, the same way the per-point geometry fields behave (M2.16c).
std::optional<SupportOnModel> selection_support_on_model(
    const Domain::SLA::SupportPoints& points,
    const std::unordered_set<size_t>& selected_point_indices
);

} // namespace Slic3r::App::Plater
