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

    Scene::NodeBuilder holes_builder{scene};
    holes_builder.set_debug_name("SlaHollowGizmo - Holes");
    std::unique_ptr<Scene::Node> holes_node = holes_builder.build();
    m_holes_node = holes_node.get();
    scene.add_child(holes_node.release(), m_main_node);

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
        m_holes_node = nullptr;
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

void SlaHollowGizmo::begin_editing()
{
    Domain::Project& project = m_project_interactor.selected_project();
    Domain::ModelObject* model_object = project.find_object_by_id(m_selected_object_id.id);
    if (!model_object) {
        return;
    }

    m_edit_state = DrainHoleEditState{};
    m_edit_state->editing.holes = model_object->sla_drain_holes;
    m_edit_state->editing.hole_radius_mm = m_current_hole_radius;
    m_edit_state->editing.hole_height_mm = m_current_hole_height;
    m_dialog->set_hole_radius(m_current_hole_radius);
    m_dialog->set_hole_height(m_current_hole_height);

    m_dialog->set_apply_enabled(true);
    m_dialog->set_generate_enabled(true);

    update_hole_visuals();
}

void SlaHollowGizmo::end_editing()
{
    clear_hole_visuals();
    m_hovered_hole_idx.reset();
    m_edit_state.reset();
}

void SlaHollowGizmo::apply_edited_holes()
{
    if (!m_edit_state.has_value() || !m_selected_object_id.valid()) {
        return;
    }

    Domain::Project& project = m_project_interactor.selected_project();
    Domain::ModelObject* model_object = project.find_object_by_id(m_selected_object_id.id);
    if (!model_object) {
        return;
    }

    m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::SlaDrainHolesApply);

    model_object->sla_drain_holes = std::move(m_edit_state->editing.holes);

    m_dialog->set_hole_count(model_object->sla_drain_holes.size());
    end_editing();

    if (m_gizmo_controller) {
        m_gizmo_controller->deactivate_current_tool();
    }
}

void SlaHollowGizmo::discard_edited_holes()
{
    end_editing();
    m_dialog->set_apply_enabled(false);

    Domain::Project& project = m_project_interactor.selected_project();
    Domain::ModelObject* model_object = project.find_object_by_id(m_selected_object_id.id);
    if (model_object) {
        m_dialog->set_hole_count(model_object->sla_drain_holes.size());
    }
}

std::optional<size_t> SlaHollowGizmo::find_nearest_hole(const Domain::Vec3d& mesh_pos, double max_distance_mm) const
{
    if (!m_edit_state.has_value()) {
        return std::nullopt;
    }

    return m_edit_state->editing.find_nearest_hole(mesh_pos, max_distance_mm);
}

void SlaHollowGizmo::add_hole_at_mesh_pos(const Domain::Vec3d& mesh_pos, const Domain::Vec3d& mesh_normal)
{
    if (!m_edit_state.has_value()) {
        return;
    }

    m_edit_state->editing.add_hole(mesh_pos, mesh_normal);
    m_dialog->set_hole_count(m_edit_state->editing.holes.size());
    take_hole_undo_snapshot();
    update_hole_visuals();
}

void SlaHollowGizmo::remove_hole_at_index(size_t idx)
{
    if (!m_edit_state.has_value() || idx >= m_edit_state->editing.holes.size()) {
        return;
    }

    m_edit_state->editing.remove_hole(idx);
    m_dialog->set_hole_count(m_edit_state->editing.holes.size());
    take_hole_undo_snapshot();
    update_hole_visuals();
}

void SlaHollowGizmo::move_hole_to_mesh_pos(size_t idx, const Domain::Vec3d& mesh_pos, const Domain::Vec3d& mesh_normal)
{
    if (!m_edit_state.has_value() || idx >= m_edit_state->editing.holes.size()) {
        return;
    }

    m_edit_state->editing.move_hole(idx, mesh_pos, mesh_normal);
    m_dialog->set_hole_count(m_edit_state->editing.holes.size());
    update_hole_visuals();
}

void SlaHollowGizmo::take_hole_undo_snapshot()
{
    m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::SlaDrainHolesEdit);
}

void SlaHollowGizmo::select_hole(size_t idx, bool add_to_selection)
{
    if (!m_edit_state.has_value()) {
        return;
    }
    m_edit_state->editing.select_hole(idx, add_to_selection);
    update_hole_visuals();
}

void SlaHollowGizmo::deselect_hole(size_t idx)
{
    if (!m_edit_state.has_value()) {
        return;
    }
    m_edit_state->editing.deselect_hole(idx);
    update_hole_visuals();
}

