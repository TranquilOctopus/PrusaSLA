#pragma once

#include "Slic3r/App/Plater/PlaceOnBedButton.hpp"
#include "Slic3r/App/Plater/PlaterScenePresenter.hpp"
#include "Slic3r/App/Plater/ReferenceFramePicker.hpp"
#include "Slic3r/App/Plater/GizmoWindow.hpp"
#include "Slic3r/Biz/Platform/JobManager/IJobManagerStatusChangedListener.hpp"
#include "Slic3r/Biz/ProjectScoped.hpp"
#include "Slic3r/Domain/ElementRef.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"

#include <string>

namespace Slic3r::Biz {
    class ProjectInteractor;
}

namespace Slic3r::App::Yoga {
    class RadioButton;
    class ProgressBar;
}

namespace Slic3r::App::Plater {
class TripleInput;

class RotationDialog final :
    public GizmoWindow,
    public App::Plater::ISelectionExtentsChangedListener,
    public Biz::ISelectedProjectChangedListener,
    public Biz::Scene::ISceneSelectionChangedListener,
    public Biz::Platform::JobManager::IJobManagerStatusChangedListener
{
public:
    RotationDialog(
        App::Plater::PlaterScenePresenter& scene_provider,
        Biz::ProjectInteractor& project_interactor
    );

    ~RotationDialog();

    void on_scene_selection_bounding_box_changed(
        Domain::SelectionId project_id,
        const std::optional<Biz::Scene::SelectionExtents>&
    ) override;

    void on_selected_project_changed_final(size_t index) override;

    void on_scene_selection_changed(
        Domain::SelectionId project_id,
        const Biz::Scene::ObjectSelection& selection
    ) override;

    void on_activated(Domain::SelectionId project_id);
    void on_deactivated();
    PlaceOnBedButton& place_on_bed_button();

    // The auto orientation search reports into the job manager (M4.12), so the button can turn into
    // a cancel button and the progress bar can fill while it runs.
    void on_job_manager_status_changed(
        const Biz::Platform::JobManager::JobManagerStatus& status
    ) override;

private:
    std::optional<Domain::Vec3d> get_obb_rotation() const;
    void reload(std::optional<Domain::SelectionId> project_id = std::nullopt);
    void update_auto_orient_button_visibility();
    void reload_auto_orient_goal();
    void reload_auto_orient_status();
    App::Plater::PlaterScenePresenter& m_scene_provider;
    Biz::ProjectInteractor& m_project_interactor;
    TripleInput* m_relative_input;
    PlaceOnBedButton* m_place_on_bed_button{nullptr};
    Yoga::LayoutButton* m_auto_orient_button{nullptr};
    Yoga::ProgressBar* m_auto_orient_progress{nullptr};
    Yoga::Item* m_auto_orient_goal_row{nullptr};
    Yoga::RadioButton* m_lowest_height_button{nullptr};
    Yoga::RadioButton* m_fewest_supports_button{nullptr};
    Yoga::RadioButton* m_least_peel_button{nullptr};
    Yoga::RadioButton* m_no_cups_button{nullptr};
    Yoga::ButtonGroup m_auto_orient_goal_buttons;
    ReferenceFramePicker* m_reference_frame_picker;

    // The auto orientation search of the selected project, as it runs or after it ended. Empty when
    // nothing is running. The search reports into the job manager under this name.
    std::string m_auto_orient_job_name;

    // How far the running search got, as the job manager reported it, 0..100.
    int m_auto_orient_progress_percent{0};

    struct ProjectContext {
        bool activated{false};
        Biz::Scene::SceneInteractor::ElementTransforms reset_rotation_candidates;
    };

    using ProjectContexts = Biz::ProjectScoped<ProjectContext>;
    ProjectContexts m_projects;

    void add_rotation(Domain::Vec3d rotate_by_rads);
    Biz::Scene::SceneInteractor::ElementTransforms get_reset_rotation_candidates() const;
    void on_auto_orient();
    void on_cancel_auto_orient();
    void apply_auto_orient_rotation(const Domain::Vec2d& rotation, const Domain::ElementRef& element);
};
}
