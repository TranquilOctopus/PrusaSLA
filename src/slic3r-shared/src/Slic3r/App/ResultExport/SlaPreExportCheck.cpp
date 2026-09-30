#include "Slic3r/App/ResultExport/SlaPreExportCheck.hpp"

#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/IDialogManager.hpp"
#include "Slic3r/App/Plater/SlaIssueAnalysis.hpp"
#include "Slic3r/App/Plater/SlaUnsupportedObjects.hpp"
#include "Slic3r/App/SlaIssueRows.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/SLAObjectCache.hpp"
#include "Slic3r/Biz/SLAResultCache.hpp"
#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"

#include "libslic3r/SLA/LayerStats.hpp"

#include "fmt/format.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

namespace Slic3r::App::SlaPreExportCheck {

namespace {

using SlaIssue = Biz::Slicing::Sla::SlaIssue;

constexpr size_t max_listed_names = 5;

/// @brief The UTF-8 bytes of U+2026. It is kept out of every translatable string: the translation
/// lookup converts a narrow string through the UI locale, which drops the bytes above 0x7F, so an
/// ellipsis inside a _u8L() string never reaches the output. Spelled as bytes rather than "\u2026"
/// because this project is not compiled with /utf-8, so a "\u2026" in a narrow literal would be
/// encoded in the execution code page instead and arrive as a different character.
constexpr std::string_view ellipsis{"\xE2\x80\xA6"};

/// @brief The UTF-8 bytes of U+00B3, the superscript three of "mm3", see ellipsis above.
constexpr std::string_view cubed{"\xC2\xB3"};

/// @brief What a list of names that was cut short ends with.
std::string more_names_suffix(size_t hidden_count)
{
    // TRN: Pre-export checklist line, listing continuation. {0} counts the models left out.
    // The comma and the ellipsis around it are not translated, see ellipsis above.
    return ", " + std::string(ellipsis) + " "
         + fmt::format(fmt::runtime(Biz::_u8L("and {0} more")), hidden_count);
}

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
        line += more_names_suffix(names.size() - max_listed_names);
    }
    return line;
}

/// @brief The models to name after a line that counts issues rather than models, at most
/// max_listed_names of them, the rest counted. Empty when no issue could be put on a model.
std::string model_list_suffix(const std::vector<std::string>& names)
{
    if (names.empty()) {
        return {};
    }
    std::string suffix = " on";
    for (size_t i = 0; i < names.size() && i < max_listed_names; ++i) {
        suffix += (i == 0 ? " " : ", ");
        suffix += names[i];
    }
    if (names.size() > max_listed_names) {
        suffix += more_names_suffix(names.size() - max_listed_names);
    }
    return suffix;
}

/// @brief What the cups, or the pockets of trapped resin, of one slice add up to: how many, the
/// largest one by the volume of resin it holds, and the models they were found on.
struct CavitySummary
{
    size_t               count            = 0;
    std::optional<double> worst_volume_mm3;
    std::vector<std::string> object_names;
};

void add_cavity_issue(CavitySummary& summary, const SlaIssue& issue)
{
    summary.count++;
    // The volume is read out of the note the slicer wrote for this kind, the same number the
    // sidebar row of the issue shows. A note without one leaves the line without a size.
    const std::optional<double> volume_mm3 = sla_issue_volume_mm3(issue.note);
    if (volume_mm3 && (!summary.worst_volume_mm3 || *volume_mm3 > *summary.worst_volume_mm3)) {
        summary.worst_volume_mm3 = volume_mm3;
    }
    // A model is named once, however many of its cavities there are.
    if (!issue.object_name.empty()
        && std::find(summary.object_names.begin(), summary.object_names.end(), issue.object_name)
               == summary.object_names.end())
    {
        summary.object_names.push_back(issue.object_name);
    }
}

/// @brief The checklist line of one kind of cavity: how many, the largest and the models.
std::string format_cavity_line(const CavitySummary& summary, const std::string& one, const std::string& many)
{
    // TRN: Pre-export checklist line, counting the cavities of one kind. {0} counts them, {1} is
    // the name of the kind in the number that goes with it, so a language can put the count
    // wherever it belongs instead of having a sentence per number.
    std::string line = fmt::format(
        fmt::runtime(Biz::_u8L("{0} {1}")), summary.count, summary.count == 1 ? one : many);
    if (summary.worst_volume_mm3) {
        // TRN: The size of the largest cavity of a kind, appended to the checklist line that
        // counted them. {0} is the volume in mm3 with one decimal. The unit is spelled outside the
        // translatable part, see the ellipsis above.
        line += fmt::format(fmt::runtime(Biz::_u8L(", the largest {0:.1f} mm")), *summary.worst_volume_mm3)
            + std::string(cubed);
    }
    return line + model_list_suffix(summary.object_names);
}