void SlaHollowGizmo::select_all_holes()
{
    if (!m_edit_state.has_value()) {
        return;
    }
    m_edit_state->editing.select_all_holes();
    update_hole_visuals();
}

void SlaHollowGizmo::clear_hole_selection()
{
    if (!m_edit_state.has_value()) {
        return;
    }
    m_edit_state->editing.clear_selection();
    update_hole_visuals();
}

void SlaHollowGizmo::delete_selected_holes()
{
    if (!m_edit_state.has_value()) {
        return;
    }

    const size_t old_count = m_edit_state->editing.holes.size();
    m_edit_state->editing.delete_selected_holes();

    if (m_edit_state->editing.holes.size() != old_count) {
        m_dialog->set_hole_count(m_edit_state->editing.holes.size());
        take_hole_undo_snapshot();
        update_hole_visuals();
    }
}

void SlaHollowGizmo::apply_radius_to_selected()
{
    if (!m_edit_state.has_value()) {
        return;
    }
    m_edit_state->editing.apply_radius_to_selected();
    update_hole_visuals();
}

void SlaHollowGizmo::apply_height_to_selected()
{
    if (!m_edit_state.has_value()) {
        return;
    }
    m_edit_state->editing.apply_height_to_selected();
    update_hole_visuals();
}

void SlaHollowGizmo::collect_paintable_volumes(const Domain::SelectionId project_id, const Domain::ElementRef& element)
{
    m_paintable_volumes.clear();

    const Domain::Project& project = m_project_interactor.project(project_id);
    const Domain::ModelObject* model_object = project.find_object_by_id(element.object_id);
    const Domain::ModelInstance* model_instance = project.find_instance_by_id(element.object_id, element.instance_id);

    if (!model_object || !model_instance) {
        return;
    }

    using MeshManager = PlaterScenePresenter::MeshManager;
    const MeshManager& mesh_manager = m_scene_presenter.model_triangle_mesh_manager(project_id);

    for (Domain::ModelVolume* model_volume : model_object->volumes) {
        if (!model_volume->is_model_part()) {
            continue;
        }

        const Scene::AuxiliaryElementId volume_id{
            Scene::AuxiliaryElementId::Type::Volume,
            model_volume->id().id
        };
        const Scene::TriangleMesh* scene_mesh = mesh_manager.get(volume_id);
        if (!scene_mesh) {
            continue;
        }

        m_paintable_volumes.push_back({
            *model_object,
            *model_instance,
            *model_volume,
            *scene_mesh,
            scene_mesh->aabb_mesh(),
            model_instance->get_matrix() * model_volume->get_matrix(),
            model_instance->get_matrix_no_offset() * model_volume->get_matrix_no_offset()
        });
    }
}

std::optional<SlaHollowGizmo::VolumeHitPoint> SlaHollowGizmo::raycast_mouse(const Domain::Vec2d& mouse_position) const
{
    if (m_paintable_volumes.empty()) {
        return std::nullopt;
    }

    const Scene::Camera& camera = m_scene_presenter.scene().camera();
    const Scene::Ray ray = camera.ray_at(mouse_position.x(), mouse_position.y());

    Domain::Vec3d closest_hit_position = Domain::Vec3d::Zero();
    Domain::Vec3d closest_hit_normal = Domain::Vec3d::UnitZ();
    double closest_hit_squared_distance = std::numeric_limits<double>::max();
    size_t closest_facet_idx = 0;
    int closest_volume_idx = -1;

    for (const auto& paintable_volume : m_paintable_volumes) {
        const int volume_idx = &paintable_volume - &m_paintable_volumes.front();

        const std::optional<MeshRaycaster::UnprojectResult> unproject_result =
            MeshRaycaster::unproject_on_mesh(
                paintable_volume.aabb_mesh,
                ray,
                paintable_volume.world_trafo,
                std::nullopt,
                true
            );

        if (!unproject_result.has_value()) {
            continue;
        }

        double hit_squared_distance =
            (ray.origin - paintable_volume.world_trafo * unproject_result->position).squaredNorm();
        if (hit_squared_distance < closest_hit_squared_distance) {
            closest_hit_squared_distance = hit_squared_distance;
            closest_facet_idx = unproject_result->facet_idx;
            closest_volume_idx = volume_idx;
            closest_hit_position = unproject_result->position;
            closest_hit_normal = unproject_result->normal;
        }
    }

    if (closest_volume_idx == -1) {
        return std::nullopt;
    }

    VolumeHitPoint hit;
    hit.volume_hit_position = closest_hit_position;
    hit.volume_hit_normal = closest_hit_normal;
    hit.volume_idx = closest_volume_idx;
    hit.facet_idx = closest_facet_idx;
    return hit;
}

