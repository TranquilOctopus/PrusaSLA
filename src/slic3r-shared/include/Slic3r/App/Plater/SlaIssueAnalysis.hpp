#pragma once

#include <string>
#include <vector>
#include "libslic3r/SLAResult.hpp"

namespace Slic3r::App::Plater {

/**
 * @brief Result of analyzing SLA issues for the post-slice notification.
 *
 * One count and one worst layer per kind: the lowest layer an issue of that kind starts on, which
 * is the first of them the print gets to. A cup and a pocket of trapped resin are told apart from
 * the islands, they need a different thing done about them. affected_objects counts the models the
 * islands were attributed to, which the message names when there is more than the layer to say.
 */
struct SlaIssueAnalysis
{
    size_t island_count               = 0;
    size_t lowest_layer               = 0;
    size_t cup_count                  = 0;
    size_t cup_lowest_layer           = 0;
    size_t trapped_resin_count        = 0;
    size_t trapped_resin_lowest_layer = 0;
    /// How many models the islands were found on, 0 when none of them could be attributed.
    size_t affected_objects = 0;
    std::string message;

    /// Nothing was found, so there is no notification to show.
    bool empty() const
    {
        return message.empty();
    }
};

/**
 * @brief Analyze SLA issues and extract the island, cup and trapped resin information for the
 *        post-slice notification.
 * @param issues Vector of SLA issues from slicing result.
 * @return Analysis containing the count and the worst layer of every kind, and a message naming
 *         the kinds that were found. Empty message when there is nothing to report.
 */
SlaIssueAnalysis
analyze_sla_issues_for_notification(const std::vector<Biz::Slicing::Sla::SlaIssue>& issues);

} // namespace Slic3r::App::Plater
