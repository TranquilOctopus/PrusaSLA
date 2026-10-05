#pragma once

#include <cstddef>
#include <vector>

#include "Slic3r/App/Yoga/Window.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"
#include "Slic3r/Biz/ISelectedConfigContainerChangedListener.hpp"
#include "Slic3r/Biz/ISelectedProjectChangedListener.hpp"
#include "Slic3r/Biz/ISelectedBedInstanceChangedListener.hpp"
#include "Slic3r/Biz/ISlicingInputChangedListener.hpp"
#include "Slic3r/Biz/Slicing/SlicingInteractor.hpp"
#include "Slic3r/Domain/ObjectID.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/App/Preview/SlaSupportsPanel.hpp"
#include "Slic3r/App/IsSlaActive.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
class SLAResultCache;
} // namespace Slic3r::Biz

namespace Slic3r::Domain {
class ModelObject;
class ModelInstance;
} // namespace Slic3r::Domain

namespace Slic3r::App {
class Navigator;
} // namespace Slic3r::App

namespace Slic3r::App::Yoga {
class LayoutButton;
class Text;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App::Preview {

/// The Preview sidebar section that makes the support step of the SLA workflow (M2.17d) doable from
/// Preview: it lists the models of the build plate with their support points, offers Auto support
/// on the selected models and on all of them (the M2.17b action of the support tool) and opens the
/// support tool for the manual points. Nothing here slices, the only Slice button is the one of the
/// action bar, which the section points at once every model has its points.
class SidebarSlaSupports :
    public Yoga::Window,
    public Biz::ISelectedConfigContainerChangedListener,
    public Biz::ISelectedProjectChangedListener,
    public Biz::ISelectedBedInstancesChangedListener,
    public Biz::ISlicingInputChangedListener
{
public:
    SidebarSlaSupports(Biz::ProjectInteractor& project_interactor, App::Navigator* navigator);

    void on_selected_config_container_changed(Domain::SelectionId project_id, Domain::SelectionId container_id) override;
    void on_selected_project_changed(size_t index) override;
    void on_selected_bed_instances_changed(Domain::SelectionId project_id, const Biz::Scene::BedSelection& bed_selection) override;
    void on_slicing_input_changed(const Domain::BedRef& bed_instance) override;
    void on_slicing_input_removed(const Domain::BedRef& bed_instance) override;

protected:
    void render_body(const Domain::Vec2f& pos, const Domain::Vec2f& size) override;

private:
    void refresh();
    void update_visibility();
    void update_controls();
    void edit_supports();
    void auto_support(bool selected_only);

    /// Removes the support points of the selected models or of every listed one (M2.32), after the
    /// yes/no dialog that names how many points of how many models go.
    void clear_supports(bool selected_only);

    /// Models having an instance on the bed selected in the scene, in model order.
    std::vector<const Domain::ModelObject*> listed_objects() const;

    /// The listed models a printable instance of which sits on a build plate, in model order. Only
    /// those can get support points, so they are what the actions of the section work on.
    std::vector<const Domain::ModelObject*> listed_printable_objects() const;

    /// The instance of @p object the support tool works on, a printable one on a build plate, or
    /// nullptr when the model has none.
    const Domain::ModelInstance* supportable_instance(const Domain::ModelObject* object) const;

    /// The listed models the scene has selected, or the one the section would edit, so that Auto
    /// support selected always has something to work on.
    std::vector<const Domain::ModelObject*> selected_printable_objects() const;

    /// Model to edit - the one selected in the scene, or the first listed one.
    const Domain::ModelObject* edited_object() const;

    /// Whether the support tool of the Prepare view is generating support points.
    bool auto_support_running() const;

    /// How many of the listed models the raft type Auto put a raft under because their underside
    /// would form a suction cup (M7.8.4). It is the support preview that read the undersides, on its
    /// worker, and the navigator is how this asks for what it decided.
    std::size_t models_with_auto_raft() const;

    /// Selects the first printable instance of every one of @p objects and opens the support tool on
    /// them, which is where the generation of Auto support lives.
    void open_support_tool(const std::vector<const Domain::ModelObject*>& objects);

    Biz::ProjectInteractor& m_project_interactor;
    App::Navigator* m_navigator{nullptr};
    Biz::ListenerScope<Biz::ISelectedConfigContainerChangedListener, Biz::ProjectInteractor, SidebarSlaSupports> m_config_container_listener_scope;
    Biz::ListenerScope<Biz::ISelectedProjectChangedListener, Biz::ProjectInteractor, SidebarSlaSupports> m_project_listener_scope;
    Biz::ListenerScope<Biz::ISelectedBedInstancesChangedListener, Biz::Scene::SceneInteractor, SidebarSlaSupports> m_bed_selection_listener_scope;
    Biz::ListenerScope<Biz::ISlicingInputChangedListener, Biz::Scene::SceneInteractor, SidebarSlaSupports> m_slicing_input_listener_scope;

    Domain::SelectionId m_current_project_id{Domain::INVALID_ID};
    Yoga::Item* m_rows_container{nullptr};
    Yoga::LayoutButton* m_edit_supports_button{nullptr};
    Yoga::LayoutButton* m_auto_support_selected_button{nullptr};
    Yoga::LayoutButton* m_auto_support_all_button{nullptr};
    Yoga::LayoutButton* m_clear_selected_button{nullptr};
    Yoga::LayoutButton* m_clear_all_button{nullptr};
    Yoga::Text* m_status_text{nullptr};
    /// The one line the Auto raft rule gets under the status (M7.8.4), hidden whenever no model on
    /// this plate is given a raft by it.
    Yoga::Text* m_auto_raft_text{nullptr};
    /// Set when a generation was started from here and cleared as soon as the support tool reports
    /// it is not running any more, which is also how the section recovers from a cancelled run.
    bool m_auto_support_running{false};
};

} // namespace Slic3r::App::Preview
