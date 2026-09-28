#include "Slic3r/App/Preview/SidebarSlaSupports.hpp"

#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Navigator.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/App/IsSlaActive.hpp"

using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;
using namespace Slic3r::Domain;

namespace Slic3r::App::Preview {

SidebarSlaSupports::SidebarSlaSupports(ProjectInteractor& project_interactor, App::Navigator* navigator) :
    Window("SidebarSlaSupports"),
    m_navigator(navigator),
    m_config_container_listener_scope(project_interactor, *this),
    m_project_listener_scope(project_interactor, *this),
    m_bed_selection_listener_scope(project_interactor.scene_interactor(), *this),
    m_project_interactor(project_interactor)
{
    set_orientation(Orientation::Vertical);
    set_gap(5_fpx);
    set_visible(false);

    // Title
    Text* title = emplace_back<Text>(_u8L("Supports"));
    title->set_font_type(Render::ImguiFontType::Bold);
    title->set_margin({ 0.f, 0.f, 0.f, 5.f });

    // Create a container for the rows
    m_rows_container = emplace_back<Item>();
    m_rows_container->set_orientation(Orientation::Vertical);
    m_rows_container->set_gap(3_fpx);

    // Edit supports button
    m_edit_supports_button = emplace_back<LayoutButton>(
        _u8L("Edit supports"),
        Render::Icon::None,
        _u8L("Switch to Prepare view with SLA Support Points tool")
    );
    m_edit_supports_button->set_margin({ 0.f, 10.f, 0.f, 0.f });
    m_edit_supports_button->callbacks().action = [this]()
    {
        // Switch to Prepare (Plater) module
        if (m_project_interactor.selected_project_id() != Domain::INVALID_ID && m_navigator) {
            m_navigator->navigate_to_module_type(Render::ModuleType::Plater);
            // Note: Activating the SLA Support Points tool on the selected model
            // requires access to the Plater module's GizmoManager, which is not
            // available from Preview. The tool activation is left as a follow-up.
        }
    };

    // Initial refresh
    refresh();
}

void SidebarSlaSupports::on_selected_config_container_changed(Domain::SelectionId project_id, Domain::SelectionId /*container_id*/)
{
    if (m_project_interactor.selected_project_id() == project_id) {
        refresh();
    }
}

void SidebarSlaSupports::on_selected_project_changed(size_t index)
{
    m_current_project_id = index;
    refresh();
}

void SidebarSlaSupports::on_selected_bed_instances_changed(Domain::SelectionId project_id, const Biz::Scene::BedSelection& /*bed_selection*/)
{
    if (m_project_interactor.selected_project_id() == project_id) {
        refresh();
    }
}

void SidebarSlaSupports::refresh()
{
    // Clear existing rows
    if (m_rows_container) {
        auto items = m_rows_container->items();
        for (Item* item : items) {
            m_rows_container->remove_later(item);
        }
    }

    update_visibility();

    if (!is_visible()) {
        return;
    }

    const auto project_id = m_project_interactor.selected_project_id();
    if (project_id == Domain::INVALID_ID) {
        return;
    }

    const auto& project = m_project_interactor.selected_project();
    const auto& model = project.model();

    // Get the selected bed instance
    const auto& bed_selection = m_project_interactor.scene_interactor().bed_selection();
    const auto bed_ref = bed_selection.last_selected_bed();
    const auto bed_instance_id = bed_ref.instance_id;

    if (bed_instance_id == Domain::INVALID_ID) {
        return;
    }

    // Find the bed instance to get its model instances
    const BedInstance* bed_instance = project.find_bed_instance_by_id(bed_instance_id);
    if (!bed_instance) {
        return;
    }

    const auto& bed_model_instances = bed_instance->model_instances;

    // For each model object that has instances on this bed, show its support points count
    for (const ModelObject* object : model.objects) {
        // Check if this object has instances on the selected bed
        bool has_instance_on_bed = false;
        for (const ModelInstance* instance : bed_model_instances) {
            if (instance->get_object() == object) {
                has_instance_on_bed = true;
                break;
            }
        }

        if (!has_instance_on_bed) {
            continue;
        }

        // Create a row for this model
        Item* row = m_rows_container->emplace_back<Item>();
        row->set_orientation(Orientation::Horizontal);
        row->set_justify_content(YGJustifySpaceBetween);
        row->set_gap(10_fpx);

        Text* label_text = row->emplace_back<Text>(object->name);
        label_text->set_font_type(Render::ImguiFontType::Regular);

        std::string points_text = std::to_string(object->sla_support_points.size()) + " " + _u8L("points");
        Text* value_text = row->emplace_back<Text>(points_text);
        value_text->set_font_type(Render::ImguiFontType::Regular);
        value_text->set_text_color(m_theme->color_imgui(Platform::Color::Text));
    }
}

void SidebarSlaSupports::update_visibility()
{
    bool is_sla = false;
    if (m_current_project_id != Domain::INVALID_ID
        && m_project_interactor.project_exists(m_current_project_id))
    {
        const ConfigContainer& config_container = m_project_interactor.selected_config_container();
        is_sla = config_container.print_technology() == PrinterTechnology::SLA;
    }
    set_visible(is_sla);
}

} // namespace Slic3r::App::Preview