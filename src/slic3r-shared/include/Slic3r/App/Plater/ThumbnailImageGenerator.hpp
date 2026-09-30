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
     * sliced print.
     *
     * Returns an empty vector when the project or the bed is gone, or when a preview is asked for
     * a bed that has not been sliced. Has to run on the main thread with the render context
     * current, like the queued thumbnails do.
     */
    [[nodiscard]] Domain::Images render_view(
        Domain::SelectionId project_id,
        Domain::SelectionId bed_instance_id,
        const Domain::Sizes& sizes,
        FixtureView view
    );

private:
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
    std::unique_ptr<ThumbnailRenderer> m_renderer;
    std::deque<Item> m_queue;
};

} // namespace Slic3r::App::Plater