std::pair<Domain::Vec3d, Domain::Vec3d> SlaHollowGizmo::hit_to_object_pos_normal(const VolumeHitPoint& hit) const
{
    const auto& paintable_volume = m_paintable_volumes[hit.volume_idx];
    const Domain::Vec3d mesh_pos = paintable_volume.model_volume.get_matrix() * hit.volume_hit_position;

    // Use the raycast normal if available; otherwise compute facet normal from the scene mesh
    Domain::Vec3d mesh_normal = hit.volume_hit_normal;
    if (mesh_normal.norm() < 1e-6) {
        const Scene::TriangleMesh& scene_mesh = paintable_volume.scene_mesh;
        if (hit.facet_idx < scene_mesh.triangles().its.indices.size() / 3) {
            const auto& its = scene_mesh.triangles().its;
            const size_t i0 = its.indices[hit.facet_idx * 3 + 0];
            const size_t i1 = its.indices[hit.facet_idx * 3 + 1];
            const size_t i2 = its.indices[hit.facet_idx * 3 + 2];
            const Domain::Vec3f v0 = its.vertices[i0].cast<float>();
            const Domain::Vec3f v1 = its.vertices[i1].cast<float>();
            const Domain::Vec3f v2 = its.vertices[i2].cast<float>();
            Domain::Vec3f facet_normal = (v1 - v0).cross(v2 - v0);
            facet_normal.normalize();
            mesh_normal = paintable_volume.world_trafo_no_translate.linear() * facet_normal.cast<double>();
            mesh_normal.normalize();
        }
    } else {
        // Transform normal from world space to object mesh space
        mesh_normal = paintable_volume.world_trafo_no_translate.linear().inverse() * mesh_normal;
        mesh_normal.normalize();
    }

    return {mesh_pos, mesh_normal};
}

