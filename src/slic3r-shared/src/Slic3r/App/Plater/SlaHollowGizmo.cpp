#include "Slic3r/App/Plater/SlaHollowGizmo.hpp"
#include "Slic3r/App/Plater/SlaHollowDialog.hpp"
#include "Slic3r/App/Plater/SlaDrainHolesEditing.hpp"
#include "Slic3r/App/Plater/PlaterScenePresenter.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/IDialogManager.hpp"
#include "Slic3r/App/Scene/Scene.hpp"
#include "Slic3r/App/Scene/NodeBuilder.hpp"
#include "Slic3r/App/Scene/GeometryDataFactory.hpp"
#include "Slic3r/App/Scene/Ray.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Render/GeometryBuilder.hpp"
#include "Slic3r/App/Plater/PlaterSceneLayer.hpp"
#include "Slic3r/App/DisplayStrings.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Slicing/SlicingInteractor.hpp"
#include "Slic3r/Biz/StatusCache.hpp"
#include "Slic3r/Biz/IUndoProvider.hpp"
#include "Slic3r/Biz/Utils/MeshRaycaster.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/Utils/MeshRaycaster.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/Domain/ConfigContainer.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/SLA/DrainHole.hpp"
#include "Slic3r/Math.hpp"
#include "libslic3r/SLAResult.hpp"
#include "libslic3r/IPrint.hpp"
#include "libslic3r/PrintSteps.hpp"
#include "Slic3r/App/Platform/KeyboardEvent.hpp"
#include "Slic3r/App/Platform/KeyModifers.hpp"
#include "Slic3r/App/Platform/KeyCode.hpp"

#include <Eigen/Geometry>
#include <fmt/format.h>
#include <magic_enum/magic_enum_flags.hpp>

using namespace Slic3r;
using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;
using namespace Slic3r::Biz::Slicing;
using namespace Slic3r::Biz::Utils;
using namespace Slic3r::Biz::Algorithms;
using namespace Slic3r::Biz::Utils;
using namespace magic_enum::bitwise_operators;

using Slic3r::Domain::SlicingId;
using Slic3r::Domain::ObjectID;
using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec3f;
using Slic3r::Domain::ColorRGBA;
using Slic3r::Domain::SLA::DrainHole;
using Slic3r::Domain::SLA::DrainHoles;

