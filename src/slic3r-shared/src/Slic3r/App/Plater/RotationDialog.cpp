#include "Slic3r/App/Plater/RotationDialog.hpp"
#include "Slic3r/App/Plater/AutoOrientProgress.hpp"
#include "Slic3r/App/Plater/PlaceOnBedButton.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/RadioButton.hpp"
#include "Slic3r/App/Yoga/ProgressBar.hpp"
#include "Slic3r/App/Plater/TripleInput.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/App/Plater/PlaterGizmosHelper.hpp"
#include "Slic3r/Math.hpp"
#include "Slic3r/App/IsSlaActive.hpp"
#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "libslic3r/SLAAutoOrient.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/LegacyFormat.hpp"
#include <Eigen/Geometry>

#include <atomic>
#include <exception>
#include <utility>

#include <fmt/format.h>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App::Plater {

using Biz::_u8L;
using Yoga::Orientation;
using Yoga::Text;
using Domain::BoundingBox3d;
using Domain::SquareMatrix4d;
using Domain::SquareMatrix3d;
using Domain::Vec3d;
using Domain::Transform3d;

namespace {

// The auto orientation goal is remembered for the whole session. This window is rebuilt every time
// the Rotate panel is opened, so the choice cannot be kept in a member of RotationDialog.
Slic3r::sla::AutoOrientGoal& auto_orient_goal()
{
    static Slic3r::sla::AutoOrientGoal goal{Slic3r::sla::AutoOrientGoal::LeastSupports};
    return goal;
}

// One auto orientation search per project, so a second press replaces the first one and the job
// manager cancels the jobs of a project that is closed. The name is also the prefix the pop
// notification of the search is registered under.
std::string auto_orient_job_name(Domain::SelectionId project_id)
{
    return fmt::format("sla_auto_orient {}", project_id);
}

// What the search answers with. A search that was cancelled before it found a pose, or one that
// found nothing to rotate, has no rotation to apply.
struct AutoOrientResult
{
    Domain::Vec2d rotation{Domain::Vec2d::Zero()};
    bool has_rotation{false};
};

// The search itself, on a worker thread. The mesh is a copy taken out of the model on the UI
// thread, so nothing here reaches into the project while the user works on it.
//
// The engine asks the status callback for every pose it scores, and it scores them in parallel, so
// the percentages can arrive out of order. The progress tracker only ever moves forward, so the
// high water mark is kept here: a percentage that is behind the last one is dropped rather than
// sent back, which the tracker would not accept.
//
// The engine counts in per cent and the tracker in a fraction, so every percentage goes through
// AutoOrientProgress.hpp on its way in.
AutoOrientResult run_auto_orient(
    Biz::JThread::StopToken stop_token,
    Biz::Platform::JobManager::ProgressTracker progress,
    Slic3r::Domain::TriangleMesh mesh,
    Slic3r::sla::AutoOrientGoal goal
)
{
    AutoOrientResult result;

    std::atomic_int reported{-1};
    const Slic3r::sla::AutoOrientStatus status{[&](int percent) {
        if (percent >= 0) {
            int last = reported.load();
            while (percent > last && !reported.compare_exchange_weak(last, percent)) {
            }
            if (percent > last)
                progress.set(auto_orient_progress_fraction(percent));
        }
        return !stop_token.stop_requested();
    }};

    result.rotation     = Slic3r::sla::auto_orient(mesh, goal, status);
    result.has_rotation = !stop_token.stop_requested();

    return result;
}

} // namespace

