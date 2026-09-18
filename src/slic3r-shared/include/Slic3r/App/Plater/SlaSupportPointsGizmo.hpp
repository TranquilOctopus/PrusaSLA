#pragma once

#include "Slic3r/App/Scene/IGizmo.hpp"
#include "Slic3r/App/Plater/GizmoWindow.hpp"

#include <memory>

namespace Slic3r::App::Plater {
class PlaterScenePresenter;

class SlaSupportPointsGizmo : public Scene::IToolGizmo
{
public:
    SlaSupportPointsGizmo(
        PlaterScenePresenter& scene_presenter
    );

    ~SlaSupportPointsGizmo() override;

    Scene::ToolType type() const override;
    bool supports_printer(Domain::PrinterTechnology pt) const override;
    bool enabled() const override;

    void on_activated() override;
    void on_deactivated() override;

    std::unique_ptr<GizmoWindow> release_ui_window() override;

private:
    PlaterScenePresenter& m_scene_presenter;
    std::unique_ptr<GizmoWindow> m_dialog;
};

} // namespace Slic3r::App::Plater