/// @brief What the layers over the peel force limit of one slice add up to.
struct PeelSummary
{
    size_t               count           = 0;
    size_t               lowest_layer    = 0; //< the first of them the print gets to
    std::optional<double> worst_force_n;
    size_t               worst_layer = 0; //< the layer the worst force was found on
};

void add_peel_issue(PeelSummary& summary, const SlaIssue& issue)
{
    // Layer 0 is a valid answer, so the count is the "nothing seen yet" marker, not the layer.
    if (summary.count == 0 || issue.layer < summary.lowest_layer) {
        summary.lowest_layer = issue.layer;
    }
    summary.count++;
    const std::optional<double> force_n = sla_issue_peel_force_n(issue.note);
    if (force_n && (!summary.worst_force_n || *force_n > *summary.worst_force_n)) {
        summary.worst_force_n = force_n;
        summary.worst_layer   = issue.layer;
    }
}

/// @brief The checklist line for the layers that are hard to peel off the film.
std::string format_peel_line(const PeelSummary& summary, const std::string& vat_film)
{
    const std::string plural = summary.count == 1 ? "" : "s";
    if (summary.worst_force_n) {
        // TRN: Pre-export checklist line for the layers the peel force model calls too hard to
        // peel. {0} counts the layers, {1} is the vat film the estimate was made with, {2} is the
        // plural of layer, {3} is the force of the worst layer in N and {4} its number.
        return fmt::format(
            fmt::runtime(Biz::_u8L("High peel force on {0} layer{2} with {1}, worst {3:.1f} N on layer {4}")),
            summary.count,
            vat_film,
            plural,
            *summary.worst_force_n,
            summary.worst_layer
        );
    }
    // TRN: The same line for a slice whose notes carry no force, so the first of the layers is
    // named instead. {0} counts the layers, {1} is the vat film, {2} is the plural of layer and
    // {3} is the number of the first of them.
    return fmt::format(
        fmt::runtime(Biz::_u8L("High peel force on {0} layer{2} with {1}, the first on layer {3}")),
        summary.count,
        vat_film,
        plural,
        summary.lowest_layer
    );
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
    const std::vector<Biz::Slicing::Sla::SlaIssue>& issues,
    const std::string&                              vat_film)
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

    // The resin that cannot drain and the cups that hold a vacuum are counted here as well. The
    // checklist cannot add the drain hole a cup wants, so the line only says what was found and
    // the dialog points at the sidebar issues list, where the suggestion is.
    CavitySummary trapped_resin;
    CavitySummary cups;
    PeelSummary    peel;
    for (const SlaIssue& issue : issues) {
        switch (issue.kind) {
        case SlaIssue::Kind::TrappedResin:
            add_cavity_issue(trapped_resin, issue);
            break;
        case SlaIssue::Kind::Cup:
            add_cavity_issue(cups, issue);
            break;
        case SlaIssue::Kind::HighPeelForce:
            add_peel_issue(peel, issue);
            break;
        default:
            // Islands are counted by the analysis above, the kinds without a pre-export meaning
            // are left out, like they are not listed in the sidebar either.
            break;
        }
    }
    if (trapped_resin.count > 0) {
        problems.lines.push_back(format_cavity_line(
            trapped_resin,
            // TRN: One pocket of resin that cannot drain out of the print, in a checklist line
            // that counted them.
            Biz::_u8L("trapped resin pocket"),
            // TRN: The same pocket, counted with more than one of them.
            Biz::_u8L("trapped resin pockets")
        ));
    }
    if (cups.count > 0) {
        problems.lines.push_back(format_cavity_line(
            cups,
            // TRN: One cup, a hole in a layer that holds a vacuum against the film, in a checklist
            // line that counted them.
            Biz::_u8L("cup"),
            // TRN: The same cup, counted with more than one of them.
            Biz::_u8L("cups")
        ));
    }
    if (peel.count > 0) {
        problems.lines.push_back(format_peel_line(peel, vat_film));
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

    return format_problems(
        unsupported_names,
        sla_result->get().export_data->issues,
        // The film the peel force estimate was made with, so the line that warns about the hard
        // layers says which film it is about. The spelling comes with the coefficients.
        std::string(::Slic3r::SLA::vat_film_name(
            sla_result->get().export_data->config.get<Domain::sla::VatFilmType>("vat_film_type")
        ))
    );
}

bool confirm(const Problems& problems)
{
    if (problems.empty()) {
        return true;
    }
    bool proceed = false;
    AppServices::instance().dialog_manager().show_labeled_yesno_dialog(
        Biz::_u8L("Check before printing"),
        problems.text() + "\n\n"
            + Biz::_u8L("Printing this as is is likely to waste resin. Fix the problems, or export anyway.")
            + "\n"
            + Biz::_u8L("The issues list in the sidebar has all of them, and can add a drain hole to a cup or a pocket of trapped resin."),
        Biz::_u8L("Export anyway"),
        Biz::_u8L("Cancel"),
        [&proceed](bool yes) { proceed = yes; }
    );
    return proceed;
}

} // namespace Slic3r::App::SlaPreExportCheck
