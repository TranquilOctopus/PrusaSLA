#pragma once

#include "Slic3r/App/Yoga/CollapsibleWindow.hpp"

#include "Slic3r/Biz/Slicing/SLAResult.hpp"
#include "Slic3r/App/Preview/SlaViewerWrapper.hpp"

#include <libslic3r/SLALayerImage.hpp>

#include <memory>
#include <optional>

namespace Slic3r::App::Yoga {
class ScrollArea;
class Text;
class ToggleButton;
class LayoutButton;
}

namespace Slic3r::App::Preview {

class SlaLayerImageWindow : public Yoga::CollapsibleWindow
{
public:
    explicit SlaLayerImageWindow(SlaViewerWrapper* sla_viewer_wrapper);
    ~SlaLayerImageWindow() override;

    void update(const Biz::Slicing::SLAResult* result);

protected:
    void render_body(const Domain::Vec2f& pos, const Domain::Vec2f& size) override;

private:
    void rebuild_texture_if_needed();
    void on_prev_layer();
    void on_next_layer();
    void on_full_resolution_changed(bool checked);

    SlaViewerWrapper* m_sla_viewer_wrapper{nullptr};
    const Biz::Slicing::SLAResult* m_last_result{nullptr};
    size_t m_last_layer_index{SIZE_MAX};
    bool m_last_full_resolution{false};

    Render::TexturePtr m_texture;
    size_t m_texture_width{0};
    size_t m_texture_height{0};

    Yoga::ScrollArea* m_scroll_area{nullptr};
    Yoga::Item* m_image_container{nullptr};
    Yoga::Text* m_layer_info_text{nullptr};
    Yoga::LayoutButton* m_prev_button{nullptr};
    Yoga::LayoutButton* m_next_button{nullptr};
    Yoga::ToggleButton* m_full_resolution_checkbox{nullptr};

    std::optional<SlaLayerImage> m_current_layer_image;
};

} // namespace Slic3r::App::Preview