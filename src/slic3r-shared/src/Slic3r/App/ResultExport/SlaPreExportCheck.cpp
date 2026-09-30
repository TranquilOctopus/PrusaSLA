#include "Slic3r/App/ResultExport/SlaPreExportCheck.hpp"

#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/IDialogManager.hpp"
#include "Slic3r/App/Plater/SlaIssueAnalysis.hpp"
#include "Slic3r/App/Plater/SlaUnsupportedObjects.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/SLAObjectCache.hpp"
#include "Slic3r/Biz/SLAResultCache.hpp"

#include "fmt/format.h"

#include <optional>

namespace Slic3r::App::SlaPreExportCheck {

namespace {

constexpr size_t max_listed_names = 5;

std::string format_unsupported_names(const std::vector<std::string>& names)
{
    // TRN: Pre-export checklist line for exactly one model.
    // "has" turns into "have" with the count, so the two forms are separate strings instead of one
    // string with a plural suffix.
    std::string line = names.size() == 1 ?
        Biz::_u8L("1 model has no supports:") :
        fmt::format(fmt::runtime(Biz::_u8L("{0} models have no supports:")), names.size());
    for (size_t i = 0; i < names.size() && i < max_listed_names; ++i) {
        line += (i == 0 ? " " : ", ");
        line += names[i];
    }
    if (names.size() > max_listed_names) {
        // TRN: Pre-export checklist line, listing continuation. {0} counts the models left out.
        // The ellipsis is the raw UTF-8 bytes: this project is not compiled with /utf-8, so a "\u2026"
        // would be encoded in the execution code page instead and arrive as a different character.
        // The literal is split after the last escape, so a hex digit following it cannot be swallowed.
        line += fmt::format(
            fmt::runtime(Biz::_u8L(", \xE2\x80\xA6" " and {0} more")),
            names.size() - max_listed_names
        );
    }
    return line;
}

} // namespace

std::string Problems::text() const
{
    std::string out;
    for (const std::string& line : lines) {
        if (!out.empty()) {
            out += "\n";
        }
        out += line;
    }
    return out;
}

Problems format_problems(
    const std::vector<std::string>&                 unsupported_object_names,
    const std::vector<Biz::Slicing::Sla::SlaIssue>& issues)
{
    Problems problems;
    if (!unsupported_object_names.empty()) {
        problems.lines.push_back(format_unsupported_names(unsupported_object_names));
    }
    const Plater::SlaIssueAnalysis analysis = Plater::analyze_sla_issues_for_notification(issues);
    if (analysis.island_count > 0) {
        // TRN: Pre-export checklist line. {0} counts islands, {1} is the 0-based layer of the first
        // one, {2} is the plural suffix.
        problems.lines.push_back(fmt::format(
            fmt::runtime(Biz::_u8L("{0} island{2}, first on layer {1}")),
            analysis.island_count,
            analysis.lowest_layer,
            analysis.island_count == 1 ? "" : "s"
        ));
    }
    return problems;
}

Problems collect(const Biz::ProjectInteractor& project_interactor)
{
    const Domain::SlicingId                slicing_id{ project_interactor.selected_bed_slicing_id() };
    const std::optional<Biz::SLAResultRef> sla_result{ project_interactor.sla_result_cache().get_result(slicing_id) };
    if (!sla_result || !sla_result->get().export_data) {
        // Not sliced (anymore), so there is nothing the checklist could say about it.
        return {};
    }

    std::vector<std::string> unsupported_names;
    if (project_interactor.project_exists(slicing_id.project_id)) {
        const Domain::Project& project = project_interactor.project(slicing_id.project_id);
        if (const Domain::BedInstance* bed_instance = project.find_bed_instance_by_id(slicing_id.bed_instance_id)) {
            for (const Domain::ModelObject* object : Plater::collect_unsupported_objects(
                     project_interactor.sla_object_cache(), slicing_id, *bed_instance, project)) {
                unsupported_names.push_back(object->name);
            }
        }
    }

    return format_problems(unsupported_names, sla_result->get().export_data->issues);
}

bool confirm(const Problems& problems)
{
    if (problems.empty()) {
        return true;
    }
    bool proceed = false;
    AppServices::instance().dialog_manager().show_labeled_yesno_dialog(
        Biz::_u8L("Check before printing"),
        problems.text() + "\n\n" + Biz::_u8L("Printing this as is is likely to waste resin. Fix the problems, or export anyway."),
        Biz::_u8L("Export anyway"),
        Biz::_u8L("Cancel"),
        [&proceed](bool yes) { proceed = yes; }
    );
    return proceed;
}

} // namespace Slic3r::App::SlaPreExportCheck
