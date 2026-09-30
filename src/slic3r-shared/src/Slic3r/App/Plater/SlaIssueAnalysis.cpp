#include "Slic3r/App/Plater/SlaIssueAnalysis.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

#include "fmt/format.h"

#include <set>

using namespace Slic3r::Biz;

namespace Slic3r::App::Plater {

SlaIssueAnalysis analyze_sla_issues_for_notification(
    const std::vector<Biz::Slicing::Sla::SlaIssue>& issues)
{
    SlaIssueAnalysis analysis;
    // The models the islands were found on, so the message can say how many of them are hit.
    std::set<size_t> affected_objects;

    for (const auto& issue : issues) {
        if (issue.kind == Biz::Slicing::Sla::SlaIssue::Kind::Island) {
            // Layer 0 is a valid answer, so count is the "nothing seen yet" marker, not the layer value.
            if (analysis.island_count == 0 || issue.layer < analysis.lowest_layer) {
                analysis.lowest_layer = issue.layer;
            }
            analysis.island_count++;
            if (issue.object_id.valid())
                affected_objects.insert(issue.object_id.id);
        }
    }
    analysis.affected_objects = affected_objects.size();

    if (analysis.island_count > 0) {
        if (analysis.affected_objects == 0) {
            // Nothing could be put on a model, so the message has no model to name.
            // TRN: Notification text for islands found during SLA slicing.
            // {0} = number of islands, {1} = first layer index (0-based).
            analysis.message = fmt::format(
                fmt::runtime(_u8L(
                    "{0} island{2} found, first on layer {1}. They can fall off during printing."
                )),
                analysis.island_count,
                analysis.lowest_layer,
                analysis.island_count == 1 ? "" : "s"
            );
        } else {
            // TRN: Notification text for islands found during SLA slicing, naming the models they
            // were found on. {0} = number of islands, {1} = first layer index (0-based),
            // {2} = plural of island, {3} = number of models, {4} = plural of model.
            analysis.message = fmt::format(
                fmt::runtime(_u8L(
                    "{0} island{2} found on {3} model{4}, first on layer {1}. They can fall off during printing."
                )),
                analysis.island_count,
                analysis.lowest_layer,
                analysis.island_count == 1 ? "" : "s",
                analysis.affected_objects,
                analysis.affected_objects == 1 ? "" : "s"
            );
        }
    }

    return analysis;
}

} // namespace Slic3r::App::Plater