RotationDialog::RotationDialog(
    App::Plater::PlaterScenePresenter& scene_provider,
    Biz::ProjectInteractor& project_interactor
) :
    GizmoWindow(),
    m_scene_provider{scene_provider},
    m_project_interactor{project_interactor},
    m_projects{project_interactor}
{
    m_scene_provider.add_listener<App::Plater::ISelectionExtentsChangedListener>(this);
    m_project_interactor.add_listener<Biz::ISelectedProjectChangedListener>(this);
    m_project_interactor.scene_interactor()
        .add_listener<Biz::Scene::ISceneSelectionChangedListener>(this);
    Biz::Platform::PlatformServices::instance().job_manager().add_listener<
        Biz::Platform::JobManager::IJobManagerStatusChangedListener>(this);

    content()->set_padding({20_fpx, 20_fpx});
    content()->set_orientation(Yoga::Orientation::Vertical);
    content()->set_gap(20_fpx);

    revert_button()->callbacks().action = [this]()
    {
        ProjectContext& project_context{m_projects.selected()};
        if (project_context.reset_rotation_candidates.empty()) {
            return;
        }
        const std::optional<Biz::Scene::SelectionExtents> selection_bounding_box{
            m_project_interactor.scene_interactor().selection_bounding_box()
        };
        const bool was_floating{selection_bounding_box && selection_bounding_box->is_floating()};
        m_project_interactor.scene_interactor().set_element_transforms(
            project_context.reset_rotation_candidates
        );
        if (selection_bounding_box && !was_floating) {
            Domain::SquareMatrix4d relative_transform_world{Domain::SquareMatrix4d::Identity()};
            relative_transform_world.col(3).z() =
                -m_project_interactor.scene_interactor().selection_bounding_box()->min_z();
            m_project_interactor.scene_interactor().transform_selection(relative_transform_world);
        }

        m_project_interactor.undo_provider().take_snapshot(
            Biz::UndoSnapshotType::RevertRotation
        );
    };

    auto rotation_section{content()->emplace_back<Yoga::Item>()};
    rotation_section->set_orientation(Orientation::Vertical);
    rotation_section->set_gap(10_fpx);

    auto title{rotation_section->emplace_back<Text>("Relative rotation")};
    title->set_font_type(Render::ImguiFontType::Bold);

    m_relative_input = rotation_section->emplace_back<TripleInput>(_u8L("°"));
    m_relative_input->on_change = [this](const Domain::Vec3d& value, int index)
    { add_rotation(Vec3d{deg2rad(value(0)), deg2rad(value(1)), deg2rad(value(2))}); };

    m_place_on_bed_button = rotation_section->emplace_back<PlaceOnBedButton>(m_project_interactor);

    m_auto_orient_button = rotation_section->emplace_back<Yoga::LayoutButton>(_u8L("Auto orient"));
    m_auto_orient_button->callbacks().action = [this]() { on_auto_orient(); };

    m_auto_orient_progress = rotation_section->emplace_back<Yoga::ProgressBar>();
    m_auto_orient_progress->set_show_overlay(true);
    m_auto_orient_progress->set_min_height(3_fpx);
    m_auto_orient_progress->set_flex_shrink(0.f);
    m_auto_orient_progress->set_visible(false);

    m_auto_orient_goal_row = rotation_section->emplace_back<Yoga::Item>();
    m_auto_orient_goal_row->set_orientation(Orientation::Horizontal);
    // The goals do not always fit on one line.
    m_auto_orient_goal_row->set_flex_wrap(YGWrapWrap);
    m_auto_orient_goal_row->set_gap(10_fpx);
    m_lowest_height_button =
        m_auto_orient_goal_row->emplace_back<Yoga::RadioButton>(_u8L("Lowest height"));
    m_auto_orient_goal_buttons.insert_button(m_lowest_height_button);
    m_fewest_supports_button =
        m_auto_orient_goal_row->emplace_back<Yoga::RadioButton>(_u8L("Fewest supports"));
    m_auto_orient_goal_buttons.insert_button(m_fewest_supports_button);
    m_least_peel_button =
        m_auto_orient_goal_row->emplace_back<Yoga::RadioButton>(_u8L("Least peel"));
    m_auto_orient_goal_buttons.insert_button(m_least_peel_button);
    m_no_cups_button = m_auto_orient_goal_row->emplace_back<Yoga::RadioButton>(_u8L("No cups"));
    m_auto_orient_goal_buttons.insert_button(m_no_cups_button);
    m_miniature_button =
        m_auto_orient_goal_row->emplace_back<Yoga::RadioButton>(_u8L("Miniature / bust"));
    m_auto_orient_goal_buttons.insert_button(m_miniature_button);
    reload_auto_orient_goal();
    m_auto_orient_goal_buttons.callbacks().checked_changed = [this](Yoga::AbstractButton*, Yoga::AbstractButton*)
    {
        if (m_lowest_height_button->checked()) {
            auto_orient_goal() = Slic3r::sla::AutoOrientGoal::MinHeight;
        } else if (m_fewest_supports_button->checked()) {
            auto_orient_goal() = Slic3r::sla::AutoOrientGoal::LeastSupports;
        } else if (m_least_peel_button->checked()) {
            auto_orient_goal() = Slic3r::sla::AutoOrientGoal::LeastPeel;
        } else if (m_no_cups_button->checked()) {
            auto_orient_goal() = Slic3r::sla::AutoOrientGoal::NoCups;
        } else if (m_miniature_button->checked()) {
            auto_orient_goal() = Slic3r::sla::AutoOrientGoal::Miniature;
        }
    };

    add_separator(content());

    m_reference_frame_picker = content()->emplace_back<ReferenceFramePicker>(
        m_project_interactor,
        Biz::Scene::SelectionReferenceFrame::Volume
    );
}

