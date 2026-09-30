#include "Slic3r/App/SlaLayerKeys.hpp"

#include <algorithm>
#include <iterator>

namespace Slic3r::App {

namespace {

/// The closest issue layer above @p current, skipped when the result has no such layer.
std::optional<size_t> next_issue_layer(const std::vector<size_t>& issue_layers, size_t current, size_t layer_count)
{
    const auto it = std::upper_bound(issue_layers.begin(), issue_layers.end(), current);
    if (it == issue_layers.end() || *it >= layer_count) {
        return std::nullopt;
    }
    return *it;
}

/// The closest issue layer below @p current, skipped when the result has no such layer.
std::optional<size_t> previous_issue_layer(const std::vector<size_t>& issue_layers, size_t current, size_t layer_count)
{
    const auto it = std::lower_bound(issue_layers.begin(), issue_layers.end(), current);
    if (it == issue_layers.begin()) {
        return std::nullopt;
    }
    const size_t layer = *std::prev(it);
    if (layer >= layer_count) {
        return std::nullopt;
    }
    return layer;
}

/// @p target, or nothing when it is the layer the user is already on: a key that does not move
/// the view is not a layer to go to, and the caller uses that to leave the slider alone.
std::optional<size_t> moved_to(size_t current, size_t target)
{
    if (target == current) {
        return std::nullopt;
    }
    return target;
}

} // namespace

std::optional<size_t> sla_layer_for_key(
    SlaLayerKey key,
    size_t current_layer,
    size_t layer_count,
    const std::vector<size_t>& issue_layers
)
{
    if (layer_count == 0) {
        return std::nullopt;
    }

    // A slider left over from another slice can point outside the layers of this result, so the
    // layer every key starts from is the one it actually shows.
    const size_t current = std::min(current_layer, layer_count - 1);

    switch (key) {
    case SlaLayerKey::PreviousLayer:
        return current > 0 ? moved_to(current, current - 1) : std::nullopt;
    case SlaLayerKey::NextLayer:
        return moved_to(current, std::min(current + 1, layer_count - 1));
    case SlaLayerKey::PreviousTenLayers:
        return moved_to(current, current > 10 ? current - 10 : 0);
    case SlaLayerKey::NextTenLayers:
        return moved_to(current, std::min(current + 10, layer_count - 1));
    case SlaLayerKey::FirstLayer:
        return moved_to(current, 0);
    case SlaLayerKey::LastLayer:
        return moved_to(current, layer_count - 1);
    case SlaLayerKey::PreviousIssue:
        return previous_issue_layer(issue_layers, current, layer_count);
    case SlaLayerKey::NextIssue:
        return next_issue_layer(issue_layers, current, layer_count);
    }

    return std::nullopt;
}

} // namespace Slic3r::App