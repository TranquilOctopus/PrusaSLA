#include "Slic3r/App/Plater/SlaSupportPointsGizmo.hpp"

#include "Slic3r/App/Plater/PlaterScenePresenter.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

using namespace Slic3r::Biz;

namespace Slic3r::App::Plater {

SlaSupportPointsGizmo::SlaSupportPointsGizmo(
    PlaterScenePresenter& scene_presenter
) :
    m_scene_presenter(scene_presenter),
    m_dialog(std::make_unique<GizmoWindow>())
{
    m_dialog->set_title(_u8L("SLA Support Points"));
    m_dialog->set_shortcut("P");
}

SlaSupportPointsGizmo::~SlaSupportPointsGizmo() = default;

Scene::ToolType SlaSupportPointsGizmo::type() const
{
    return Scene::ToolType::SlaSupportPoints;
}

bool SlaSupportPointsGizmo::supports_printer(Domain::PrinterTechnology pt) const
{
    return pt == Domain::PrinterTechnology::SLA;
}

bool SlaSupportPointsGizmo::enabled() const
{
    return true;
}

void SlaSupportPointsGizmo::on_activated()
{
    // Stub: no implementation yet
}

void SlaSupportPointsGizmo::on_deactivated()
{
    // Stub: no implementation yet
}

std::unique_ptr<GizmoWindow> SlaSupportPointsGizmo::release_ui_window()
{
    return std::move(m_dialog);
}

} // namespace Slic3r::App::Plater