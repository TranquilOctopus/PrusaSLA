#include "Slic3r/App/Plater/SlaHeightBand.hpp"
#include "Slic3r/App/Plater/SlaHeightBandMeshes.hpp"

#include "Slic3r/App/IsSlaActive.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Scene/ClipperPresenter.hpp"
#include "Slic3r/App/Scene/ISceneProvider.hpp"
#include "Slic3r/App/Scene/Node.hpp"
#include "Slic3r/App/Scene/NodeBuilder.hpp"
#include "Slic3r/App/Scene/Scene.hpp"
#include "Slic3r/App/ThemeTypes.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/SliderWithInput.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/ConfigContainer.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/Project.hpp"

#include <algorithm>
#include <cmath>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App::Plater {

using Biz::_u8L;

static constexpr double band_step   = 0.1;
static constexpr int band_precision = 1;

SlaHeightBand::SlaHeightBand(
    Biz::ProjectInteractor& project_interactor,
    Render::Device& device,
    Scene::ISceneProvider& scene_provider,
    Scene::Clipper& clipper
) :
    Window("SlaHeightBand"),
    m_project_interactor(project_interactor),
    m_device(device),
    m_scene_provider(scene_provider),
    m_config_container_listener_scope(project_interactor, *this),
    m_project_listener_scope(project_interactor, *this),
    m_selection_listener_scope(project_interactor.scene_interactor(), *this),
    m_bed_selection_listener_scope(project_interactor.scene_interactor(), *this),
    m_sla_object_cache_listener_scope(project_interactor.sla_object_cache(), *this)
{
    set_orientation(Orientation::Vertical);
    set_gap(5_fpx);
    set_padding(10_fpx);
    set_visible(false);

    // The clipper presenter draws the selected object clipped to the band into this subtree.
    Scene::Scene& scene = m_scene_provider.scene();
    Scene::NodeBuilder builder{scene};
    builder.set_debug_name("SlaHeightBand - Main");
    std::unique_ptr<Scene::Node> main_node = builder.build();
    m_main_node                            = main_node.get();
    scene.add_child(main_node.release(), &scene.root());
    m_presenter = std::make_unique<Scene::ClipperPresenter>(&clipper, &m_device, &m_scene_provider);

    Text* title = emplace_back<Text>(_u8L("Height band"), Render::ImguiFontType::Bold);
    title->set_text_color(m_theme->color_imgui(Platform::Color::Text));

    auto add_slider = [this](const std::string& name, SliderWithInput*& slider)
    {
        Item* row = emplace_back<Item>();
        row->set_gap(5_fpx);

        Text* label = row->emplace_back<Text>(name);
        label->set_font_type(Render::ImguiFontType::Regular);
        label->set_text_color(
            m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled)
        );

        slider = row->emplace_back<SliderWithInput>(_u8L("mm"));
        slider->set_flex_grow(1.f);
        slider->set_step(band_step);
        slider->set_validator_precision(band_precision);
    };

    add_slider(_u8L("Bottom"), m_min_slider);
    m_min_slider->callbacks().value_changed = [this](double value)
    {
        m_z_min = value;
        apply();
    };

    add_slider(_u8L("Top"), m_max_slider);
    m_max_slider->callbacks().value_changed = [this](double value)
    {
        m_z_max = value;
        apply();
    };

    Item* reset_row = emplace_back<Item>();
    reset_row->set_justify_content(YGJustifyFlexEnd);
    m_reset_button = reset_row->emplace_back<LayoutButton>(
        _u8L("Reset"),
        Render::Icon::DSRevert,
        _u8L("Show the whole print height again")
    );
    m_reset_button->callbacks().action = [this]()
    {
        m_z_min = 0.;
        m_z_max = m_max_print_height;
        update_limits();
        apply();
    };

    refresh();
}

SlaHeightBand::~SlaHeightBand()
{
    // Hand the shared clipper back in a neutral state, the listener scopes unregister themselves.
    if (m_presented && m_presenter) {
        m_presenter->deactivate();
    }
}

void SlaHeightBand::on_selected_config_container_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId /*container_id*/
)
{
    if (m_project_interactor.selected_project_id() == project_id) {
        refresh();
    }
}

void SlaHeightBand::on_selected_project_changed(size_t /*index*/)
{
    refresh();
}

void SlaHeightBand::on_selected_project_changed_final(size_t /*index*/)
{
    // The config container and the bed are only complete after the final callback.
    refresh();
}

void SlaHeightBand::on_scene_selection_changed(
    Domain::SelectionId project_id,
    const Biz::Scene::ObjectSelection& /*selection*/
)
{
    if (m_project_interactor.selected_project_id() == project_id) {
        refresh();
    }
}

void SlaHeightBand::on_selected_bed_instances_changed(
    Domain::SelectionId project_id,
    const Biz::Scene::BedSelection& /*bed_selection*/
)
{
    if (m_project_interactor.selected_project_id() == project_id) {
        // Another build plate - the band has to be rebuilt around its models.
        stop();
        refresh();
    }
}

void SlaHeightBand::on_sla_object_cache_changed(const Domain::SlicingId& id, Domain::ObjectID /*object_id*/)
{
    if (id == m_project_interactor.selected_bed_slicing_id()) {
        // Slicing replaced the support tree or the raft of an object on this bed.
        refresh();
    }
}

void SlaHeightBand::set_suspended(bool suspended)
{
    if (m_suspended == suspended)
        return;
    m_suspended = suspended;

    if (m_suspended) {
        // A tool gizmo owns the shared clipper now. Only drop the clipped geometry - it re-enabled
        // the very scene nodes the gizmo just switched off.
        if (m_presenter) {
            m_presenter->reset();
        }
        m_presented = false;
    } else {
        apply();
    }
}