Scene::GizmoActivationState SlaHollowGizmo::on_mouse(Scene::GizmoEventContext& ctx, bool only_active)
{
    using namespace Slic3r::App::Platform;

    const MouseEvent& mouse_event = ctx.mouse_event();
    const Domain::Vec2d mouse_position = Domain::Vec2f(ctx.screen_mouse_x(), ctx.screen_mouse_y()).cast<double>();

    const bool is_left_button_event =
        (mouse_event.button() & MouseButton::Left) == MouseButton::Left;
    const bool is_right_button_event =
        (mouse_event.button() & MouseButton::Right) == MouseButton::Right;

    const bool ctrl_down  = (mouse_event.key_modifiers() & KeyModifiers(KeyModifier::Ctrl)) != 0;
    const bool shift_down = (mouse_event.key_modifiers() & KeyModifiers(KeyModifier::Shift)) != 0;

    if (m_paintable_volumes.empty()) {
        return Scene::GizmoActivationState::Inactive;
    }

    if (!m_edit_state.has_value()) {
        begin_editing();
    }

    const std::optional<VolumeHitPoint> hit_opt = raycast_mouse(mouse_position);
    const bool has_hit = hit_opt.has_value();

    // Track hovered hole (when not dragging)
    if (!m_edit_state->dragged_hole_idx.has_value() && has_hit) {
        const auto [mesh_pos, mesh_normal] = hit_to_object_pos_normal(*hit_opt);
        const double hover_radius = m_edit_state->editing.hole_radius_mm * 2.0;
        m_hovered_hole_idx = find_nearest_hole(mesh_pos, hover_radius);
    } else if (!has_hit || m_edit_state->dragged_hole_idx.has_value()) {
        m_hovered_hole_idx.reset();
    }

    // Left button down
    if (is_left_button_event && mouse_event.type() == MouseEvent::Type::ButtonDown) {
        // Ctrl+click: remove hole
        if (ctrl_down) {
            if (has_hit) {
                const auto [mesh_pos, mesh_normal] = hit_to_object_pos_normal(*hit_opt);
                const double removal_radius = m_edit_state->editing.hole_radius_mm * 2.0;
                if (auto idx = find_nearest_hole(mesh_pos, removal_radius); idx.has_value()) {
                    remove_hole_at_index(*idx);
                    return Scene::GizmoActivationState::Active;
                }
            }
            return Scene::GizmoActivationState::Inactive;
        }

        // Shift+click on hole: toggle selection
        if (shift_down && has_hit) {
            const auto [mesh_pos, mesh_normal] = hit_to_object_pos_normal(*hit_opt);
            const double selection_radius = m_edit_state->editing.hole_radius_mm * 2.0;
            if (auto idx = find_nearest_hole(mesh_pos, selection_radius); idx.has_value()) {
                m_edit_state->editing.toggle_hole(*idx);
                update_hole_visuals();
                return Scene::GizmoActivationState::Active;
            }
        }

        // Regular click on hole: select and start drag
        if (has_hit) {
            const auto [mesh_pos, mesh_normal] = hit_to_object_pos_normal(*hit_opt);
            const double selection_radius = m_edit_state->editing.hole_radius_mm * 2.0;
            if (auto idx = find_nearest_hole(mesh_pos, selection_radius); idx.has_value()) {
                clear_hole_selection();
                select_hole(*idx);
                m_edit_state->dragged_hole_idx = idx;
                m_edit_state->drag_start_world_pos = m_paintable_volumes[hit_opt->volume_idx].world_trafo * hit_opt->volume_hit_position;
                m_edit_state->drag_start_mesh_pos = mesh_pos;
                return Scene::GizmoActivationState::Active;
            } else {
                // Click on empty model surface: add hole
                clear_hole_selection();
                const auto [mesh_pos, mesh_normal] = hit_to_object_pos_normal(*hit_opt);
                add_hole_at_mesh_pos(mesh_pos, mesh_normal);
                return Scene::GizmoActivationState::Active;
            }
        }

        // Click on empty space: clear selection
        clear_hole_selection();
        update_hole_visuals();
        return Scene::GizmoActivationState::Inactive;
    }

    // Right button down: remove hole
    if (is_right_button_event && mouse_event.type() == MouseEvent::Type::ButtonDown) {
        if (has_hit) {
            const auto [mesh_pos, mesh_normal] = hit_to_object_pos_normal(*hit_opt);
            const double removal_radius = m_edit_state->editing.hole_radius_mm * 2.0;
            if (auto idx = find_nearest_hole(mesh_pos, removal_radius); idx.has_value()) {
                remove_hole_at_index(*idx);
                return Scene::GizmoActivationState::Active;
            }
        }
        return Scene::GizmoActivationState::Inactive;
    }

    // Mouse move during drag
    if (mouse_event.type() == MouseEvent::Type::Move && m_edit_state->dragged_hole_idx.has_value()) {
        if (has_hit) {
            const auto [mesh_pos, mesh_normal] = hit_to_object_pos_normal(*hit_opt);
            move_hole_to_mesh_pos(*m_edit_state->dragged_hole_idx, mesh_pos, mesh_normal);
            return Scene::GizmoActivationState::Active;
        }
        return Scene::GizmoActivationState::Inactive;
    }

    // Button up
    if ((is_left_button_event || is_right_button_event) && mouse_event.type() == MouseEvent::Type::ButtonUp) {
        if (m_edit_state->dragged_hole_idx.has_value()) {
            take_hole_undo_snapshot();
            m_edit_state->dragged_hole_idx.reset();
            update_hole_visuals();
            return Scene::GizmoActivationState::Active;
        }
        return Scene::GizmoActivationState::Inactive;
    }

    return Scene::GizmoActivationState::Inactive;
}

std::unique_ptr<GizmoWindow> SlaHollowGizmo::release_ui_window()
{
    return std::move(m_dialog);
}

void SlaHollowGizmo::render_scene(Render::CommandBuffer& cmd_buffer)
{
    // No per-frame updates needed for static preview
    (void)cmd_buffer;
}

// Visuals

