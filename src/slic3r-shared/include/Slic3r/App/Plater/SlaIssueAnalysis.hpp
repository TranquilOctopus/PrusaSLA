#pragma once

#include <string>
#include <vector>
#include "libslic3r/SLAResult.hpp"

namespace Slic3r::App::Plater {

/**
 * @brief Result of analyzing SLA issues for island notification.
 */
struct SlaIssueAnalysis
{
    size_t island_count = 0;
    size_t lowest_layer = 0;
    std::string message;
};

/**
 * @brief Analyze SLA issues and extract island information for user notification.
 * @param issues Vector of SLA issues from slicing result.
 * @return Analysis containing island count, lowest layer index, and formatted message.
 */
SlaIssueAnalysis analyze_sla_issues_for_notification(
    const std::vector<Biz::Slicing::Sla::SlaIssue>& issues);

} // namespace Slic3r::App::Plater