double SlaHeightBand::max_print_height() const
{
    if (m_current_config_container_id == Domain::INVALID_ID
        || !m_project_interactor.project_exists(m_current_project_id))
        return 0.;

    // Same source the build volume geometry uses, see Domain::BedContainer.
    const auto& config_box =
        m_project_interactor.selected_config_container().selected_preset().printer.config_box();
    const auto* item = config_box.find("max_print_height").item;
    return item != nullptr ? item->get<double>() : 0.;
}

const Domain::ModelObject* SlaHeightBand::selected_object() const
{
    if (m_current_project_id == Domain::INVALID_ID
        || !m_project_interactor.project_exists(m_current_project_id))
        return nullptr;
    const Biz::Scene::ObjectSelection& selection =
        m_project_interactor.scene_interactor().object_selection();
    if (selection.elements.empty())
        return nullptr;
    return m_project_interactor.project(m_current_project_id)
        .find_object_by_id(selection.elements.front().object_id);
}

const Domain::ModelInstance* SlaHeightBand::selected_instance() const
{
    if (m_current_project_id == Domain::INVALID_ID
        || !m_project_interactor.project_exists(m_current_project_id))
        return nullptr;
    const Biz::Scene::ObjectSelection& selection =
        m_project_interactor.scene_interactor().object_selection();
    if (selection.elements.empty())
        return nullptr;
    const Domain::ElementRef& element = selection.elements.front();
    return m_project_interactor.project(m_current_project_id)
        .find_instance_by_id(element.object_id, element.instance_id);
}

std::vector<Scene::Clipper::ExtraMesh> SlaHeightBand::collect_extra_meshes() const
{
    if (m_current_project_id == Domain::INVALID_ID
        || !m_project_interactor.project_exists(m_current_project_id))
        return {};

    const Domain::SlicingId slicing_id = m_project_interactor.selected_bed_slicing_id();
    if (slicing_id.project_id != m_current_project_id)
        return {};

    const Domain::Project& project = m_project_interactor.project(m_current_project_id);
    const Domain::BedInstance* bed = project.find_bed_instance_by_id(slicing_id.bed_instance_id);
    if (bed == nullptr)
        return {};

    return collect_height_band_meshes(
        *bed, slicing_id, m_project_interactor.sla_object_cache(), selected_instance()
    );
}

void SlaHeightBand::update_visibility()
{
    const bool is_sla = m_current_project_id != Domain::INVALID_ID
        && m_project_interactor.project_exists(m_current_project_id)
        && is_sla_active(m_project_interactor);
    set_visible(is_sla);
}

void SlaHeightBand::update_limits()
{
    const double max_height = max_print_height();
    const bool limits_grew  = max_height > m_max_print_height;
    m_max_print_height      = max_height;

    if (limits_grew || m_z_max > m_max_print_height) {
        m_z_max = m_max_print_height;
    }
    if (m_z_min > m_max_print_height) {
        m_z_min = m_max_print_height;
    }

    // A slider with an empty range would not be usable while no printer is known.
    const double slider_max = m_max_print_height > 0. ? m_max_print_height : 1.;

    if (m_min_slider) {
        m_min_slider->set_begin_value(0.);
        m_min_slider->set_end_value(slider_max);
        m_min_slider->set_value(std::min(m_z_min, slider_max));
    }
    if (m_max_slider) {
        m_max_slider->set_begin_value(0.);
        m_max_slider->set_end_value(slider_max);
        m_max_slider->set_value(std::min(m_z_max, slider_max));
    }
}

void SlaHeightBand::refresh()
{
    // The panel may be built after a printer is already selected, so do not rely on the callbacks alone.
    m_current_project_id          = m_project_interactor.selected_project_id();
    m_current_config_container_id = m_project_interactor.selected_config_container_id();

    update_visibility();

    if (!is_visible()) {
        stop();
        return;
    }

    update_limits();
    apply();
}

void SlaHeightBand::apply()
{
    m_band = Scene::HeightBand::make(m_z_min, m_z_max, m_max_print_height);

    const Domain::ModelObject* object     = selected_object();
    const Domain::ModelInstance* instance = selected_instance();
    // The band no longer needs a selection - it shows the whole plate. What it does need is a
    // printer to take max_print_height from, which the visibility check above guarantees.
    const bool can_clip = m_presenter != nullptr && m_main_node != nullptr;

    if (m_min_slider != nullptr)
        m_min_slider->set_enabled(can_clip);
    if (m_max_slider != nullptr)
        m_max_slider->set_enabled(can_clip);
    if (m_reset_button != nullptr)
        m_reset_button->set_enabled(can_clip);

    if (m_presented && (m_clip_object != object || m_clip_instance != instance)) {
        // The clipper holds raw pointers to the old target, which may already be gone.
        m_presenter->reset();
        m_presented = false;
    }

    if (m_suspended || !m_band.active || !can_clip) {
        stop();
        return;
    }

    if (!m_presented) {
        m_presenter->activate(object, instance, m_main_node, 0., Scene::BuildMeshesNodes::Yes);
        // fill_cut caps the cut faces, so the inside of the model stays readable.
        m_presenter->set_behavior(true, true, 0.);
        m_presented     = true;
        m_clip_object   = object;
        m_clip_instance = instance;
    }

    // The rest of the plate, the support trees and the rafts, capped with the same planes.
    m_presenter->set_extra_meshes(collect_extra_meshes());
    m_presenter->set_height_band(m_band);
}

void SlaHeightBand::stop()
{
    if (m_presented && m_presenter) {
        m_presenter->deactivate();
    }
    m_presented = false;
}

} // namespace Slic3r::App::Plater