namespace Slic3r::Biz {

class SlaHollowRequest :
    public ISLAObjectCacheChangedListener,
    public IStatusCacheChangedListener
{
public:
    struct Callbacks
    {
        std::function<void(std::optional<Slicing::Sla::Object>)> completed =
            [](std::optional<Slicing::Sla::Object>) {};
    };

    SlaHollowRequest() = delete;

    SlaHollowRequest(
        SlicingInteractor& slicing_interactor,
        StatusCache& status_cache,
        SLAObjectCache& sla_object_cache,
        ProjectInteractor& project_interactor
    ) :
        m_slicing_interactor(slicing_interactor),
        m_status_cache(status_cache),
        m_sla_object_cache(sla_object_cache),
        m_project_interactor(project_interactor)
    {}

    ~SlaHollowRequest() override
    {
        this->cancel();
    }

    Callbacks& callbacks()
    {
        return m_callbacks;
    }

    void start(SlicingId slicing_id, ObjectID model_object_id)
    {
        if (this->running()) {
            return;
        }

        m_state = State::WaitingForSlicing;
        m_slicing_id = slicing_id;
        m_model_object_id = model_object_id;
        m_has_fresh_object = false;
        m_sla_object_cache.add_listener<ISLAObjectCacheChangedListener>(this);
        m_status_cache.add_listener<IStatusCacheChangedListener>(this);

        const StatusCode status = m_slicing_interactor.get_status(slicing_id);
        if (status == StatusCode::Finished) {
            this->try_complete_from_cache();
        } else if (status == StatusCode::Modified) {
            this->request_slicing_until_hollowing();
        } else if (status == StatusCode::Empty || status == StatusCode::InvalidData) {
            this->complete(std::nullopt);
        }
    }

    void cancel()
    {
        if (!this->running()) {
            return;
        }

        m_sla_object_cache.remove_listener<ISLAObjectCacheChangedListener>(this);
        m_status_cache.remove_listener<IStatusCacheChangedListener>(this);

        m_state = State::Idle;
    }

    [[nodiscard]] bool running() const
    {
        return m_state != State::Idle;
    }

    void on_sla_object_cache_changed(const SlicingId& id, ObjectID object_id) override
    {
        if (!this->running() || id != m_slicing_id || object_id != m_model_object_id) {
            return;
        }

        const std::optional<Slicing::Status> current_status = m_status_cache.get_status(id);
        if (current_status.has_value()
            && current_status->code == StatusCode::Running
            && this->cached_object().has_value())
        {
            m_has_fresh_object = true;
        }
    }

    void on_status_cache_status_code_changed(const SlicingId id) override
    {
        if (!this->running() || id != m_slicing_id) {
            return;
        }

        const std::optional<Slicing::Status> status = m_status_cache.get_status(id);
        if (!status.has_value()) {
            this->complete(std::nullopt);
            return;
        }

        switch (status->code) {
        case StatusCode::Running:
            m_state = State::SlicingActive;
            m_has_fresh_object = false;
            break;
        case StatusCode::Stopping:
            m_state = State::SlicingActive;
            m_has_fresh_object = false;
            break;
        case StatusCode::Updating:
            m_has_fresh_object = false;
            break;
        case StatusCode::Modified:
            if (m_has_fresh_object) {
                this->try_complete_from_cache();
            } else if (m_state == State::WaitingForSlicing) {
                this->request_slicing_until_hollowing();
            } else {
                this->complete(std::nullopt);
            }
            break;
        case StatusCode::Finished:
            this->try_complete_from_cache();
            break;
        case StatusCode::Empty:
        case StatusCode::InvalidData:
            this->complete(std::nullopt);
            break;
        default:
            break;
        }
    }

private:
    enum class State
    {
        Idle,
        WaitingForSlicing,
        SlicingRequested,
        SlicingActive
    };

    [[nodiscard]] std::optional<Slicing::Sla::Object> cached_object() const
    {
        const SLAObjectCache::Key key{m_slicing_id, m_model_object_id};
        const SLAObjectOptRef opt_ref = m_sla_object_cache.get_instance(key);
        if (!opt_ref.has_value()) {
            return std::nullopt;
        }

        return opt_ref->get();
    }

    void try_complete_from_cache()
    {
        const std::optional<Slicing::Sla::Object> object = this->cached_object();
        if (object.has_value()) {
            this->complete(object);
        }
    }

    void complete(std::optional<Slicing::Sla::Object> sla_object)
    {
        this->cancel();
        m_callbacks.completed(sla_object);
    }

    void request_slicing_until_hollowing()
    {
        m_state = State::SlicingRequested;
        m_slicing_interactor.slice_bed(
            m_slicing_id,
            SliceUntilStep{SLAPrintObjectStep::slaposHollowing, m_model_object_id}
        );
    }

    SlicingInteractor& m_slicing_interactor;
    StatusCache& m_status_cache;
    SLAObjectCache& m_sla_object_cache;
    ProjectInteractor& m_project_interactor;

    Callbacks m_callbacks;

    State m_state = State::Idle;
    bool m_has_fresh_object = false;
    SlicingId m_slicing_id;
    ObjectID m_model_object_id;
};

} // namespace Slic3r::Biz

