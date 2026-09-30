#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace Slic3r::App {

/// What a key pressed in the SLA layer image window asks for (M5.6d).
enum class SlaLayerKey
{
    PreviousLayer,      ///< one layer below the current one
    NextLayer,          ///< one layer above the current one
    PreviousTenLayers,  ///< ten layers below, as far as there are any
    NextTenLayers,      ///< ten layers above, as far as there are any
    FirstLayer,
    LastLayer,
    PreviousIssue,      ///< the closest layer with an issue below the current one
    NextIssue,          ///< the closest layer with an issue above the current one
};

/// The layer @p key moves to, or nothing when it moves nowhere: no layer at all, a key that
/// leaves the layer where it is (the first layer and Home, the last layer and End), or an issue
/// key with no issue in that direction. @p current_layer is clamped into the range of @p
/// layer_count first, so a slider that is out of step with the result still lands on a layer
/// that exists. @p issue_layers is ascending and free of duplicates, as sla_issue_layers()
/// returns it; an issue on a layer the result does not have is skipped.
std::optional<size_t> sla_layer_for_key(
    SlaLayerKey key,
    size_t current_layer,
    size_t layer_count,
    const std::vector<size_t>& issue_layers
);

} // namespace Slic3r::App