RotationDialog::~RotationDialog()
{
    // The search applies its result through this window, so a search that is still running has to
    // be gone before the window is. cancel_job() waits for the worker thread, which asks whether to
    // go on between the poses it scores.
    if (!m_auto_orient_job_name.empty()) {
        Biz::Platform::PlatformServices::instance().job_manager().cancel_job(
            m_auto_orient_job_name
        );
        m_auto_orient_job_name.clear();
    }

    m_scene_provider.remove_listener<App::Plater::ISelectionExtentsChangedListener>(this);
    m_project_interactor.remove_listener<Biz::ISelectedProjectChangedListener>(this);
    m_project_interactor.scene_interactor()
        .remove_listener<Biz::Scene::ISceneSelectionChangedListener>(this);
    Biz::Platform::PlatformServices::instance().job_manager().remove_listener<
        Biz::Platform::JobManager::IJobManagerStatusChangedListener>(this);
}

void RotationDialog::on_scene_selection_bounding_box_changed(
    Domain::SelectionId project_id,
    const std::optional<Biz::Scene::SelectionExtents>&
) {
    reload(project_id);
}

void RotationDialog::on_selected_project_changed_final(size_t index) {
    reload(index);
}

void RotationDialog::on_scene_selection_changed(
    Domain::SelectionId project_id,
    const Biz::Scene::ObjectSelection& selection
) {
    reload(project_id);
}

void RotationDialog::on_activated(Domain::SelectionId project_id) {
    m_projects.selected().activated = true;
    m_reference_frame_picker->on_activated();
    reload(project_id);
}

void RotationDialog::on_deactivated() {
    m_reference_frame_picker->on_deactivated();
    m_projects.selected().activated = false;
}

PlaceOnBedButton& RotationDialog::place_on_bed_button() {
    return *m_place_on_bed_button;
}

void RotationDialog::reload(std::optional<Domain::SelectionId> project_id) {
    ProjectContext& project_context{m_projects.selected()};
    if (!project_context.activated) {
        return;
    }
    if (project_id && project_id != m_project_interactor.selected_project_id()) {
        return;
    }

    m_relative_input->set_value({0, 0, 0});

    project_context.reset_rotation_candidates = get_reset_rotation_candidates();

    if (project_context.reset_rotation_candidates.empty()) {
        revert_button()->set_visible(false);
    } else {
        revert_button()->set_visible(true);
    }

    if (m_project_interactor.scene_interactor().object_selection().contains_wipe_tower()) {
        m_relative_input->set_visible({false, false, true});
    } else {
        m_relative_input->set_visible({true, true, true});
    }
    update_auto_orient_button_visibility();
}

void RotationDialog::update_auto_orient_button_visibility()
{
    const bool is_sla = App::is_sla_active(m_project_interactor);
    m_auto_orient_button->set_visible(is_sla);
    m_auto_orient_goal_row->set_visible(is_sla);
}

