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
class ScrollArea;
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
    // The peel force in N above which a layer is called high, 0 when the slicer found no warning.
    double peel_warning_n() const;
    void on_prev_layer();
    void on_next_layer();
    // Turns a click on the fitted layer image into slice mm and opens the native resolution zoom on it.
    void on_layer_image_clicked();
    void render_zoom_region(size_t layer_index);
    void close_zoom();
    size_t current_layer_index() const;

    // A single island reported by the slicer, with the slice coordinates of its centroid.
    struct Island
    {
        size_t layer{0};
        double x_mm{0.};
        double y_mm{0.};
        std::optional<double> area_mm2;
        std::string object_name; //< the model it was found on, empty when unknown
    };
    // Fills the island list and its rows from the issues of the last result.
    void rebuild_island_list();
    std::string island_row_text(const Island& island) const;
    // Island layer closest to the current one, in the given direction.
    std::optional<size_t> prev_island_layer() const;
    std::optional<size_t> next_island_layer() const;
    void go_to_layer(size_t layer);
    void on_prev_island();
    void on_next_island();
    // The window has to be tall enough for the image, the charts, the island list and the zoom.
    void update_min_height();
    // Draws a ring on every island of the layer the image was rendered for.
    void render_island_markers(float rect_min_x, float rect_min_y, float rect_width, float rect_height) const;

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

    // Islands of the last result, sorted by layer. The list is empty and hidden when the
    // slicer found none.
    std::vector<Island> m_islands;
    std::vector<size_t> m_island_layers; //< unique layers of m_islands, ascending
    Yoga::Item* m_islands_panel{nullptr};
    Yoga::Item* m_islands_header{nullptr};
    Yoga::Text* m_islands_title{nullptr};
    Yoga::LayoutButton* m_islands_prev_button{nullptr};
    Yoga::LayoutButton* m_islands_next_button{nullptr};
    Yoga::ScrollArea* m_islands_rows{nullptr};

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