namespace Slic3r::App::Plater {

SlaHollowGizmo::SlaHollowGizmo(
    PlaterScenePresenter& scene_presenter,
    Biz::ProjectInteractor& project_interactor,
    Render::Device& device
) :
    m_scene_presenter(scene_presenter),
    m_project_interactor(project_interactor),
    m_device(device),
    m_dialog(std::make_unique<SlaHollowDialog>())
{
    m_dialog->set_title(_u8L("SLA Hollow"));
    m_dialog->set_shortcut("H");

    m_hollow_request = std::make_unique<Biz::SlaHollowRequest>(
        m_project_interactor.slicing_interactor(),
        m_project_interactor.status_cache(),
        m_project_interactor.sla_object_cache(),
        m_project_interactor
    );

    m_dialog->callbacks().preview = [this]() { this->start_preview(); };
    m_dialog->callbacks().enable_changed = [this](bool value)
    {
        if (m_selected_object_id.valid()) {
            Domain::Project& project = m_project_interactor.selected_project();
            Domain::ModelObject* model_object = project.find_object_by_id(m_selected_object_id.id);
            if (model_object) {
                m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::SlaDrainHolesEdit);
                write_hollowing_config(model_object, value, m_current_min_thickness, m_current_quality, m_current_closing_distance);
                m_current_enable = value;
                if (value) {
                    start_preview();
                } else {
                    clear_preview_visuals();
                    m_dialog->set_status(_u8L("Hollowing disabled."));
                }
            }
        }
    };
    m_dialog->callbacks().min_thickness_changed = [this](double value)
    {
        m_current_min_thickness = value;
        if (m_selected_object_id.valid()) {
            Domain::Project& project = m_project_interactor.selected_project();
            Domain::ModelObject* model_object = project.find_object_by_id(m_selected_object_id.id);
            if (model_object) {
                m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::SlaDrainHolesEdit);
                write_hollowing_config(model_object, m_current_enable, value, m_current_quality, m_current_closing_distance);
                if (m_current_enable) {
                    start_preview();
                }
            }
        }
    };
    m_dialog->callbacks().quality_changed = [this](double value)
    {
        m_current_quality = value;
        if (m_selected_object_id.valid()) {
            Domain::Project& project = m_project_interactor.selected_project();
            Domain::ModelObject* model_object = project.find_object_by_id(m_selected_object_id.id);
            if (model_object) {
                m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::SlaDrainHolesEdit);
                write_hollowing_config(model_object, m_current_enable, m_current_min_thickness, value, m_current_closing_distance);
                if (m_current_enable) {
                    start_preview();
                }
            }
        }
    };
    m_dialog->callbacks().closing_distance_changed = [this](double value)
    {
        m_current_closing_distance = value;
        if (m_selected_object_id.valid()) {
            Domain::Project& project = m_project_interactor.selected_project();
            Domain::ModelObject* model_object = project.find_object_by_id(m_selected_object_id.id);
            if (model_object) {
                m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::SlaDrainHolesEdit);
                write_hollowing_config(model_object, m_current_enable, m_current_min_thickness, m_current_quality, value);
                if (m_current_enable) {
                    start_preview();
                }
            }
        }
    };
    m_dialog->callbacks().hole_radius_changed = [this](double value)
    {
        if (m_edit_state.has_value()) {
            m_edit_state->editing.hole_radius_mm = value;
            apply_radius_to_selected();
        }
    };
    m_dialog->callbacks().hole_height_changed = [this](double value)
    {
        if (m_edit_state.has_value()) {
            m_edit_state->editing.hole_height_mm = value;
            apply_height_to_selected();
        }
    };
    m_dialog->callbacks().remove_selected_holes = [this]()
    {
        delete_selected_holes();
    };
    m_dialog->callbacks().remove_all_holes = [this]()
    {
        select_all_holes();
        delete_selected_holes();
    };

    m_dialog->set_preview_enabled(false);
}

SlaHollowGizmo::~SlaHollowGizmo() = default;

Scene::ToolType SlaHollowGizmo::type() const
{
    return Scene::ToolType::SlaHollow;
}

bool SlaHollowGizmo::supports_printer(Domain::PrinterTechnology pt) const
{
    return pt == Domain::PrinterTechnology::SLA;
}

bool SlaHollowGizmo::enabled() const
{
    const Biz::Scene::ObjectSelection& selection =
        m_project_interactor.scene_interactor().object_selection();
    return selection.state() == Biz::Scene::SelectionState::WholeInstance;
}

void SlaHollowGizmo::provide_gizmo_controller(Scene::IGizmoController& controller)
{
    m_gizmo_controller = &controller;
}

void SlaHollowGizmo::on_activated()
{
    m_project_interactor.scene_interactor().add_listener<Biz::Scene::ISceneSelectionChangedListener>(this);
    m_project_interactor.sla_object_cache().add_listener<Biz::ISLAObjectCacheChangedListener>(this);

    const Biz::Scene::ObjectSelection& selection =
        m_project_interactor.scene_interactor().object_selection();
    this->on_scene_selection_changed(m_project_interactor.selected_project_id(), selection);
}

void SlaHollowGizmo::on_deactivated()
{
    m_project_interactor.scene_interactor().remove_listener<Biz::Scene::ISceneSelectionChangedListener>(this);
    m_project_interactor.sla_object_cache().remove_listener<Biz::ISLAObjectCacheChangedListener>(this);

    if (m_preview_slicing_id.has_value()) {
        m_hollow_request->cancel();
        m_preview_slicing_id.reset();
    }
    m_has_preview = false;

    clear_preview_visuals();

    m_dialog->set_preview_enabled(false);
    m_dialog->set_status(_u8L("No preview generated yet."));
}

