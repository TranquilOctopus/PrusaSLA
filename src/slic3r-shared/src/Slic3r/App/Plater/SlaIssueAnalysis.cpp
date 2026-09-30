#include "Slic3r/App/Plater/SlaIssueAnalysis.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

#include "fmt/format.h"

#include <set>

using namespace Slic3r::Biz;

namespace Slic3r::App::Plater {

namespace {

using SlaIssue = Biz::Slicing::Sla::SlaIssue;

/// One half of the notification, naming one kind of issue: how many were found and where the
/// first of them is. The name goes in as an argument, so a language can name the kind in its own
/// case rather than have two sentences that only differ in a suffix.
std::string sentence(size_t count, size_t lowest_layer, const char* one, const char* many)
{
    // TRN: Part of the post-slice notification, naming one kind of issue. {0} counts the issues of
    // that kind, {1} is the layer the first of them was found on, {2} is the name of the kind.
    return fmt::format(
        fmt::runtime(_u8L("{0} {2} found, first on layer {1}")),
        count,
        lowest_layer,
        count == 1 ? one : many
    );
}

} // namespace

SlaIssueAnalysis
analyze_sla_issues_for_notification(const std::vector<Biz::Slicing::Sla::SlaIssue>& issues)
{
    SlaIssueAnalysis analysis;
    // The models the islands were found on, so the message can say how many of them are hit.
    std::set<size_t> affected_objects;

    for (const auto& issue : issues) {
        switch (issue.kind) {
        case SlaIssue::Kind::Island:
            // Layer 0 is a valid answer, so count is the "nothing seen yet" marker, not the layer value.
            if (analysis.island_count == 0 || issue.layer < analysis.lowest_layer) {
                analysis.lowest_layer = issue.layer;
            }
            analysis.island_count++;
            if (issue.object_id.valid()) {
                affected_objects.insert(issue.object_id.id);
            }
            break;
        case SlaIssue::Kind::Cup:
            if (analysis.cup_count == 0 || issue.layer < analysis.cup_lowest_layer) {
                analysis.cup_lowest_layer = issue.layer;
            }
            analysis.cup_count++;
            break;
        case SlaIssue::Kind::TrappedResin:
            if (analysis.trapped_resin_count == 0
                || issue.layer < analysis.trapped_resin_lowest_layer)
            {
                analysis.trapped_resin_lowest_layer = issue.layer;
            }
            analysis.trapped_resin_count++;
            break;
        default:
            break;
        }
    }
    analysis.affected_objects = affected_objects.size();

    // The kinds that need something done about them, in the order of how bad they are for the
    // print: an island falls off, a cup holds the whole layer against the film, resin that cannot
    // leave is only found when the print is already in the bottle.
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
    if (analysis.cup_count > 0) {
        if (!analysis.message.empty()) {
            analysis.message += " ";
        }
        // TRN: Post-slice notification, what the cups of a slice do to the print.
        analysis.message += sentence(analysis.cup_count, analysis.cup_lowest_layer, "cup", "cups")
            + ". They can hold a vacuum against the film on every peel.";
    }
    if (analysis.trapped_resin_count > 0) {
        if (!analysis.message.empty()) {
            analysis.message += " ";
        }
        // TRN: Post-slice notification, what the pockets of trapped resin of a slice mean.
        analysis.message +=
            sentence(
                analysis.trapped_resin_count,
                analysis.trapped_resin_lowest_layer,
                "trapped resin pocket",
                "trapped resin pockets"
            )
            + ". The resin in it cannot drain.";
    }

    return analysis;
}

} // namespace Slic3r::App::Plater
