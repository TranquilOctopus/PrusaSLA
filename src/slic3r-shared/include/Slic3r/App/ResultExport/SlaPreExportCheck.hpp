#pragma once

#include <string>
#include <vector>

#include "libslic3r/SLAResult.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
}

namespace Slic3r::App::SlaPreExportCheck {

/**
 * @brief Problems of a sliced SLA build plate that waste resin if printed as is.
 *
 * One line per problem, already localized and ready to show to the user.
 */
struct Problems
{
    std::vector<std::string> lines;

    bool empty() const { return lines.empty(); }
    /// The lines joined by newlines, for use as the body of a dialog.
    std::string text() const;
};

/**
 * @brief Build the checklist lines from data already gathered from the slice result.
 *
 * Only islands are reported from @p issues; other issue kinds have no pre-export meaning.
 * Unsupported object names are truncated, a count of the remaining ones is appended.
 */
Problems format_problems(
    const std::vector<std::string>&                 unsupported_object_names,
    const std::vector<Biz::Slicing::Sla::SlaIssue>& issues);

/**
 * @brief Collect the checklist problems of the currently selected build plate.
 *
 * Empty when the plate is not sliced yet or when there is nothing worth warning about.
 */
Problems collect(const Biz::ProjectInteractor& project_interactor);

/**
 * @brief Ask the user whether to export in spite of @p problems.
 *
 * @return true to continue with the export, false when the user cancelled.
 */
bool confirm(const Problems& problems);

} // namespace Slic3r::App::SlaPreExportCheck
