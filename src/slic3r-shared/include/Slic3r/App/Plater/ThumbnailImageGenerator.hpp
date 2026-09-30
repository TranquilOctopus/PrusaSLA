#pragma once

#include "Slic3r/Biz/Platform/IMainThreadDispatcher.hpp"
#include "libslic3r/IThumbnailImageGenerator.hpp"
#include "Slic3r/App/Plater/ThumbnailRenderer.hpp"
#include "Slic3r/App/FixtureRender.hpp"

#include <deque>

namespace Slic3r::Domain {
class Workbench;
} // namespace Slic3r::Domain

namespace Slic3r::App::Scene {
class IProjectSceneProvider;
} // namespace Slic3r::App::Scene

namespace Slic3r::App::Render {
class Device;
} // namespace Slic3r::App::Render

namespace Slic3r::App::Plater {

class ThumbnailImageGenerator : public Biz::Slicing::IThumbnailImageGenerator
{
public:
    void init(
        const Domain::Workbench& workbench,
        Render::Device& device,
        Scene::IProjectSceneProvider& scene_provider
    );

    std::future<Biz::Slicing::ThumbnailImageResults> enqueue_thumbnail_requests(
        const Biz::Slicing::ThumbnailImageRequests& requests
    ) override;
    void handle_enqueued_requests() override;

    bool initialized() const;

    /**
     * @brief Renders one view of one bed offscreen, the way a thumbnail is rendered (PLAN G3).
     *
     * The same path the objects list thumbnails go through, without the queue and without the
     * window: the scene is rendered into a framebuffer, read back and flipped, so the result is
     * the 3D view alone and its size is the one asked for. Prepare draws the bed, Preview the
     * Preview view itself, with that view's scene and that view's camera.
     *
     * Returns an empty vector when the project or the bed is gone, when a preview is asked for a
     * bed that has not been sliced, when the Preview view is not there to be asked (see
     * set_fixture_view_source) or when the view has nothing in its scene yet. Has to run on the
     * main thread with the render context current, like the queued thumbnails do.
     */
    [[nodiscard]] Domain::Images render_view(
        Domain::SelectionId project_id,
        Domain::SelectionId bed_instance_id,
        const Domain::Sizes& sizes,
        FixtureView view
    );

    /**
     * @brief The 3D view a render of the Preview view draws, which is the Preview view itself.
     *
     * The Prepare view is a scene of the workbench and the generator already has it, but the
     * Preview view has a scene of its own (roadmap M6.2b). The app sets this once the Preview
     * module is there; until then a render of the preview has nothing to draw and says so.
     */
    void set_fixture_view_source(App::IFixtureViewSource* fixture_view_source)
    {
        m_fixture_view_source = fixture_view_source;
    }

private:
    /// The Preview view of the print, drawn with its own scene and its own camera.
    [[nodiscard]] Domain::Images render_preview_view(const Domain::Sizes& sizes);

    struct Item
    {
        Biz::Slicing::ThumbnailImageRequests requests;
        std::promise<Biz::Slicing::ThumbnailImageResults> promise;

        bool operator==(const Item& other) const
        {
            return requests == other.requests;
        }
    };

    const Domain::Workbench* m_workbench{nullptr};
    Render::Device* m_device{nullptr};
    Scene::IProjectSceneProvider* m_scene_provider{nullptr};
    App::IFixtureViewSource* m_fixture_view_source{nullptr};
    std::unique_ptr<ThumbnailRenderer> m_renderer;
    std::deque<Item> m_queue;
};

} // namespace Slic3r::App::Plater
