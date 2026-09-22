#pragma once

#include "Slic3r/App/Yoga/Window.hpp"
#include "Slic3r/App/Preview/SlaLayerViewCache.hpp"
#include "Slic3r/App/Render/Texture.hpp"
#include "Slic3r/Domain/Image.hpp"
#include "Slic3r/Biz/Slicing/SLAResult.hpp"

#include <memory>
#include <optional>
#include <string>

namespace Slic3r::App::Yoga {
class Text;
class LayoutButton;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App::Preview {

class SlaLayerViewPanel : public Yoga::Window
{
public:
    explicit SlaLayerViewPanel();

    void set_sla_result(const Biz::Slicing::SLAResult* result);
    void set_current_layer(size_t layer_index, float layer_z);

    void on_sla_result_changed();

private:
    void render_body(const Yoga::Vec2f& pos, const Yoga::Vec2f& size) override;

    void decode_and_cache_layer(size_t layer_index);
    bool try_decode_png(const Slic3r::Sla::OutputFiles::FileData& file_data,
                        std::vector<uint8_t>& out_image_data,
                        unsigned& out_width,
                        unsigned& out_height);

    const Biz::Slicing::SLAResult* m_sla_result = nullptr;
    size_t m_current_layer_index = 0;
    float m_current_layer_z = 0.0f;

    SlaLayerViewCache m_cache;

    std::unique_ptr<Render::Texture> m_texture;
    std::string m_texture_name;

    Yoga::Text* m_layer_info_text = nullptr;
    Yoga::Text* m_no_preview_text = nullptr;
    Yoga::Text* m_no_data_text = nullptr;
};

} // namespace Slic3r::App::Preview