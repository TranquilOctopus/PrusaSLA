#pragma once

#include "Slic3r/App/Yoga/Window.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"
#include "Slic3r/Biz/ISelectedConfigContainerChangedListener.hpp"
#include "Slic3r/Biz/ISelectedProjectChangedListener.hpp"
#include "Slic3r/Biz/ISelectedBedInstanceChangedListener.hpp"
#include "Slic3r/Biz/Slicing/SlicingInteractor.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/App/IsSlaActive.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
class SLAResultCache;
} // namespace Slic3r::Biz

namespace Slic3r::App {
class Navigator;
} // namespace Slic3r::App

namespace Slic3r::App::Yoga {
class LayoutButton;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App::Preview {

class SidebarSlaSupports :
    public Yoga::Window,
    public Biz::ISelectedConfigContainerChangedListener,
    public Biz::ISelectedProjectChangedListener,
    public Biz::ISelectedBedInstancesChangedListener
{
public:
    SidebarSlaSupports(Biz::ProjectInteractor& project_interactor, App::Navigator* navigator);

    void on_selected_config_container_changed(Domain::SelectionId project_id, Domain::SelectionId container_id) override;
    void on_selected_project_changed(size_t index) override;
    void on_selected_bed_instances_changed(Domain::SelectionId project_id, const Biz::Scene::BedSelection& bed_selection) override;

private:
    void refresh();
    void update_visibility();

    Biz::ProjectInteractor& m_project_interactor;
    App::Navigator* m_navigator{nullptr};
    Biz::ListenerScope<Biz::ISelectedConfigContainerChangedListener, Biz::ProjectInteractor, SidebarSlaSupports> m_config_container_listener_scope;
    Biz::ListenerScope<Biz::ISelectedProjectChangedListener, Biz::ProjectInteractor, SidebarSlaSupports> m_project_listener_scope;
    Biz::ListenerScope<Biz::ISelectedBedInstancesChangedListener, Biz::Scene::SceneInteractor, SidebarSlaSupports> m_bed_selection_listener_scope;

    Domain::SelectionId m_current_project_id{Domain::INVALID_ID};
    Yoga::Item* m_rows_container{nullptr};
    Yoga::LayoutButton* m_edit_supports_button{nullptr};
};

} // namespace Slic3r::App::Preview