void SlaHollowGizmo::on_scene_selection_changed(
    Domain::SelectionId project_id,
    const Biz::Scene::ObjectSelection& selection
)
{
    if (m_preview_slicing_id.has_value()) {
        m_hollow_request->cancel();
        m_preview_slicing_id.reset();
    }
    m_has_preview = false;

    if (!enabled() || selection.elements.empty()) {
        m_dialog->set_enable(false);
        m_dialog->set_preview_enabled(false);
        m_dialog->set_status(_u8L("Select a single object instance to configure hollowing."));
        return;
    }

    const Domain::ElementRef& element = selection.elements.front();
    if (element.volume_id != 0) {
        m_dialog->set_enable(false);
        m_dialog->set_preview_enabled(false);
        m_dialog->set_status(_u8L("Select the whole object, not a volume."));
        return;
    }

    const Domain::Project& project = m_project_interactor.project(project_id);
    const Domain::ModelObject* model_object = project.find_object_by_id(element.object_id);
    if (!model_object) {
        m_dialog->set_enable(false);
        m_dialog->set_preview_enabled(false);
        m_dialog->set_status(_u8L("Object not found."));
        return;
    }

    m_selected_object_id = model_object->id();
    m_selected_instance_id = element.instance_id;

    const Domain::ModelInstance* instance = project.find_instance_by_id(element.object_id, element.instance_id);
    if (!instance) {
        m_dialog->set_enable(false);
        m_dialog->set_preview_enabled(false);
        m_dialog->set_status(_u8L("Instance not found."));
        return;
    }

    const Domain::BedRef bed_ref = instance->get_last_bed();
    if (project.find_bed_instance_by_id(bed_ref.instance_id) == nullptr) {
        m_dialog->set_enable(false);
        m_dialog->set_preview_enabled(false);
        m_dialog->set_status(_u8L("Object must be placed on a bed."));
        return;
    }

    // Read current config
    read_hollowing_config(model_object);

    m_dialog->set_enable(m_current_enable);
    m_dialog->set_min_thickness(m_current_min_thickness);
    m_dialog->set_quality(m_current_quality);
    m_dialog->set_closing_distance(m_current_closing_distance);

    // Initialize preview scene nodes
    Scene::Scene& scene = m_scene_presenter.scene();
    Scene::NodeBuilder main_builder{scene};
    main_builder.set_debug_name("SlaHollowGizmo - Main");
    std::unique_ptr<Scene::Node> main_node = main_builder.build();
    m_main_node = main_node.get();
    scene.add_child(main_node.release(), &scene.root());

    Scene::NodeBuilder preview_builder{scene};
    preview_builder.set_debug_name("SlaHollowGizmo - Preview");
    std::unique_ptr<Scene::Node> preview_node = preview_builder.build();
    m_preview_node = preview_node.get();
    scene.add_child(preview_node.release(), m_main_node);

    m_dialog->set_preview_enabled(true);
    m_dialog->set_status(_u8L("Ready to preview."));

    // Connect drain hole dialog callbacks
    m_dialog->callbacks().hole_radius_changed = [this](double value)
    {
        m_current_hole_radius = value;
        if (m_edit_state.has_value()) {
            m_edit_state->editing.hole_radius_mm = value;
        }
    };
    m_dialog->callbacks().hole_height_changed = [this](double value)
    {
        m_current_hole_height = value;
        if (m_edit_state.has_value()) {
            m_edit_state->editing.hole_height_mm = value;
        }
    };
    m_dialog->callbacks().remove_selected_holes = [this]()
    {
        delete_selected_holes();
    };
    m_dialog->callbacks().remove_all_holes = [this]()
    {
        if (m_edit_state.has_value()) {
            m_edit_state->editing.select_all_holes();
            delete_selected_holes();
        }
    };
}

void SlaHollowGizmo::on_sla_object_cache_changed(const Domain::SlicingId& id, Domain::ObjectID object_id)
{
    if (!m_preview_slicing_id.has_value() || id != *m_preview_slicing_id || object_id != m_selected_object_id) {
        return;
    }
    // The request callback will handle completion
}