void RotationDialog::reload_auto_orient_goal()
{
    switch (auto_orient_goal()) {
    case Slic3r::sla::AutoOrientGoal::MinHeight:
        m_lowest_height_button->set_checked(true);
        break;
    case Slic3r::sla::AutoOrientGoal::LeastPeel:
        m_least_peel_button->set_checked(true);
        break;
    case Slic3r::sla::AutoOrientGoal::NoCups:
        m_no_cups_button->set_checked(true);
        break;
    case Slic3r::sla::AutoOrientGoal::LeastSupports:
        m_fewest_supports_button->set_checked(true);
        break;
    case Slic3r::sla::AutoOrientGoal::Miniature:
        m_miniature_button->set_checked(true);
        break;
    }
}

void RotationDialog::on_job_manager_status_changed(
    const Biz::Platform::JobManager::JobManagerStatus& status
)
{
    if (m_auto_orient_job_name.empty())
        return;

    const auto it{status.find(m_auto_orient_job_name)};
    if (it == status.end() || it->second.status != Domain::JobStatus::Started) {
        // The search is gone from the manager, which is what happens once it is finished and when
        // its project was closed: either way the panel goes back to what it shows normally.
        m_auto_orient_job_name.clear();
        reload_auto_orient_status();
        return;
    }

    m_auto_orient_progress_percent =
        it->second.percent ? auto_orient_progress_percent(it->second.percent.value()) : 0;
    reload_auto_orient_status();
}

void RotationDialog::reload_auto_orient_status()
{
    const bool running{!m_auto_orient_job_name.empty()};

    m_auto_orient_progress->set_visible(running);
    if (!running) {
        m_auto_orient_progress_percent = 0;
        m_auto_orient_button->set_label(_u8L("Auto orient"));
        m_auto_orient_button->callbacks().action = [this]() { on_auto_orient(); };
        return;
    }

    m_auto_orient_button->set_label(_u8L("Cancel"));
    m_auto_orient_button->callbacks().action = [this]() { on_cancel_auto_orient(); };
    m_auto_orient_progress->set_progress(
        m_auto_orient_progress_percent,
        Slic3r::format(_u8L("Auto orient %1%/100"), m_auto_orient_progress_percent)
    );
}

void RotationDialog::on_cancel_auto_orient()
{
    if (m_auto_orient_job_name.empty())
        return;

    // Ask the search to stop between the poses it scores; it applies nothing when it stops.
    Biz::Platform::PlatformServices::instance().job_manager().request_job_stop(
        m_auto_orient_job_name
    );
}

void RotationDialog::on_auto_orient()
{
    using namespace Biz::Scene;
    using Domain::ElementRef;
    using Domain::ModelInstance;
    using Domain::ModelObject;

    if (!App::is_sla_active(m_project_interactor)) {
        return;
    }

    Biz::Scene::SceneInteractor& scene_interactor{m_project_interactor.scene_interactor()};
    const ObjectSelection& selection{scene_interactor.object_selection()};

    // Check if exactly one whole instance is selected (WholeInstance state)
    const SelectionState state{selection.state()};
    if (state != SelectionState::WholeInstance) {
        return;
    }

    // Get the selected element (should be exactly one)
    if (selection.elements.size() != 1) {
        return;
    }

    const ElementRef& element{selection.elements.front()};
    ASSERT(element.has_instance());

    const Domain::SelectionId project_id{m_project_interactor.selected_project_id()};
    const Domain::Project& project{m_project_interactor.workbench().project(project_id)};

    const ModelInstance* instance{project.find_instance_by_id(element.object_id, element.instance_id)};
    if (!instance) {
        return;
    }

    const ModelObject* object{instance->get_object()};
    if (!object) {
        return;
    }

    // The mesh the engine searches is taken out of the model here, on the UI thread, so that the
    // worker thread has a copy of its own and nothing to read while the user works on the project.
    const Domain::TriangleMesh mesh{Slic3r::sla::auto_orient_mesh(*object)};
    if (mesh.its.vertices.empty()) {
        return;
    }

    m_auto_orient_job_name = auto_orient_job_name(project_id);
    const std::string job_name{m_auto_orient_job_name};
    m_auto_orient_progress_percent = 0;
    Biz::Platform::PlatformServices::instance().job_manager()
        .create_job(job_name, run_auto_orient, std::move(mesh), auto_orient_goal())
        .set_project_id(project_id)
        .on_result([this, job_name, element](AutoOrientResult result)
                   {
                       if (m_auto_orient_job_name != job_name)
                           return; // a newer search took over while this one was finishing
                       if (result.has_rotation) {
                           apply_auto_orient_rotation(result.rotation, element);
                       }
                       m_auto_orient_job_name.clear();
                       reload_auto_orient_status();
                   })
        .on_exception([this, job_name](const std::exception_ptr&)
                      {
                          if (m_auto_orient_job_name != job_name)
                              return;
                          m_auto_orient_job_name.clear();
                          reload_auto_orient_status();
                      })
        .start();

    reload_auto_orient_status();
}

