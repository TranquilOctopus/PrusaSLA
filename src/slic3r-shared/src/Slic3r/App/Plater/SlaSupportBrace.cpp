#include "Slic3r/App/Plater/SlaSupportBrace.hpp"

namespace Slic3r::App::Plater {

std::optional<SupportBrace> selection_support_brace(
    const Domain::SLA::SupportPoints& points,
    const std::unordered_set<size_t>& selected_point_indices
)
{
    std::optional<SupportBrace> common;
    for (size_t idx : selected_point_indices) {
        if (idx >= points.size()) {
            continue;
        }
        const SupportBrace brace = points[idx].brace;
        if (!common.has_value()) {
            common = brace;
        } else if (*common != brace) {
            // The points of the selection disagree, so there is no value to show.
            return std::nullopt;
        }
    }

    return common;
}

} // namespace Slic3r::App::Plater
