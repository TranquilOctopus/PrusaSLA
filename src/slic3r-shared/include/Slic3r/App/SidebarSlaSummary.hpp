#pragma once

#include "Slic3r/App/Yoga/Window.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"
#include "Slic3r/Biz/ISelectedConfigContainerChangedListener.hpp"
#include "Slic3r/Biz/ISelectedProjectChangedListener.hpp"
#include "Slic3r/Biz/ISelectedBedInstanceChangedListener.hpp"
#include "Slic3r/Biz/Slicing/SlicingInteractor.hpp"
#include "Slic3r/Biz/ResinEconomicsInteractor.hpp"
#include "Slic3r/Domain/SelectionId.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
class SLAResultCache;
} // namespace Slic3r::Biz

namespace Slic3r::App {

class SidebarSlaSummary :
    public Yoga::Window,
    public Biz::ISelectedConfigContainerChangedListener,
    public Biz::ISelectedProjectChangedListener,
    public Biz::ISelectedBedInstancesChangedListener,
    public Biz::Slicing::IStatusListener
{
public:
    explicit SidebarSlaSummary(Biz::ProjectInteractor& project_interactor);

    void on_selected_config_container_changed(Domain::SelectionId project_id, Domain::SelectionId container_id) override;
    void on_selected_project_changed(size_t index) override;
    void on_selected_project_changed_final(size_t index) override;
    void on_selected_bed_instances_changed(Domain::SelectionId project_id, const Biz::Scene::BedSelection& bed_selection) override;
    void on_status_changed(const Biz::Slicing::StatusUpdate, const Domain::SlicingId&) override;

private:
    void refresh();
    void update_visibility();
    void clear_rows();

    Biz::ProjectInteractor& m_project_interactor;
    Biz::ListenerScope<Biz::ISelectedConfigContainerChangedListener, Biz::ProjectInteractor, SidebarSlaSummary> m_config_container_listener_scope;
    Biz::ListenerScope<Biz::ISelectedProjectChangedListener, Biz::ProjectInteractor, SidebarSlaSummary> m_project_listener_scope;
    Biz::ListenerScope<Biz::ISelectedBedInstancesChangedListener, Biz::Scene::SceneInteractor, SidebarSlaSummary> m_bed_selection_listener_scope;
    Biz::ListenerScope<Biz::Slicing::IStatusListener, Biz::Slicing::SlicingInteractor, SidebarSlaSummary> m_status_listener_scope;

    Domain::SelectionId m_current_bed_instance_id{Domain::INVALID_ID};
    Domain::SelectionId m_current_project_id{Domain::INVALID_ID};
    Domain::SelectionId m_current_config_container_id{Domain::INVALID_ID};
    Yoga::Item* m_rows_container{nullptr};
};

namespace Slic3r::App::SidebarSlaSummaryFormat {
std::string format_resin_ml(std::optional<double> ml);
std::string format_cost(std::optional<double> cost);
std::string format_bottles(std::optional<double> bottles);
std::string format_layers(std::optional<size_t> layers);
} // namespace Slic3r::App::SidebarSlaSummaryFormat

} // namespace Slic3r::App