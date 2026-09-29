#pragma once

#include "Slic3r/App/Yoga/Window.hpp"
#include "Slic3r/App/Scene/Clipper.hpp"
#include "Slic3r/App/Scene/HeightBand.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"
#include "Slic3r/Biz/ISelectedBedInstanceChangedListener.hpp"
#include "Slic3r/Biz/ISelectedConfigContainerChangedListener.hpp"
#include "Slic3r/Biz/ISelectedProjectChangedListener.hpp"
#include "Slic3r/Biz/SLAObjectCache.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/Domain/SlicingId.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App::Render {
class Device;
} // namespace Slic3r::App::Render

namespace Slic3r::App::Scene {
class ISceneProvider;
class Node;
class ClipperPresenter;
} // namespace Slic3r::App::Scene

namespace Slic3r::App::Yoga {
class SliderWithInput;
class LayoutButton;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App::Plater {

/**
 * @brief The "Height band" panel of the 3D plater view, the SLA counterpart of Chitubox's preview band.
 *
 * Two sliders set a lower and an upper Z limit taken from the selected printer's max_print_height. The band
 * is applied through the plater's own Scene::ClipperPresenter: the lower limit is the clipper plane (and so
 * the mesh clipper caps its cut face), the upper limit is a second clipping plane of the same shader.
 * Every printable instance on the build plate, plus the SLA support tree and the raft, are fed to the
 * clipper as extra meshes, so the band shows the whole print and not only the selected model. The band is
 * off by default (full range) and suspended while a tool gizmo owns the shared clipper.
 */
class SlaHeightBand :
    public Yoga::Window,
    public Biz::ISelectedConfigContainerChangedListener,
    public Biz::ISelectedProjectChangedListener,
    public Biz::Scene::ISceneSelectionChangedListener,
    public Biz::ISelectedBedInstancesChangedListener,
    public Biz::ISLAObjectCacheChangedListener
{
public:
    SlaHeightBand(
        Biz::ProjectInteractor& project_interactor,
        Render::Device& device,
        Scene::ISceneProvider& scene_provider,
        Scene::Clipper& clipper
    );
    ~SlaHeightBand();

    void on_selected_config_container_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId container_id
    ) override;
    void on_selected_project_changed(size_t index) override;
    void on_selected_project_changed_final(size_t index) override;
    void on_scene_selection_changed(
        Domain::SelectionId project_id,
        const Biz::Scene::ObjectSelection& selection
    ) override;
    void on_selected_bed_instances_changed(
        Domain::SelectionId project_id,
        const Biz::Scene::BedSelection& bed_selection
    ) override;
    // A finished slice replaced the support tree or the raft of an object on the bed.
    void on_sla_object_cache_changed(const Domain::SlicingId& id, Domain::ObjectID object_id) override;

    /// A tool gizmo is using the shared clipper - hide the band and remember that it has to come back.
    void set_suspended(bool suspended);

    [[nodiscard]] const Scene::HeightBand& band() const
    {
        return m_band;
    }

private:
    void refresh();
    void update_visibility();
    void update_limits();
    void apply();
    void stop();

    [[nodiscard]] double max_print_height() const;
    [[nodiscard]] const Domain::ModelObject* selected_object() const;
    [[nodiscard]] const Domain::ModelInstance* selected_instance() const;
    // Every printable model on the selected build plate, the support trees and the rafts.
    [[nodiscard]] std::vector<Scene::Clipper::ExtraMesh> collect_extra_meshes() const;

private:
    Biz::ProjectInteractor& m_project_interactor;
    Render::Device& m_device;
    Scene::ISceneProvider& m_scene_provider;
    std::unique_ptr<Scene::ClipperPresenter> m_presenter;
    Scene::Node* m_main_node{nullptr};

    Biz::ListenerScope<
        Biz::ISelectedConfigContainerChangedListener,
        Biz::ProjectInteractor,
        SlaHeightBand>
        m_config_container_listener_scope;
    Biz::ListenerScope<Biz::ISelectedProjectChangedListener, Biz::ProjectInteractor, SlaHeightBand>
        m_project_listener_scope;
    Biz::ListenerScope<
        Biz::Scene::ISceneSelectionChangedListener,
        Biz::Scene::SceneInteractor,
        SlaHeightBand>
        m_selection_listener_scope;
    Biz::ListenerScope<
        Biz::ISelectedBedInstancesChangedListener,
        Biz::Scene::SceneInteractor,
        SlaHeightBand>
        m_bed_selection_listener_scope;
    Biz::ListenerScope<
        Biz::ISLAObjectCacheChangedListener,
        Biz::SLAObjectCache,
        SlaHeightBand>
        m_sla_object_cache_listener_scope;

    Domain::SelectionId m_current_project_id{Domain::INVALID_ID};
    Domain::SelectionId m_current_config_container_id{Domain::INVALID_ID};

    Scene::HeightBand m_band;
    double m_z_min{0.};
    double m_z_max{0.};
    double m_max_print_height{0.};
    bool m_suspended{false};
    bool m_presented{false};
    const Domain::ModelObject* m_clip_object{nullptr};
    const Domain::ModelInstance* m_clip_instance{nullptr};

    Yoga::SliderWithInput* m_min_slider{nullptr};
    Yoga::SliderWithInput* m_max_slider{nullptr};
    Yoga::LayoutButton* m_reset_button{nullptr};
};

} // namespace Slic3r::App::Plater
