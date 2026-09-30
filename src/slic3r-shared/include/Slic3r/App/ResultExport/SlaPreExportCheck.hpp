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
 * One line per problem, in the order a user reads them: the models that cannot be printed at all,
 * the islands that can fall off, the resin that cannot drain, the cups that hold a vacuum against
 * the film, and the layers that are hard to peel. The two cavity lines name how many were found,
 * the largest one by volume and the models they were found on; the peel line names how many layers
 * are over the limit, the worst of them and the vat film the estimate was made with. Layer numbers
 * are the 0-based ones the slicer reports, as the earlier lines already were. A list of model
 * names is truncated, a count of the remaining ones is appended.
 *
 * The lines inform, they change nothing: no drain hole is added and the export is not blocked, the
 * dialog that shows them points at the sidebar issues list, where the details and the drain hole
 * suggestion are.
 *
 * @param vat_film The vat film of the slice, named in the peel line. Defaults to the FEP the
 *                 config defaults to, for a call that has no config to read.
 */
Problems format_problems(
    const std::vector<std::string>&                 unsupported_object_names,
    const std::vector<Biz::Slicing::Sla::SlaIssue>& issues,
    const std::string&                              vat_film = "FEP");

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