void RotationDialog::apply_auto_orient_rotation(
    const Domain::Vec2d& target_rot_sla, const Domain::ElementRef& element
)
{
    using namespace Biz::Scene;
    using Domain::ModelInstance;
    using Domain::SquareMatrix3d;
    using Domain::SquareMatrix4d;
    using Domain::Vec3d;
    using Eigen::AngleAxisd;

    Biz::Scene::SceneInteractor& scene_interactor{m_project_interactor.scene_interactor()};

    // The search ran on a worker thread, so the selection may have moved on while it did. The
    // rotation belongs to the element the search was started for, and to nothing else.
    const ObjectSelection& selection{scene_interactor.object_selection()};
    if (selection.elements.size() != 1 || selection.elements.front() != element) {
        return;
    }

    const ModelInstance* instance{
        m_project_interactor.workbench()
            .project(m_project_interactor.selected_project_id())
            .find_instance_by_id(element.object_id, element.instance_id)
    };
    if (!instance) {
        return;
    }

    const double rot_x{target_rot_sla.x()};
    const double rot_y{target_rot_sla.y()};

    // Build target rotation matrix: R_target = Ry(y) * Rx(x)
    const SquareMatrix3d R_target{
        AngleAxisd(rot_y, Vec3d::UnitY()) * AngleAxisd(rot_x, Vec3d::UnitX())
    };

    // Get current instance rotation matrix (3x3)
    const Transform3d instance_matrix{instance->get_matrix()};
    const SquareMatrix3d R_current{instance_matrix.rotation()};

    // Compute relative rotation: R_target * R_current^-1 = R_target * R_current^T
    const SquareMatrix3d R_relative{R_target * R_current.transpose()};

    // Get selection bounding box center
    const std::optional<SelectionExtents> selection_bounding_box{scene_interactor.selection_bounding_box()};
    if (!selection_bounding_box) {
        return;
    }
    const Vec3d center{selection_bounding_box->oriented_bounding_box().center};

    // Build relative world transform: T(c) * R_relative * T(-c)
    SquareMatrix4d relative_transform_world{SquareMatrix4d::Identity()};
    relative_transform_world.block<3, 3>(0, 0) = R_relative;
    relative_transform_world.block<3, 1>(0, 3) = center - R_relative * center;

    // Apply transform and place on bed
    scene_interactor.transform_selection(relative_transform_world, true);

    // Take undo snapshot
    m_project_interactor.undo_provider().take_snapshot(Biz::UndoSnapshotType::Rotate);
}

void RotationDialog::add_rotation(Domain::Vec3d rotate_by_rads)
{
    const std::optional<Biz::Scene::SelectionExtents> selection_bounding_box{
        m_project_interactor.scene_interactor().selection_bounding_box()
    };
    if (!selection_bounding_box) {
        return;
    }

    const Biz::Scene::OrientedBoundingBox& bounding_box{selection_bounding_box->oriented_bounding_box()};

    rotate_by_rads(1) = -rotate_by_rads(1);

    const bool was_floating{selection_bounding_box->is_floating()};
    Biz::Scene::SceneInteractor& scene_interactor{m_project_interactor.scene_interactor()};
    scene_interactor.transform_selection(
        get_rotation_matrix(
            bounding_box.rotation,
            bounding_box.center,
            rotate_by_rads
        ),
        !was_floating
    );
    m_project_interactor.undo_provider().take_snapshot(Biz::UndoSnapshotType::SetRotation);
}

