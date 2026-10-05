#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "Slic3r/Biz/ProjectScoped.hpp"
#include "Slic3r/Biz/ISelectedProjectChangedListener.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/ObjectID.hpp"

#include "Slic3r/App/IAppConfigChangedListener.hpp"
#include "Slic3r/App/ModalDialog.hpp"

#include <boost/filesystem/path.hpp>

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {

namespace Plater {
class PlaterRenderModule;
} // namespace Plater

namespace Preview {
class PreviewRenderModule;
} // namespace Preview

namespace Platform {
class AbstractRenderCanvas;
} // namespace Platform

namespace Scene {
enum class ToolType : uint8_t;
} // namespace Scene

namespace Yoga {
class Dialog;
} // namespace Yoga

namespace Render {
enum class ModuleType
{
    Undef,
    Plater,
    Preview,
};
} // namespace Render

class Navigator : public Biz::ISelectedProjectChangedListener, public IAppConfigChangedListener
{
public:
    ~Navigator();

    struct Callbacks
    {
        std::function<void()> render_module_switched;
        std::function<void(ModalDialog)> modal_dialog_changed;
    };

    Callbacks& callbacks();

    /// Makes the canvas reachable before the plater and preview modules exist.
    void set_canvas(Platform::AbstractRenderCanvas& canvas);

    /// False between set_canvas and on_init, while the app runs without the plater and preview.
    bool has_modules() const;

    void on_init(
        Plater::PlaterRenderModule& plater_module,
        Preview::PreviewRenderModule& preview_module,
        Platform::AbstractRenderCanvas& canvas,
        Biz::ProjectInteractor* project_interactor
    );

    void navigate_to_module_type(Render::ModuleType type);

    /**
     * @brief Open the resin import review dialog for @p path.
     *
     * The dialog lives in the bed sidebar of a render module, so this goes to the module that is
     * on the screen: both Prepare and Preview have one, so the import does not switch views. Called
     * by the entry points of M3.10b, the "Import resin profile" button and a profile dropped onto
     * the window.
     */
    void open_resin_import(const boost::filesystem::path& path);

    /// Switch to Prepare and open one of its tools, e.g. Scene::ToolType::SlaSupportPoints.
    void activate_plater_tool(Scene::ToolType tool);

    /**
     * @brief Run the SLA support tool's Auto support on @p object_ids, or on every printable model
     * of the project when the list is empty.
     *
     * The generation belongs to the support tool, so Prepare and that tool have to be open, which
     * is what the Preview "Supports" section does before it calls this (M2.17d4).
     *
     * @return false when the run was not started, because the tool is not open, the printer is not
     * an SLA one or nothing could be supported.
     */
    bool run_sla_auto_support(const std::vector<Domain::ObjectID>& object_ids = {});

    /// Whether a support generation of the Prepare support tool is still going, false when it is
    /// not open at all.
    bool sla_auto_support_running() const;

    /**
     * @brief What the raft type Auto resolved to for a model, as the support preview decided it.
     *
     * The decision is a reading of the underside of the model that only the support preview does
     * outside the slice (rulebook R6, M7.8.4), so the Prepare module owns it and everything else
     * asks for it here, the way it asks for the state of the tool above. Empty when there is
     * nothing to report: no Prepare module yet, no built preview for that model, or a raft type
     * that is not Auto, which is read as it is stored.
     */
    std::optional<Domain::sla::RaftType> sla_auto_raft_type(Domain::ObjectID object_id) const;

    void on_selected_project_changed(size_t index) override;

    void set_opened_dialog(Yoga::Dialog* opened_dialog);

    void navigate_to_item(const Domain::ConfigItem* config_item);

    void request_search();

    void open_invalid_data_dialog();

    void set_modal_dialog(ModalDialog dialog);
    ModalDialog current_modal_dialog() const;
    bool is_any_modal_dialog_opened() const;

    bool object_list_collapsed() const;
    void set_object_list_collapsed(bool collapsed);

    bool has_fullscreen() const;
    bool is_fullscreen() const;
    void set_fullscreen(bool fullscreen);
    void close_application();

    void on_app_config_changed(const std::string& key) override;

private:
    void set_render_module_type(Render::ModuleType type);

private:
    struct ProjectContext
    {
        Render::ModuleType type{Render::ModuleType::Plater};
        Yoga::Dialog* opened_dialog{nullptr}; ///< Dialog is considered exclusive
    };

    using ProjectContexts    = Biz::ProjectScoped<ProjectContext>;
    using ProjectContextsPtr = std::unique_ptr<ProjectContexts>;

    ProjectContextsPtr m_project_contexts;
    Callbacks m_callbacks;

    Plater::PlaterRenderModule* m_plater_module{nullptr};
    Preview::PreviewRenderModule* m_preview_module{nullptr};
    Platform::AbstractRenderCanvas* m_canvas{nullptr};
    Biz::ProjectInteractor* m_project_interactor{nullptr};
    bool m_object_list_collapsed{false};
    ModalDialog m_current_modal_dialog{ModalDialog::None};
};

} // namespace Slic3r::App
