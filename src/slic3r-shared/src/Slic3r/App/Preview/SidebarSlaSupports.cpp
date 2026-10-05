#include "Slic3r/App/Preview/SidebarSlaSupports.hpp"

#include <algorithm>
#include <optional>
#include <string>

#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/IDialogManager.hpp"
#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/Plater/SlaSupportPointsClear.hpp"
#include "Slic3r/App/Scene/IGizmo.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/Domain/ElementRef.hpp"
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
    m_slicing_input_listener_scope(project_interactor.scene_interactor(), *this),
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

// The one line telling where the workflow stands, the Slice call to action is one of its texts.
    m_status_text = emplace_back<Text>(_u8L(""));
    m_status_text->set_font_type(Render::ImguiFontType::Regular);
    m_status_text->set_margin({ 0.f, 0.f, 0.f, 5.f });

    // What the Auto raft type decided for the models on this plate (M7.8.4). It carries no text of
    // its own until there is something to say, and it is hidden again when there is not.
    m_auto_raft_text = emplace_back<Text>(_u8L(""));
    m_auto_raft_text->set_font_type(Render::ImguiFontType::Regular);
    m_auto_raft_text->set_visible(false);

    // The Auto support actions of the support tool (M2.17b), on the same row.
    Item* auto_support_row = emplace_back<Item>();
    auto_support_row->set_orientation(Orientation::Horizontal);
    auto_support_row->set_gap(10_fpx);

    m_auto_support_selected_button = auto_support_row->emplace_back<LayoutButton>(
        _u8L("Auto support selected"),
        Render::Icon::None,
        _u8L("Generate support points for the selected models")
    );
    m_auto_support_selected_button->set_flex_grow(1.f);
    m_auto_support_selected_button->callbacks().action = [this]() { auto_support(true); };

    m_auto_support_all_button = auto_support_row->emplace_back<LayoutButton>(
        _u8L("Auto support all"),
        Render::Icon::None,
        _u8L("Generate support points for every model on the build plate")
    );
    m_auto_support_all_button->set_flex_grow(1.f);
    m_auto_support_all_button->callbacks().action = [this]() { auto_support(false); };

    // Removing the points again without opening the tool (M2.32), on the same terms: the selected
    // models or every model on the build plate.
    Item* clear_support_row = emplace_back<Item>();
    clear_support_row->set_orientation(Orientation::Horizontal);
    clear_support_row->set_gap(10_fpx);

    m_clear_selected_button = clear_support_row->emplace_back<LayoutButton>(
        _u8L("Clear selected"),
        Render::Icon::None,
        _u8L("Remove the support points of the selected models")
    );
    m_clear_selected_button->set_flex_grow(1.f);
    m_clear_selected_button->callbacks().action = [this]() { clear_supports(true); };

    m_clear_all_button = clear_support_row->emplace_back<LayoutButton>(
        _u8L("Clear all"),
        Render::Icon::None,
        _u8L("Remove the support points of every model on the build plate")
    );
    m_clear_all_button->set_flex_grow(1.f);
    m_clear_all_button->callbacks().action = [this]() { clear_supports(false); };

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

void SidebarSlaSupports::on_slicing_input_changed(const Domain::BedRef& /*bed_instance*/)
{
    // Support points reach the project as slicing input, so this is how a generation made from here
    // shows what it generated in the list below.
    refresh();
}

void SidebarSlaSupports::on_slicing_input_removed(const Domain::BedRef& /*bed_instance*/)
{
    refresh();
}

void SidebarSlaSupports::render_body(const Domain::Vec2f& pos, const Domain::Vec2f& size)
{
    // The generation runs in the support tool and can stop there at any time, so the section asks
    // the tool whether one is going instead of trusting what it started.
    const bool running = auto_support_running();
    if (running != m_auto_support_running) {
        m_auto_support_running = running;
        update_controls();
    }

    // The raft type Auto is resolved by the support preview on its worker, so its answer lands
    // after the last refresh of this section and nothing else would ever show it. It is asked for
    // while the section is on screen and written only when it changed (M7.8.4).
    if (is_visible()) {
        const std::string note = sla_auto_raft_note(models_with_auto_raft());
        if (note != m_auto_raft_text->text()) {
            m_auto_raft_text->set_text(note);
            m_auto_raft_text->set_visible(!note.empty());
            m_auto_raft_text->set_text_color(m_theme->color_imgui(Platform::Color::Text));
        }
    }

    Yoga::Window::render_body(pos, size);
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

    m_auto_support_running = auto_support_running();
    update_controls();
}

