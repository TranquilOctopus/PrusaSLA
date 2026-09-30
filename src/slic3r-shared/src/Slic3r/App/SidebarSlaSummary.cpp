#include "Slic3r/App/SidebarSlaSummary.hpp"

#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/IDialogManager.hpp"
#include "Slic3r/App/Theme.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/ResinEconomicsInteractor.hpp"
#include "Slic3r/Biz/SLAObjectCache.hpp"
#include "Slic3r/Biz/SLAResultCache.hpp"
#include "Slic3r/Biz/Sla/DrainHoleSuggestion.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/IUndoProvider.hpp"
#include "Slic3r/Biz/Slicing/SlicingInteractor.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/Domain/ConfigContainer.hpp"
#include "Slic3r/Domain/ElementRef.hpp"
#include "Slic3r/Domain/FindById.hpp"
#include "Slic3r/Domain/ModelInstance.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/Domain/SLA/DrainHole.hpp"
#include "Slic3r/Domain/SlicingId.hpp"
#include "Slic3r/Domain/PrinterTechnology.hpp"

#include "libslic3r/SLAResult.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>
#include <vector>

using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;
using namespace Slic3r::Domain;

namespace Slic3r::App {

namespace {

/// The models of the bed a drain hole can be suggested on. The candidates borrow the meshes, so
/// the meshes are kept here and reserved once, before the candidates take pointers into them.
struct DrainHoleCandidates
{
    std::vector<Domain::TriangleMesh>                 meshes;
    std::vector<Slic3r::Biz::Sla::DrainHoleCandidate> candidates;
};

/// The models of a bed the printer prints, each of them named once, in the order they stand on the
/// plate. Several instances of one model are one row, the way the slicer reports them.
std::vector<std::string> model_names_of_bed(const Domain::Project& project, Domain::SelectionId bed_instance_id)
{
    std::vector<std::string> names;
    const Domain::BedInstance* bed_instance = project.find_bed_instance_by_id(bed_instance_id);
    if (bed_instance == nullptr) {
        return names;
    }

    std::vector<Domain::ObjectID> seen;
    for (const Domain::ModelInstance* instance : bed_instance->model_instances) {
        if (instance == nullptr || !instance->is_printable()) {
            continue;
        }
        const Domain::ModelObject* model_object = instance->get_object();
        if (model_object == nullptr
            || std::find(seen.begin(), seen.end(), model_object->id()) != seen.end()) {
            continue;
        }
        seen.push_back(model_object->id());
        names.push_back(model_object->name);
    }

    return names;
}

} // namespace

SidebarSlaSummary::SidebarSlaSummary(Biz::ProjectInteractor& project_interactor) :
    Window("SidebarSlaSummary"),
    m_config_container_listener_scope(project_interactor, *this),
    m_project_listener_scope(project_interactor, *this),
    m_bed_selection_listener_scope(project_interactor.scene_interactor(), *this),
    m_status_listener_scope(project_interactor.slicing_interactor(), *this),
    m_project_interactor(project_interactor)
{
    set_orientation(Orientation::Vertical);
    set_gap(5_fpx);
    set_visible(false);

    // Create a container for the rows
    m_rows_container = emplace_back<Item>();
    m_rows_container->set_orientation(Orientation::Vertical);
    m_rows_container->set_gap(3_fpx);

    // Initial refresh
    refresh();
}

void SidebarSlaSummary::on_selected_config_container_changed(Domain::SelectionId project_id, Domain::SelectionId container_id)
{
    if (m_project_interactor.selected_project_id() == project_id) {
        m_current_config_container_id = container_id;
        m_current_project_id = project_id;
        refresh();
    }
}

void SidebarSlaSummary::on_selected_project_changed(size_t index)
{
    m_current_project_id = index;
    m_current_config_container_id = m_project_interactor.selected_config_container_id();
    refresh();
}

void SidebarSlaSummary::on_selected_project_changed_final(size_t /*index*/)
{
    // No additional action needed
}

void SidebarSlaSummary::on_selected_bed_instances_changed(Domain::SelectionId project_id, const Biz::Scene::BedSelection& /*bed_selection*/)
{
    if (m_project_interactor.selected_project_id() == project_id) {
        refresh();
    }
}

void SidebarSlaSummary::on_status_changed(const Biz::Slicing::StatusUpdate, const Domain::SlicingId slicing_id)
{
    // Only refresh if this status change is for the currently selected bed
    if (m_current_project_id != Domain::INVALID_ID
        && m_current_bed_instance_id != Domain::INVALID_ID
        && slicing_id.project_id == m_current_project_id
        && slicing_id.bed_instance_id == m_current_bed_instance_id)
    {
        // Check if slicing finished
        const auto status = m_project_interactor.slicing_interactor().get_status(slicing_id);
        if (status == Biz::Slicing::StatusCode::Finished) {
            refresh();
        }
    }
}

void SidebarSlaSummary::refresh()
{
    clear_rows();

    // Get current selection
    const auto project_id = m_project_interactor.selected_project_id();
    const auto config_container_id = m_project_interactor.selected_config_container_id();
    const auto& bed_selection = m_project_interactor.scene_interactor().bed_selection();
    const auto bed_ref = bed_selection.last_selected_bed();
    const auto bed_instance_id = bed_ref.instance_id;

    m_current_project_id = project_id;
    m_current_config_container_id = config_container_id;
    m_current_bed_instance_id = bed_instance_id;
    m_current_slicing_id          = SlicingId{project_id, bed_instance_id};

    update_visibility();

    if (!is_visible()) {
        return;
    }

    // If no bed is selected, show dashes
    if (bed_instance_id == Domain::INVALID_ID || project_id == Domain::INVALID_ID) {
        auto add_row = [this](const std::string& label, const std::string& value) {
            Item* row = m_rows_container->emplace_back<Item>();
            row->set_orientation(Orientation::Horizontal);
            row->set_justify_content(YGJustifySpaceBetween);
            row->set_gap(10_fpx);

            Text* label_text = row->emplace_back<Text>(label);
            label_text->set_font_type(Render::ImguiFontType::Regular);

            Text* value_text = row->emplace_back<Text>(value);
            value_text->set_font_type(Render::ImguiFontType::Regular);
            value_text->set_text_color(m_theme->color_imgui(Platform::Color::Text));
        };

        add_row(_u8L("Resin"), "—");
        add_row(_u8L("Cost"), "—");
        add_row(_u8L("Layers"), "—");
        add_row(_u8L("Print time"), "—");
        return;
    }

    // Compute resin economics for the selected bed
    ResinEconomicsInteractor economics_interactor(m_project_interactor);
    const BedResinEconomics bed_economics = economics_interactor.compute_bed_economics(project_id, bed_instance_id);

    // Get layer count, print time and issues from SLA result cache
    std::optional<size_t> layer_count;
    std::optional<double> print_time_s;
    SlaIssueRows issue_rows;
    SlaObjectUseRows object_use_rows;
    if (bed_economics.has_result) {
        const SLAResultCache& sla_cache = m_project_interactor.sla_result_cache();
        const SlicingId slicing_id{project_id, bed_instance_id};
        const std::optional<SLAResultRef> sla_result_opt = sla_cache.get_result(slicing_id);
        if (sla_result_opt) {
            const Slicing::SLAResult& sla_result = sla_result_opt.value().get();
            if (sla_result.export_data) {
                if (sla_result.export_data->files.data.size() > 0) {
                    layer_count = sla_result.export_data->files.data.size();
                }
                // The estimate the engine made for a printer without a tilt (M1.11c); it is empty
                // for a print that has not been sliced yet, which is the en dash case.
                print_time_s = sla_result.export_data->print_time_s;
                issue_rows   = build_sla_issue_rows(sla_result.export_data->issues);
                // What every model of the plate cures on its own (M1.11d).
                object_use_rows = build_sla_object_use_rows(sla_result.export_data->object_resin_use,
                                                            sla_result.export_data->config);
            }
        }
    }

    // Before the first slice there is no per model resin to show, but the models of the bed are
    // known and the table then reads as the en dash it is, the same one the figures above carry.
    if (object_use_rows.empty()) {
        object_use_rows = unsliced_sla_object_use_rows(
            model_names_of_bed(m_project_interactor.selected_project(), bed_instance_id));
    }

    // Build rows
    auto add_row = [this](const std::string& label, const std::string& value) {
        Item* row = m_rows_container->emplace_back<Item>();
        row->set_orientation(Orientation::Horizontal);
        row->set_justify_content(YGJustifySpaceBetween);
        row->set_gap(10_fpx);

        Text* label_text = row->emplace_back<Text>(label);
        label_text->set_font_type(Render::ImguiFontType::Regular);

        Text* value_text = row->emplace_back<Text>(value);
        value_text->set_font_type(Render::ImguiFontType::Regular);
        value_text->set_text_color(m_theme->color_imgui(Platform::Color::Text));
    };

    // Resin row
    std::string resin_label = _u8L("Resin");
    std::string resin_value = SidebarSlaSummaryFormat::format_resin_ml(bed_economics.economics.millilitres);
    add_row(resin_label, resin_value);

    // Cost row
    std::string cost_label = _u8L("Cost");
    std::string cost_value = SidebarSlaSummaryFormat::format_cost(bed_economics.economics.cost);
    std::string bottles_value = SidebarSlaSummaryFormat::format_bottles(bed_economics.economics.bottles_fraction);
    if (bed_economics.economics.cost.has_value() || bed_economics.economics.bottles_fraction.has_value()) {
        if (!cost_value.empty() && cost_value != "—" && !bottles_value.empty() && bottles_value != "—") {
            cost_value += "          bottles " + bottles_value;
        } else if (!bottles_value.empty() && bottles_value != "—") {
            cost_value = "          bottles " + bottles_value;
        }
    }
    add_row(cost_label, cost_value);

    // Layers row
    std::string layers_label = _u8L("Layers");
    std::string layers_value = SidebarSlaSummaryFormat::format_layers(layer_count);
    add_row(layers_label, layers_value);

    // Estimated print time row, beside the layer count it is summed from (M1.11c).
    add_row(_u8L("Print time"), SidebarSlaSummaryFormat::format_print_time(print_time_s));

    add_object_use_rows(object_use_rows);

    add_issue_rows(issue_rows);
}

void SidebarSlaSummary::add_object_use_rows(const SlaObjectUseRows& object_use_rows)
{
    // One model on the plate says nothing the figures above do not, and an empty plate has no table
    // at all. The whole window is hidden for FFF, which never gets here with rows.
    if (!object_use_rows.show()) {
        return;
    }

    m_rows_container->emplace_back<Separator>();

    LayoutButton* toggle = m_rows_container->emplace_back<LayoutButton>(object_use_rows.title());
    toggle->set_checkable(true);
    toggle->set_checked(m_object_use_expanded);
    toggle->set_content_justify_content(YGJustifyFlexStart);
    toggle->set_content_padding({4.f, 2.f});
    toggle->set_flex_shrink(0_fpx);
    toggle->set_tooltip(_u8L("Show what every model of the plate cures on its own"));

    Item* rows = m_rows_container->emplace_back<Item>();
    rows->set_orientation(Orientation::Vertical);
    rows->set_gap(3_fpx);
    rows->set_visible(m_object_use_expanded);

    toggle->callbacks().checked_changed = [this, rows](bool checked)
    {
        m_object_use_expanded = checked;
        rows->set_visible(checked);
    };

    for (const SlaObjectUseRow& row : object_use_rows.rows) {
        Item* line = rows->emplace_back<Item>();
        line->set_orientation(Orientation::Horizontal);
        line->set_justify_content(YGJustifySpaceBetween);
        line->set_gap(10_fpx);
        line->set_flex_shrink(0_fpx);

        Text* name = line->emplace_back<Text>(row.object_name);
        name->set_font_type(Render::ImguiFontType::Regular);

        Text* value = line->emplace_back<Text>(sla_object_use_value_text(row));
        value->set_font_type(Render::ImguiFontType::Regular);
        value->set_text_color(m_theme->color_imgui(Platform::Color::Text));
    }
}

void SidebarSlaSummary::add_issue_rows(const SlaIssueRows& issue_rows)
{
    // Nothing found, or the printer is not an SLA one, in which case the whole window is hidden.
    if (issue_rows.empty()) {
        return;
    }

    m_rows_container->emplace_back<Separator>();

    Text* title = m_rows_container->emplace_back<Text>(issue_rows.title());
    title->set_font_type(Render::ImguiFontType::Bold);
    title->set_flex_shrink(0_fpx);

    for (SlaIssueRow issue_row : issue_rows.rows) {
        // A cavity is the one kind of issue a hole in the model can answer, so its row carries a
        // button that adds the suggested one next to the link into the layer image window.
        const bool is_cavity = Slic3r::Biz::Sla::accepts_drain_hole_suggestion(issue_row.kind);

        // The issues of a slice are found on the merged layers of the bed, so a cavity names no
        // model. The search that places the hole finds the one it lands on anyway, so the row runs
        // it too and names that model the way an island row names its own (M4.8g).
        if (is_cavity && issue_row.object_name.empty()) {
            const std::optional<Slic3r::Biz::Sla::DrainHoleSuggestion> suggestion =
                suggest_drain_hole(issue_row);
            if (suggestion.has_value()) {
                const Domain::ModelObject* owner =
                    m_project_interactor.selected_project().find_object_by_id(suggestion->object_id.id);
                if (owner != nullptr) {
                    issue_row.object_name = owner->name;
                }
            }
        }

        Item* line = m_rows_container->emplace_back<Item>();
        line->set_orientation(Orientation::Horizontal);
        line->set_align_items(YGAlignCenter);
        line->set_gap(4_fpx);
        line->set_flex_shrink(0_fpx);

        // A cavity of trapped resin is the same kind of problem as a cup and takes the same answer,
        // so both wear the token of the cup warning.
        const ImColor color = m_theme->color_imgui(
            is_cavity ? Platform::Color::SlaCupWarning : Platform::Color::SlaIslandWarning
        );

        LayoutButton* row = line->emplace_back<
            LayoutButton>(sla_issue_row_text(issue_row), Render::Icon::WarningMarker);
        row->set_label_color(color);
        row->set_icon_tint(color);
        // A link, not a panel button: no box around it and the label to the left.
        row->set_background_color(Platform::Color::ButtonTransparent);
        row->set_content_justify_content(YGJustifyFlexStart);
        row->set_content_padding({4.f, 2.f});
        row->set_flex_grow(1_fpx);
        row->set_tooltip(_u8L("Show this layer in the layer image window"));

        const size_t layer      = issue_row.layer;
        row->callbacks().action = [this, layer]()
        {
            // The layer image window lives in another render module, so the request goes through
            // the app wide channel and is picked up there.
            AppServices::instance().sla_layer_jump().request(m_current_slicing_id, layer);
        };

        if (!is_cavity) {
            continue;
        }

        LayoutButton* add_hole = line->emplace_back<LayoutButton>(_u8L("Add drain hole"));
        add_hole->set_tooltip(_u8L("Add a drain hole where this issue suggests one. "
                                   "Slice again to see it in the print."));
        add_hole->set_flex_shrink(0_fpx);
        add_hole->callbacks().action = [this, issue_row]() { this->add_suggested_drain_hole(issue_row); };
    }

    if (const std::string more_text = issue_rows.more_text(); !more_text.empty()) {
        Text* more = m_rows_container->emplace_back<Text>(more_text);
        more->set_flex_shrink(0_fpx);
    }
}

std::optional<Slic3r::Biz::Sla::DrainHoleSuggestion> SidebarSlaSummary::suggest_drain_hole(
    const SlaIssueRow& issue_row) const
{
    const Domain::Project& project = m_project_interactor.selected_project();
    const Domain::BedInstance* bed_instance = project.find_bed_instance_by_id(m_current_bed_instance_id);
    if (bed_instance == nullptr) {
        return std::nullopt;
    }

    // The issues of a slice are found on the merged layers of the whole bed, so none of them names
    // the model the cavity is in. The models of the bed are the candidates and the one whose
    // surface lies nearest to the cavity along its axis is the one that owns it.
    //
    // The candidates borrow their mesh, so the meshes are kept alive here for the whole search and
    // are reserved once, so that the addresses the candidates hold stay valid.
    DrainHoleCandidates collected;
    collected.meshes.reserve(bed_instance->model_instances.size());
    collected.candidates.reserve(bed_instance->model_instances.size());
    const Biz::SLAObjectCache& sla_object_cache = m_project_interactor.sla_object_cache();
    for (const Domain::ModelInstance* instance : bed_instance->model_instances) {
        if (instance == nullptr || !instance->is_printable()) {
            continue;
        }
        const Domain::ModelObject* model_object = instance->get_object();
        if (model_object == nullptr) {
            continue;
        }
        // The transformation of the instance in the frame of the sliced layers, which is the one
        // the slice used. Without it there is no way to tell where the model was when the cavity
        // was found.
        const Biz::SLAObjectOptRef sla_object =
            sla_object_cache.get_instance(Biz::SLAObjectCache::Key{m_current_slicing_id, model_object->id()});
        if (!sla_object.has_value()) {
            continue;
        }
        const Biz::Slicing::Sla::Object& sliced_object = sla_object->get();
        const auto instance_trafo                     = std::find_if(
            sliced_object.instance_trafos.begin(),
            sliced_object.instance_trafos.end(),
            [&instance](const auto& entry) { return entry.first == instance->id(); }
        );
        if (instance_trafo == sliced_object.instance_trafos.end()) {
            continue;
        }
        collected.meshes.emplace_back(model_object->raw_mesh());
        collected.candidates.push_back(Slic3r::Biz::Sla::DrainHoleCandidate{
            model_object->id(), &collected.meshes.back(), sliced_object.object_trafo * (instance_trafo->second)});
    }

    return Slic3r::Biz::Sla::suggest_nearest_drain_hole(collected.candidates, issue_row.kind, issue_row.position);
}

void SidebarSlaSummary::add_suggested_drain_hole(const SlaIssueRow& issue_row)
{
    const std::optional<Slic3r::Biz::Sla::DrainHoleSuggestion> suggestion = suggest_drain_hole(issue_row);
    if (!suggestion.has_value()) {
        AppServices::instance().dialog_manager().show_warning_dialog(
            _u8L("No surface of a model on the plate lies inside this issue, so no drain hole "
                 "could be placed. The model may have been moved since it was sliced."),
            _u8L("Warning")
        );
        return;
    }

    const Domain::SLA::DrainHole hole = suggestion->to_drain_hole();
    m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::SlaDrainHolesApply);
    m_project_interactor.scene_interactor().modify_sla_drain_holes(
        Domain::ElementRef{suggestion->object_id.id, 0},
        [hole](Domain::ModelObject& model_object) { model_object.sla_drain_holes.emplace_back(hole); }
    );
}