void SlaHollowGizmo::start_preview()
{
    if (m_preview_slicing_id.has_value() || !m_selected_object_id.valid()) {
        return;
    }

    Domain::Project& project = m_project_interactor.selected_project();
    Domain::ModelObject* model_object = project.find_object_by_id(m_selected_object_id.id);
    if (!model_object) {
        return;
    }

    const Domain::ModelInstance* instance = project.find_instance_by_id(m_selected_object_id.id, m_selected_instance_id);
    if (!instance || !instance->is_printable()) {
        AppServices::instance().dialog_manager().show_warning_dialog(
            _u8L("Preview requires printable object."),
            _u8L("Warning")
        );
        return;
    }

    const Domain::BedRef bed_ref = instance->get_last_bed();
    if (project.find_bed_instance_by_id(bed_ref.instance_id) == nullptr) {
        AppServices::instance().dialog_manager().show_warning_dialog(
            _u8L("Preview requires the object to be placed on a bed."),
            _u8L("Warning")
        );
        return;
    }

    const SlicingId slicing_id{m_project_interactor.selected_project_id(), bed_ref.instance_id};
    const StatusCode status = m_project_interactor.slicing_interactor().get_status(slicing_id);
    if (status == StatusCode::InvalidData) {
        std::string error_message;
        const std::optional<Slicing::Status> status_opt = m_project_interactor.status_cache().get_status(slicing_id);
        if (status_opt.has_value()) {
            for (const Slicing::Error& error : status_opt->errors) {
                error_message += "\n" + App::to_display_string(error, project);
            }
        }
        AppServices::instance().dialog_manager().show_warning_dialog(
            _u8L("Preview requires valid print setup.") + error_message,
            _u8L("Warning")
        );
        return;
    } else if (status == StatusCode::Empty) {
        AppServices::instance().dialog_manager().show_warning_dialog(
            _u8L("Preview requires printable object."),
            _u8L("Warning")
        );
        return;
    }

    m_preview_slicing_id = slicing_id;
    m_dialog->set_preview_enabled(false);
    m_dialog->set_status(_u8L("Generating preview..."));

    m_hollow_request->callbacks().completed =
        [this](const std::optional<Slicing::Sla::Object> sla_object)
    { this->on_preview_completed(sla_object); };

    m_hollow_request->start(slicing_id, m_selected_object_id);
}

void SlaHollowGizmo::on_preview_completed(std::optional<Biz::Slicing::Sla::Object> sla_object)
{
    m_preview_slicing_id.reset();

    if (sla_object.has_value() && sla_object->mesh) {
        m_has_preview = true;

        const Domain::Project& project = m_project_interactor.selected_project();
        const Domain::ModelInstance* instance = project.find_instance_by_id(m_selected_object_id.id, m_selected_instance_id);

        show_preview_mesh(*sla_object, instance);

        m_dialog->set_preview_enabled(true);
        m_dialog->set_status(_u8L("Preview generated."));
    } else {
        m_has_preview = false;
        clear_preview_visuals();
        m_dialog->set_preview_enabled(true);
        m_dialog->set_status(_u8L("Failed to generate preview."));

        AppServices::instance().dialog_manager().show_warning_dialog(
            _u8L("Failed to generate hollowing preview."),
            _u8L("Warning")
        );
    }
}

void SlaHollowGizmo::clear_preview_visuals()
{
    if (m_preview_node != nullptr) {
        Scene::Scene& scene = m_scene_presenter.scene();
        scene.remove_children([this](const Scene::Node* node) {
            return node->parent() == m_preview_node;
        }, m_preview_node);
    }

    if (m_main_node != nullptr) {
        Scene::Scene& scene = m_scene_presenter.scene();
        scene.remove_child(m_main_node);
        m_main_node = nullptr;
        m_preview_node = nullptr;
    }
}