Domain::SquareMatrix4d remove_rotation(
    const Domain::Transform3d& matrix,
    const Domain::Vec3d& center
)
{
    Domain::SquareMatrix4d result{matrix.matrix()};
    result.col(3).head<3>() -= center;

    const SquareMatrix3d rotation{matrix.rotation()};
    SquareMatrix4d inverse_rotation{SquareMatrix4d::Identity()};
    inverse_rotation.block(0, 0, 3, 3) = rotation.transpose();
    result = inverse_rotation * result;

    result.col(3).head<3>() += center;

    return result;
}

Domain::SquareMatrix4d remove_rotation(
    const Domain::Transform3d& volume_trafo,
    const Domain::Transform3d& instance_trafo,
    const Domain::Vec3d& center
)
{
    Domain::SquareMatrix3d linear_part{volume_trafo.rotation().transpose()};

    Domain::SquareMatrix4d local{Domain::SquareMatrix4d::Identity()};
    local.block<3, 3>(0, 0) = linear_part;
    local.block<3, 1>(0, 3) = center - linear_part * center;

    local = (instance_trafo.inverse() * local * instance_trafo).matrix();

    return (local * volume_trafo).matrix();
}

Biz::Scene::SceneInteractor::ElementTransforms RotationDialog::get_reset_rotation_candidates() const
{
    Biz::Scene::SceneInteractor& scene_interactor{m_project_interactor.scene_interactor()};
    Biz::Scene::SceneInteractor::ElementTransforms result;

    using Biz::Scene::SelectionState;
    const SelectionState state{scene_interactor.object_selection().state()};

    if (state != SelectionState::SingleVolume
        && state != SelectionState::MultipleVolumes
        && state != SelectionState::WholeInstance)
    {
        return {};
    }

    std::optional<Biz::Scene::SelectionExtents> selection_bounding_box{
        m_project_interactor.scene_interactor().selection_bounding_box()
    };
    if (!selection_bounding_box) {
        return {};
    }

    const Biz::Scene::OrientedBoundingBox& bounding_box{selection_bounding_box->oriented_bounding_box()};

    for (const Domain::ElementRef& element : scene_interactor.object_selection().elements) {
        ASSERT(element.has_instance());

        const Domain::ModelInstance* instance{
            m_project_interactor.workbench()
                .project(m_project_interactor.selected_project_id())
                .find_instance_by_id(element.object_id, element.instance_id)
        };
        const Domain::Transform3d instance_matrix{instance->get_matrix()};

        if (element.has_volume()) {
            const Domain::ElementRef ref{element.object_id, 0, element.volume_id};
            const Domain::ModelVolume* volume{
                m_project_interactor.workbench()
                    .project(m_project_interactor.selected_project_id())
                    .find_volume_by_id(element.object_id, element.volume_id)
            };
            const Domain::Transform3d volume_matrix{volume->get_matrix()};

            const SquareMatrix4d no_rotation{
                remove_rotation(volume_matrix, instance_matrix, bounding_box.center)
            };
            if (!volume_matrix.matrix().isApprox(no_rotation)) {
                result.insert_or_assign(ref, no_rotation);
            }
        } else {
            const Domain::ElementRef instance_ref{element.object_id, instance->id().id};
            const SquareMatrix4d instance_no_rotation{
                remove_rotation(instance_matrix, bounding_box.center)
            };
            if (!instance_matrix.matrix().isApprox(instance_no_rotation)) {
                result.insert_or_assign(instance_ref, instance_no_rotation);
            }

            for (const Domain::ModelVolume* volume : instance->get_object()->volumes) {
                const Domain::ElementRef volume_ref{element.object_id, 0, volume->id().id};
                const Domain::Transform3d volume_matrix{volume->get_matrix()};
                const SquareMatrix4d no_rotation{remove_rotation(
                    volume_matrix,
                    Domain::Transform3d{instance_no_rotation},
                    bounding_box.center
                )};
                if (!volume_matrix.matrix().isApprox(no_rotation)) {
                    result.insert_or_assign(volume_ref, no_rotation);
                }
            }
        }
    }

    return result;
}
} // namespace Slic3r::App::Plater