void SidebarSlaSummary::update_visibility()
{
    bool is_sla = false;
    if (m_current_config_container_id != Domain::INVALID_ID
        && m_project_interactor.project_exists(m_current_project_id))
    {
        const ConfigContainer& config_container = m_project_interactor.selected_config_container();
        is_sla = config_container.print_technology() == PrinterTechnology::SLA;
    }
    set_visible(is_sla);
}

void SidebarSlaSummary::clear_rows()
{
    if (m_rows_container) {
        // Remove all children
        auto items = m_rows_container->items();
        for (Item* item : items) {
            m_rows_container->remove_later(item);
        }
    }
}

namespace SidebarSlaSummaryFormat {

std::string format_resin_ml(std::optional<double> ml)
{
    if (!ml.has_value()) {
        return "—";
    }
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << *ml << " ml";
    return ss.str();
}

std::string format_cost(std::optional<double> cost)
{
    if (!cost.has_value()) {
        return "—";
    }
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << *cost;
    return ss.str();
}

std::string format_bottles(std::optional<double> bottles)
{
    if (!bottles.has_value()) {
        return "—";
    }
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << *bottles;
    return ss.str();
}

std::string format_layers(std::optional<size_t> layers)
{
    if (!layers.has_value()) {
        return "—";
    }
    return std::to_string(*layers);
}

std::string format_print_time(std::optional<double> seconds)
{
    if (!seconds.has_value()) {
        return "—";
    }
    // A negative or non-finite estimate is not a time: it is the "no estimate" state, which the
    // engine marks by leaving the value out, so a stale one shows as the dash and not as 0:00.
    if (!std::isfinite(*seconds) || *seconds < 0.) {
        return "—";
    }
    const double whole_minutes = std::floor(*seconds / 60.);
    std::ostringstream ss;
    ss << static_cast<long long>(whole_minutes / 60.) << ':' << std::setfill('0') << std::setw(2)
       << static_cast<long long>(whole_minutes) % 60;
    return ss.str();
}

} // namespace SidebarSlaSummaryFormat

} // namespace Slic3r::App