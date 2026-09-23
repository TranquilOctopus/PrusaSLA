#include "Slic3r/App/SidebarSlaSummary.hpp"

#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/ResinEconomicsInteractor.hpp"
#include "Slic3r/Biz/SLAResultCache.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/Slicing/SlicingInteractor.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Domain/ConfigContainer.hpp"
#include "Slic3r/Domain/FindById.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/Domain/SlicingId.hpp"
#include "Slic3r/Domain/PrinterTechnology.hpp"

#include <iomanip>
#include <sstream>

using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;
using namespace Slic3r::Domain;

namespace Slic3r::App {

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
        return;
    }

    // Compute resin economics for the selected bed
    ResinEconomicsInteractor economics_interactor(m_project_interactor);
    const BedResinEconomics bed_economics = economics_interactor.compute_bed_economics(project_id, bed_instance_id);

    // Get layer count from SLA result cache
    std::optional<size_t> layer_count;
    if (bed_economics.has_result) {
        const SLAResultCache& sla_cache = m_project_interactor.sla_result_cache();
        const SlicingId slicing_id{project_id, bed_instance_id};
        const std::optional<SLAResultRef> sla_result_opt = sla_cache.get_result(slicing_id);
        if (sla_result_opt) {
            const Slicing::SLAResult& sla_result = sla_result_opt.value().get();
            if (sla_result.export_data && sla_result.export_data->files.data.size() > 0) {
                layer_count = sla_result.export_data->files.data.size();
            }
        }
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

} // namespace SidebarSlaSummaryFormat

} // namespace Slic3r::App