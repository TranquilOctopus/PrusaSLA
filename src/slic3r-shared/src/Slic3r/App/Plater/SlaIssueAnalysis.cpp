#include "Slic3r/App/Plater/SlaIssueAnalysis.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

#include "fmt/format.h"

namespace Slic3r::App::Plater {

SlaIssueAnalysis analyze_sla_issues_for_notification(
    const std::vector<Biz::Slicing::Sla::SlaIssue>& issues)
{
    SlaIssueAnalysis analysis;
    for (const auto& issue : issues) {
        if (issue.kind == Biz::Slicing::Sla::SlaIssue::Kind::Island) {
            analysis.island_count++;
            if (analysis.lowest_layer == 0 || issue.layer < analysis.lowest_layer) {
                analysis.lowest_layer = issue.layer;
            }
        }
    }

    if (analysis.island_count > 0) {
        // TRN: Notification text for islands found during SLA slicing.
        // {0} = number of islands, {1} = first layer index (0-based).
        analysis.message = fmt::format(
            fmt::runtime(_u8L("{0} island{2} found, first on layer {1}. They can fall off during printing.")),
            analysis.island_count,
            analysis.lowest_layer,
            analysis.island_count == 1 ? "" : "s"
        );
    }

    return analysis;
}

} // namespace Slic3r::App::Plater
