#include "Slic3r/App/Plater/SlaHollowGizmo.hpp"

#include "Slic3r/App/Plater/PlaterScenePresenter.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

using namespace Slic3r::Biz;

namespace Slic3r::App::Plater {

SlaHollowGizmo::SlaHollowGizmo(
    PlaterScenePresenter& scene_presenter
) :
    m_scene_presenter(scene_presenter),
    m_dialog(std::make_unique<GizmoWindow>())
{
    m_dialog->set_title(_u8L("SLA Hollow"));
    m_dialog->set_shortcut("H");
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
    return true;
}

void SlaHollowGizmo::on_activated()
{
    // Stub: no implementation yet
}

void SlaHollowGizmo::on_deactivated()
{
    // Stub: no implementation yet
}

std::unique_ptr<GizmoWindow> SlaHollowGizmo::release_ui_window()
{
    return std::move(m_dialog);
}

} // namespace Slic3r::App::Plater