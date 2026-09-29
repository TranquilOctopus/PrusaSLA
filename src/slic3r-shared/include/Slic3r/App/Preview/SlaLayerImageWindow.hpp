#pragma once

#include "Slic3r/App/Yoga/CollapsibleWindow.hpp"
#include "Slic3r/App/Render/Types.hpp"

#include <libslic3r/SLAResult.hpp>
#include <libslic3r/SLALayerImage.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::App::Render { class Device; }
namespace Slic3r::Biz { class ProjectInteractor; }

namespace Slic3r::App::Yoga {
class Text;
class LayoutButton;
class Item;
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
    void rebuild_zoom_texture();
    void render_plot(
        const std::vector<float>& values,
        const std::string& title,
        size_t current_layer,
        Platform::Color line_color,
        float width,
        float height
    );
    std::string layer_stats_text() const;
    void on_prev_layer();
    void on_next_layer();
    // Turns a click on the fitted layer image into slice mm and opens the native resolution zoom on it.
    void on_layer_image_clicked();
    void render_zoom_region(size_t layer_index);
    void close_zoom();
    size_t current_layer_index() const;

    Render::Device& m_device;
    Biz::ProjectInteractor& m_project_interactor;
    DoubleSliderForLayers* m_slider{nullptr};
    const Biz::Slicing::SLAResult* m_last_result{nullptr};
    size_t m_last_layer_index{SIZE_MAX};

    // Per layer statistics owned by the result, kept alive as long as we draw them
    std::shared_ptr<const Biz::Slicing::SLAResultData> m_result_data;
    std::shared_ptr<const std::vector<float>> m_layer_areas;
    std::shared_ptr<const std::vector<float>> m_layer_peel_force;
    size_t m_current_layer{0};

    Render::TexturePtr m_texture;
    size_t m_texture_width{0};
    size_t m_texture_height{0};

    Yoga::Text* m_layer_info_text{nullptr};
    Yoga::LayoutButton* m_prev_button{nullptr};
    Yoga::LayoutButton* m_next_button{nullptr};

    std::optional<::Slic3r::sla::SlaLayerImage> m_current_layer_image;

    // Pixel zoom: a 256 x 256 px window of the layer at the printer's native pixel size.
    Yoga::Item* m_zoom_panel{nullptr};
    Yoga::Item* m_zoom_header{nullptr};
    Yoga::Text* m_zoom_label{nullptr};
    Yoga::LayoutButton* m_zoom_close_button{nullptr};
    Render::TexturePtr m_zoom_texture;
    size_t m_zoom_texture_width{0};
    size_t m_zoom_texture_height{0};
    double m_zoom_center_x_mm{0.};
    double m_zoom_center_y_mm{0.};
    std::optional<::Slic3r::sla::SlaLayerImage> m_zoom_image;
};

} // namespace Slic3r::App::Preview
