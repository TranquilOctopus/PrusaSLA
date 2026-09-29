#include "Slic3r/App/Preview/SidebarSlaSupports.hpp"

#include <algorithm>

#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/Scene/IGizmo.hpp"

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
    m_project_interactor(project_interactor),
    // The project may already be selected when this is constructed, so the initial state has to be read.
    m_current_project_id(project_interactor.selected_project_id())
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
    m_edit_supports_button->callbacks().action = [this]() { edit_supports(); };

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

    if (m_project_interactor.selected_project_id() == Domain::INVALID_ID) {
        return;
    }

    for (const ModelObject* object : listed_objects()) {
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

std::vector<const ModelObject*> SidebarSlaSupports::listed_objects() const
{
    if (m_current_project_id == Domain::INVALID_ID
        || !m_project_interactor.project_exists(m_current_project_id)
        || m_current_project_id != m_project_interactor.selected_project_id())
    {
        return {};
    }

    const auto& project = m_project_interactor.selected_project();

    // Only the models placed on the selected bed are listed
    const auto& bed_selection = m_project_interactor.scene_interactor().bed_selection();
    const BedInstance* bed_instance =
        project.find_bed_instance_by_id(bed_selection.last_selected_bed().instance_id);
    if (!bed_instance) {
        return {};
    }

    std::vector<const ModelObject*> objects;
    for (const ModelObject* object : project.model().objects) {
        const bool has_instance_on_bed = std::ranges::any_of(
            bed_instance->model_instances,
            [object](const ModelInstance* instance) { return instance->get_object() == object; }
        );
        if (has_instance_on_bed) {
            objects.push_back(object);
        }
    }
    return objects;
}

const ModelObject* SidebarSlaSupports::edited_object() const
{
    const std::vector<const ModelObject*> objects = listed_objects();
    if (objects.empty()) {
        return nullptr;
    }

    // The model selected in the scene wins, so that the button edits what the user works on.
    const Biz::Scene::ObjectSelection& selection =
        m_project_interactor.scene_interactor().object_selection();
    if (selection.elements.size() == 1) {
        const size_t object_id = selection.elements.front().object_id;
        const auto it = std::ranges::find_if(
            objects,
            [object_id](const ModelObject* object) { return object->id().id == object_id; }
        );
        if (it != objects.end()) {
            return *it;
        }
    }

    return objects.front();
}

void SidebarSlaSupports::edit_supports()
{
    if (m_project_interactor.selected_project_id() == Domain::INVALID_ID || !m_navigator) {
        return;
    }

    // Switch to Prepare (Plater) module
    m_navigator->navigate_to_module_type(Render::ModuleType::Plater);

    const ModelObject* object = edited_object();
    if (!object) {
        return;
    }

    const auto it = std::ranges::find_if(
        object->instances,
        [](const ModelInstance* instance) { return instance->is_printable(); }
    );
    if (it == object->instances.end()) {
        return; // no printable instance to edit
    }

    // Select the instance before the tool is activated, the tool is enabled by the selection.
    m_project_interactor.scene_interactor().set_object_selection({
        Biz::Scene::SelectionMode::Instance,
        { { object->id().id, (*it)->id().id } }
    });
    m_navigator->activate_plater_tool(Scene::ToolType::SlaSupportPoints);
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