void SlaHollowGizmo::show_preview_mesh(const Biz::Slicing::Sla::Object& sla_object, const Domain::ModelInstance* instance)
{
    if (!sla_object.mesh || m_preview_node == nullptr || !instance) {
        return;
    }

    Scene::Scene& scene = m_scene_presenter.scene();

    // Sla::Object::mesh is already a Domain::TriangleMesh; hand its indexed set to the scene.
    if (sla_object.mesh->its.indices.empty()) {
        return;
    }

    const std::string mesh_id = "sla_hollow_preview";
    auto trimesh = m_triangle_mesh_manager.get_or_create(mesh_id, [&]() {
        return std::make_unique<Scene::TriangleMesh>(sla_object.mesh);
    });

    // Create or get geometry
    const auto* geom = m_geometry_manager.get_or_create(mesh_id, [&]() {
        return Render::geometry_from_triangle_mesh(m_device, trimesh->triangles());
    });

    // Instance transform
    const Domain::Transform3d instance_trafo = instance->get_matrix();

    // Semi-transparent material for preview
    ColorRGBA color = AppServices::instance().theme().color(Platform::Color::SlaSupportPointAuto, Platform::ColorGroup::Default);
    color = ColorRGBA{color.r(), color.g(), color.b(), 0.3f};

    Render::Material material = Render::Material{}
        .set_shader(m_device.context().shader_manager().shader("gouraud_light"))
        .set_uniform("uniform_color", color);

    Domain::Transform3d xform = instance_trafo;

    Scene::NodeBuilder builder{scene};
    builder.set_debug_name("hollow_preview_mesh")
        .set_mesh(geom, material, Scene::RenderLayerId(PlaterSceneLayer::GizmoHandles))
        .set_aabb(trimesh->aabb_mesh())
        .set_transform(xform);

    scene.add_child(builder.build().release(), m_preview_node);
}

bool SlaHollowGizmo::read_hollowing_config(const Domain::ModelObject* model_object)
{
    if (!model_object) {
        m_current_enable = false;
        m_current_min_thickness = 3.0;
        m_current_quality = 0.5;
        m_current_closing_distance = 2.0;
        return false;
    }

    bool enable = false;
    double min_thickness = 3.0;
    double quality = 0.5;
    double closing_distance = 2.0;

    auto enable_result = model_object->object_settings_sla.find("hollowing_enable");
    if (enable_result.item) {
        enable = enable_result.item->get<bool>();
    }

    auto thickness_result = model_object->object_settings_sla.find("hollowing_min_thickness");
    if (thickness_result.item) {
        min_thickness = thickness_result.item->get<double>();
    }

    auto quality_result = model_object->object_settings_sla.find("hollowing_quality");
    if (quality_result.item) {
        quality = quality_result.item->get<double>();
    }

    auto closing_result = model_object->object_settings_sla.find("hollowing_closing_distance");
    if (closing_result.item) {
        closing_distance = closing_result.item->get<double>();
    }

    m_current_enable = enable;
    m_current_min_thickness = min_thickness;
    m_current_quality = quality;
    m_current_closing_distance = closing_distance;

    return true;
}

void SlaHollowGizmo::write_hollowing_config(Domain::ModelObject* model_object, bool enable, double min_thickness, double quality, double closing_distance)
{
    if (!model_object) {
        return;
    }

    auto enable_result = model_object->object_settings_sla.find("hollowing_enable");
    if (enable_result.item) {
        enable_result.item->set<bool>(enable);
    }

    auto thickness_result = model_object->object_settings_sla.find("hollowing_min_thickness");
    if (thickness_result.item) {
        thickness_result.item->set<double>(min_thickness);
    }

    auto quality_result = model_object->object_settings_sla.find("hollowing_quality");
    if (quality_result.item) {
        quality_result.item->set<double>(quality);
    }

    auto closing_result = model_object->object_settings_sla.find("hollowing_closing_distance");
    if (closing_result.item) {
        closing_result.item->set<double>(closing_distance);
    }
}

void SlaHollowGizmo::take_undo_snapshot()
{
    m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::SlaDrainHolesEdit);
}

std::unique_ptr<GizmoWindow> SlaHollowGizmo::release_ui_window()
{
    return std::move(m_dialog);
}

Scene::GizmoActivationState SlaHollowGizmo::on_mouse(Scene::GizmoEventContext& ctx, bool only_active)
{
    // No interactive mouse handling for hollowing gizmo
    (void)ctx;
    (void)only_active;
    return Scene::GizmoActivationState::Inactive;
}

void SlaHollowGizmo::render_scene(Render::CommandBuffer& cmd_buffer)
{
    // No per-frame updates needed for static preview
    (void)cmd_buffer;
}

} // namespace Slic3r::App::Plater