void SlaHollowGizmo::update_hole_visuals()
{
    if (!m_edit_state.has_value() || m_holes_node == nullptr) {
        return;
    }

    const auto& holes = m_edit_state->editing.holes;
    if (holes.empty()) {
        clear_hole_visuals();
        return;
    }

    Scene::Scene& scene = m_scene_presenter.scene();

    // Get the instance transform
    const Domain::Project& project = m_project_interactor.selected_project();
    const Domain::ModelInstance* instance = project.find_instance_by_id(m_selected_object_id.id, m_selected_instance_id);
    if (!instance) {
        return;
    }
    const Domain::Transform3d instance_trafo = instance->get_matrix();

    // Cylinder geometry (shared for all holes, unit radius and height)
    create_cylinder_geometry_if_needed();
    const auto* cylinder_geom = m_geometry_manager.get(m_cylinder_geometry_id);
    if (!cylinder_geom) {
        return;
    }
    const auto* cylinder_trimesh = m_triangle_mesh_manager.get(m_cylinder_geometry_id);
    if (!cylinder_trimesh) {
        return;
    }

    // Clear existing hole nodes
    scene.remove_children([this](const Scene::Node* node) {
        return node->parent() == m_holes_node;
    }, m_holes_node);

    // Determine highlighted index (dragged or hovered)
    std::optional<size_t> highlighted_idx = m_edit_state->dragged_hole_idx;
    if (!highlighted_idx.has_value()) {
        highlighted_idx = m_hovered_hole_idx;
    }

    for (size_t i = 0; i < holes.size(); ++i) {
        const auto& hole = holes[i];
        const bool highlighted = highlighted_idx.has_value() && *highlighted_idx == i;
        const bool is_selected = m_edit_state->editing.selected_hole_indices.count(i) > 0;
        const bool is_failed = hole.failed;

        // Hole position in world space: instance_trafo * hole.pos (hole.pos is in mesh coords)
        Domain::Vec3d world_pos = instance_trafo * hole.pos.cast<double>();

        // Color based on hole state
        ColorRGBA color = get_hole_color(hole, highlighted || is_selected);

        Render::Material material = Render::Material{}
            .set_shader(m_device.context().shader_manager().shader("gouraud_light"))
            .set_uniform("uniform_color", color);

        // Transform: translate to world position, rotate to align with normal, scale to hole radius and height
        Eigen::Quaterniond q;
        Domain::Vec3d hole_normal = hole.normal.cast<double>();
        hole_normal.normalize();
        q.setFromTwoVectors(Domain::Vec3d::UnitZ(), hole_normal);

        Domain::Transform3d xform = Domain::Transform3d::Identity();
        xform.translate(world_pos);
        xform.rotate(q);
        xform.scale(Domain::Vec3d(static_cast<double>(hole.radius), static_cast<double>(hole.radius), static_cast<double>(hole.height)));

        Scene::NodeBuilder builder{scene};
        builder.set_debug_name(fmt::format("drain_hole_{}", i))
            .set_mesh(cylinder_geom, material, Scene::RenderLayerId(PlaterSceneLayer::GizmoHandles))
            .set_aabb(cylinder_trimesh->aabb_mesh())
            .set_transform(xform);

        scene.add_child(builder.build().release(), m_holes_node);
    }
}

void SlaHollowGizmo::clear_hole_visuals()
{
    if (m_holes_node != nullptr) {
        Scene::Scene& scene = m_scene_presenter.scene();
        scene.remove_children([this](const Scene::Node* node) {
            return node->parent() == m_holes_node;
        }, m_holes_node);
    }
}

Domain::ColorRGBA SlaHollowGizmo::get_hole_color(const Domain::SLA::DrainHole& hole, bool highlighted) const
{
    const auto& theme = AppServices::instance().theme();

    ColorRGBA base_color;
    if (hole.failed) {
        base_color = theme.color(Platform::Color::Error, Platform::ColorGroup::Default);
    } else {
        base_color = theme.color(Platform::Color::SlaDrainHole, Platform::ColorGroup::Default);
    }

    if (highlighted) {
        // Brighten for highlight
        return ColorRGBA{
            std::min(base_color.r() * 1.5f, 1.0f),
            std::min(base_color.g() * 1.5f, 1.0f),
            std::min(base_color.b() * 1.5f, 1.0f),
            base_color.a()
        };
    }

    return base_color;
}

void SlaHollowGizmo::create_cylinder_geometry_if_needed()
{
    if (m_cylinder_geometry_created) {
        return;
    }

    static constexpr double CYLINDER_RESOLUTION_ANGLE = Slic3r::deg2rad(360.0 / 32.0);
    Domain::TriangleMesh mesh = Biz::Algorithms::TriangleMesh::make_cylinder(1.0, 1.0, CYLINDER_RESOLUTION_ANGLE);
    auto cylinder_trimesh = std::make_unique<Scene::TriangleMesh>(std::move(mesh.its));
    const auto* cylinder_geom = m_geometry_manager.get_or_create(m_cylinder_geometry_id, [&]() {
        return Render::geometry_from_triangle_mesh(m_device, cylinder_trimesh->triangles());
    });
    (void)cylinder_geom;
    m_cylinder_geometry_created = true;
}

} // namespace Slic3r::App::Plater