void SidebarSlaSupports::update_controls()
{
    const std::vector<const ModelObject*> printable = listed_printable_objects();
    const std::size_t models_with_supports = static_cast<std::size_t>(
        std::ranges::count_if(printable, [](const ModelObject* object) { return !object->sla_support_points.empty(); })
    );
    const std::vector<const ModelObject*> selected = selected_printable_objects();
    const std::size_t selected_models_with_supports = static_cast<std::size_t>(
        std::ranges::count_if(selected, [](const ModelObject* object) { return !object->sla_support_points.empty(); })
    );

    const SlaSupportsPanelState state = sla_supports_panel_state(
        printable.size(),
        selected.size(),
        models_with_supports,
        selected_models_with_supports,
        m_auto_support_running
    );

    m_edit_supports_button->set_enabled(state.edit_supports_enabled);
    m_auto_support_selected_button->set_enabled(state.auto_support_selected_enabled);
    m_auto_support_all_button->set_enabled(state.auto_support_all_enabled);
    m_clear_selected_button->set_enabled(state.clear_selected_enabled);
    m_clear_all_button->set_enabled(state.clear_all_enabled);

    switch (state.status) {
    case SlaSupportsStatus::NoModels:
        m_status_text->set_text(_u8L("No printable model on this build plate."));
        break;
    case SlaSupportsStatus::NeedsSupports:
        m_status_text->set_text(_u8L("Some models have no support points yet."));
        break;
    case SlaSupportsStatus::Generating:
        m_status_text->set_text(_u8L("Generating support points..."));
        break;
    case SlaSupportsStatus::ReadyToSlice:
        // Nothing here slices, the section only says that the Slice button is what is left to do.
        m_status_text->set_text(_u8L("Every model has support points. Press Slice."));
        break;
    }

    m_status_text->set_text_color(
        m_theme->color_imgui(state.slice_call_to_action ? Platform::Color::AccentSecondary : Platform::Color::Text)
    );
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

const ModelInstance* SidebarSlaSupports::supportable_instance(const ModelObject* object) const
{
    if (!object) {
        return nullptr;
    }

    // The same rule as the support tool: a model can be supported when a printable instance of it
    // sits on a build plate.
    const Domain::Project& project = m_project_interactor.selected_project();
    for (const ModelInstance* instance : object->instances) {
        if (!instance || !instance->is_printable()) {
            continue;
        }
        const Domain::BedRef bed_ref = instance->get_last_bed();
        if (project.find_bed_instance_by_id(bed_ref.instance_id) != nullptr) {
            return instance;
        }
    }
    return nullptr;
}

std::vector<const ModelObject*> SidebarSlaSupports::listed_printable_objects() const
{
    std::vector<const ModelObject*> printable;
    for (const ModelObject* object : listed_objects()) {
        if (supportable_instance(object)) {
            printable.push_back(object);
        }
    }
    return printable;
}

std::vector<const ModelObject*> SidebarSlaSupports::selected_printable_objects() const
{
    const std::vector<const ModelObject*> printable = listed_printable_objects();
    if (printable.empty()) {
        return {};
    }

    // The models selected in the scene, so that "Auto support selected" acts on what the user
    // works on. Without such a selection the model the section would edit is the one to act on.
    std::vector<const ModelObject*> selected;
    const Biz::Scene::ObjectSelection& selection = m_project_interactor.scene_interactor().object_selection();
    for (const Domain::ElementRef& element : selection.elements) {
        const auto it = std::ranges::find_if(
            printable,
            [&element](const ModelObject* object) { return object->id().id == element.object_id; }
        );
        if (it != printable.end()) {
            selected.push_back(*it);
        }
    }

    if (selected.empty()) {
        if (const ModelObject* object = edited_object();
            object != nullptr && std::ranges::find(printable, object) != printable.end())
        {
            selected.push_back(object);
        }
    }

    return selected;
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
    if (const ModelObject* object = edited_object(); object != nullptr) {
        open_support_tool({ object });
    }
}

void SidebarSlaSupports::auto_support(bool selected_only)
{
    if (m_auto_support_running || !m_navigator) {
        return;
    }

    const std::vector<const ModelObject*> objects = selected_only ? selected_printable_objects()
                                                                 : listed_printable_objects();
    if (objects.empty()) {
        return;
    }

    // The generation is the support tool's (M2.17b), which runs the engine on its worker thread, so
    // the section opens the tool on the models and hands them over to it.
    open_support_tool(objects);

    std::vector<Domain::ObjectID> object_ids;
    object_ids.reserve(objects.size());
    for (const ModelObject* object : objects) {
        object_ids.push_back(object->id());
    }

    if (!m_navigator->run_sla_auto_support(object_ids)) {
        return;
    }

    m_auto_support_running = true;
    update_controls();
}

// Removing the support points does not need the support tool at all (M2.32), so the section does it
// from here: what would go is worked out first, the user is asked how many points of how many models
// that is, and only an answer of yes takes them away.
void SidebarSlaSupports::clear_supports(bool selected_only)
{
    if (m_auto_support_running) {
        return;
    }

    const std::vector<const ModelObject*> objects = selected_only ? selected_printable_objects()
                                                                 : listed_printable_objects();
    const Plater::SlaSupportPointsClearPlan plan = Plater::sla_support_points_clear_plan(objects);
    if (plan.empty()) {
        return;
    }

    AppServices::instance().dialog_manager().show_yesno_dialog(
        _u8L("Clear support points"),
        Plater::sla_support_points_clear_question(plan),
        [this, plan](bool answer)
        {
            if (!answer) {
                return;
            }
            Plater::clear_sla_support_points(m_project_interactor, plan);
            // The points of the section's list and its rows are the ones that went, so it reads the
            // new state of the build plate.
            refresh();
        }
    );
}

void SidebarSlaSupports::open_support_tool(const std::vector<const ModelObject*>& objects)
{
    if (m_project_interactor.selected_project_id() == Domain::INVALID_ID || !m_navigator) {
        return;
    }

    // Switch to Prepare (Plater) module
    m_navigator->navigate_to_module_type(Render::ModuleType::Plater);

    Biz::Scene::ObjectSelection::ElementRefs elements;
    elements.reserve(objects.size());
    for (const ModelObject* object : objects) {
        const ModelInstance* instance = supportable_instance(object);
        if (instance) {
            elements.push_back(Domain::ElementRef{ object->id().id, instance->id().id });
        }
    }
    if (elements.empty()) {
        return;
    }

    // Select the models before the tool is activated, the tool is enabled by the selection.
    m_project_interactor.scene_interactor().set_object_selection({
        Biz::Scene::SelectionMode::Instance,
        elements
    });
    m_navigator->activate_plater_tool(Scene::ToolType::SlaSupportPoints);
}

bool SidebarSlaSupports::auto_support_running() const
{
    // The generation belongs to the support tool of the Prepare view, which only the navigator
    // reaches from here, and an unopened tool runs nothing.
    return m_navigator != nullptr && m_navigator->sla_auto_support_running();
}

std::size_t SidebarSlaSupports::models_with_auto_raft() const
{
    // The decision was taken by the support preview, which read the undersides of the models on its
    // own worker, so nothing is read or sliced here: a model without a built preview (no points, or
    // a build that has not come back yet) has nothing to report either.
    if (m_navigator == nullptr) {
        return 0;
    }

    std::size_t count = 0;
    for (const ModelObject* object : listed_printable_objects()) {
        const std::optional<Domain::sla::RaftType> raft = m_navigator->sla_auto_raft_type(object->id());
        // A raft type that is not Auto reports nothing at all, and an Auto that resolved to no raft
        // resolves to None, which is the same answer: this model prints without one.
        if (raft.has_value() && *raft != Domain::sla::RaftType::None) {
            ++count;
        }
    }
    return count;
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
