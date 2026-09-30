#include "Slic3r/App/Plater/SlaSupportOnModel.hpp"

namespace Slic3r::App::Plater {

std::optional<SupportOnModel> selection_support_on_model(
    const Domain::SLA::SupportPoints& points,
    const std::unordered_set<size_t>& selected_point_indices)
{
    std::optional<SupportOnModel> common;
    for (size_t idx : selected_point_indices) {
        if (idx >= points.size()) {
            continue;
        }
        const SupportOnModel on_model = points[idx].on_model;
        if (!common.has_value()) {
            common = on_model;
        } else if (*common != on_model) {
            // The points of the selection disagree, so there is no value to show.
            return std::nullopt;
        }
    }

    return common;
}

} // namespace Slic3r::App::Plater
