#pragma once

#include "Slic3r/App/Yoga/CollapsibleWindow.hpp"
#include "Slic3r/App/Render/Types.hpp"

#include <libslic3r/SLAResult.hpp>
#include <libslic3r/SLALayerImage.hpp>

#include <memory>
#include <optional>

namespace Slic3r::App::Render { class Device; }
namespace Slic3r::Biz { class ProjectInteractor; }

namespace Slic3r::App::Yoga {
class Text;
class LayoutButton;
}

namespace Slic3r::App::Preview {

class DoubleSliderForLayers;

class SlaLayerImageWindow : public Yoga::CollapsibleWindow
{
public:
    SlaLayerImageWindow(Render::Device& device, Biz::ProjectInteractor& project_interactor);
    ~SlaLayerImageWindow() override;

    void update(const Biz::Slicing::SLAResult* result);
    void set_slider(DoubleSliderForLayers* slider);

protected:
    void render_body(const Domain::Vec2f& pos, const Domain::Vec2f& size) override;

private:
    void rebuild_texture_if_needed();
    void on_prev_layer();
    void on_next_layer();

    Render::Device& m_device;
    Biz::ProjectInteractor& m_project_interactor;
    DoubleSliderForLayers* m_slider{nullptr};
    const Biz::Slicing::SLAResult* m_last_result{nullptr};
    size_t m_last_layer_index{SIZE_MAX};

    Render::TexturePtr m_texture;
    size_t m_texture_width{0};
    size_t m_texture_height{0};

    Yoga::Text* m_layer_info_text{nullptr};
    Yoga::LayoutButton* m_prev_button{nullptr};
    Yoga::LayoutButton* m_next_button{nullptr};

    std::optional<::Slic3r::sla::SlaLayerImage> m_current_layer_image;
};

} // namespace Slic3r::App::Preview