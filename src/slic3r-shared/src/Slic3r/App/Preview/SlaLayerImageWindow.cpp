#include "Slic3r/App/Preview/SlaLayerImageWindow.hpp"

#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Render/ImguiRender.hpp"
#include "Slic3r/App/Imgui/ImguiExtension.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/App/IsSlaActive.hpp"

#include "Slic3r/Domain/ConfigViews.hpp"
#include "Slic3r/Domain/Types.hpp"

#include <libslic3r/SLALayerImage.hpp>

#include <imgui/imgui.h>

namespace Slic3r::App::Yoga {
class Item;
}

// Custom Item that renders a texture
class ImageItem : public Slic3r::App::Yoga::Item
{
public:
    ImageItem() = default;
    void set_texture(const Slic3r::App::Render::TexturePtr& texture, const ImVec2& size)
    {
        m_texture = texture;
        m_size = size;
    }
    void clear_texture() { m_texture.reset(); }

protected:
    void render(const Slic3r::Domain::Vec2f& pos, const Slic3r::Domain::Vec2f& size) override
    {
        Item::render(pos, size);
        if (m_texture) {
            Slic3r::App::Render::ImguiRender* imgui_render = Slic3r::App::Yoga::Item::imgui_render();
            if (imgui_render) {
                ImGui::PushStyleVar(ImGuiStyleVar_ImageRounding, 0.f);
                ImGui::ImageWithBg(
                    (ImTextureID)(intptr_t)m_texture.get(),
                    m_size,
                    ImVec2(0, 0),
                    ImVec2(1, 1),
                    ImVec4(0, 0, 0, 1),
                    ImVec4(1, 1, 1, 1)
                );
                imgui_render->use_texture(m_texture);
                ImGui::PopStyleVar();
            }
        }
    }

private:
    Slic3r::App::Render::TexturePtr m_texture;
    ImVec2 m_size{0, 0};
};

namespace Slic3r::App::Yoga {
class Item;
}

using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;

namespace Slic3r::App::Preview {

SlaLayerImageWindow::SlaLayerImageWindow(SlaViewerWrapper* sla_viewer_wrapper)
    : CollapsibleWindow(_u8L("Layer image"), "SlaLayerImageWindow")
    , m_sla_viewer_wrapper(sla_viewer_wrapper)
{
    set_default_size({420.f, 320.f});
    set_collapsible(true);
    content()->set_padding(0.f);

    // Create a scroll area for full resolution mode
    m_scroll_area = content()->emplace_back<ScrollArea>();
    m_scroll_area->set_orientation(Orientation::Vertical);
    m_scroll_area->set_padding(Paddings(0.f));
    m_scroll_area->set_visible(false);

    m_image_container = m_scroll_area->emplace_back<ImageItem>();
    m_image_container->set_flex_shrink(0.f);

    // Layer info text (shown when not in full resolution mode, or always at top)
    m_layer_info_text = content()->emplace_back<Text>("");
    m_layer_info_text->set_padding(Paddings(10.f, 5.f, 10.f, 5.f));

    // Controls row: Previous / Next / Full resolution
    Item* controls_row = content()->emplace_back<Item>();
    controls_row->set_orientation(Orientation::Horizontal);
    controls_row->set_justify_content(YGJustifySpaceBetween);
    controls_row->set_padding(Paddings(10.f, 5.f, 10.f, 10.f));
    controls_row->set_gap(10.f);

    m_prev_button = controls_row->emplace_back<LayoutButton>(_u8L("Previous"), Render::Icon::CaretLeft);
    m_prev_button->callbacks().action = [this]() { on_prev_layer(); };

    m_next_button = controls_row->emplace_back<LayoutButton>(_u8L("Next"), Render::Icon::CaretRight);
    m_next_button->callbacks().action = [this]() { on_next_layer(); };

    m_full_resolution_checkbox = controls_row->emplace_back<ToggleButton>(_u8L("Full resolution"));
    m_full_resolution_checkbox->set_checked(false);
    m_full_resolution_checkbox->callbacks().checked_changed = [this](bool checked) {
        on_full_resolution_changed(checked);
    };

    set_visible(false);
}

SlaLayerImageWindow::~SlaLayerImageWindow() = default;

void SlaLayerImageWindow::update(const Biz::Slicing::SLAResult* result)
{
    if (!result || result->slices.empty()) {
        set_visible(false);
        return;
    }

    // Check if SLA is active
    if (!is_sla_active(m_sla_viewer_wrapper->project_interactor())) {
        set_visible(false);
        return;
    }

    set_visible(true);

    // Get current layer index from the slider
    auto* slider = m_sla_viewer_wrapper->slider_layers();
    if (!slider) {
        set_visible(false);
        return;
    }

    size_t current_layer = static_cast<size_t>(slider->active_pos());

    // Clamp to valid range
    if (current_layer >= result->slices.size()) {
        current_layer = result->slices.empty() ? 0 : result->slices.size() - 1;
    }

    bool full_resolution = m_full_resolution_checkbox->checked();

    // Check if we need to rebuild the texture
    bool result_changed = m_last_result != result;
    bool layer_changed = m_last_layer_index != current_layer;
    bool full_res_changed = m_last_full_resolution != full_resolution;

    if (result_changed || layer_changed || full_res_changed) {
        m_last_result = result;
        m_last_layer_index = current_layer;
        m_last_full_resolution = full_resolution;

        // Render the layer image
        size_t max_w = full_resolution ? 0 : 1024;
        size_t max_h = full_resolution ? 0 : 1024;

        const Domain::ConfigView& printer_config = result->export_data->config;
        m_current_layer_image = sla::render_sla_layer_image(
            result->slices[current_layer], printer_config, max_w, max_h);

        rebuild_texture_if_needed();
    }

    // Update layer info text
    std::string layer_text = _u8L("Layer") + " " + std::to_string(current_layer + 1) + " / " + std::to_string(result->slices.size());
    if (current_layer < result->heights.size()) {
        layer_text += "  Z = " + Slic3r::format("%.2f mm", result->heights[current_layer]);
    }
    m_layer_info_text->set_text(layer_text);

    // Update button states
    m_prev_button->set_enabled(current_layer > 0);
    m_next_button->set_enabled(current_layer + 1 < result->slices.size());

    // Update scroll area visibility
    m_scroll_area->set_visible(full_resolution);
    m_image_container->set_visible(full_resolution);

    // Request render to update the image
    request_render();
}

void SlaLayerImageWindow::rebuild_texture_if_needed()
{
    if (!m_current_layer_image.has_value() || m_current_layer_image->pixels.empty()) {
        return;
    }

    const auto& img = *m_current_layer_image;
    size_t w = img.width;
    size_t h = img.height;

    // Convert gray to RGBA (g, g, g, 255)
    std::vector<uint8_t> rgba_data;
    rgba_data.resize(w * h * 4);
    for (size_t i = 0; i < w * h; ++i) {
        uint8_t g = img.pixels[i];
        rgba_data[i * 4 + 0] = g;
        rgba_data[i * 4 + 1] = g;
        rgba_data[i * 4 + 2] = g;
        rgba_data[i * 4 + 3] = 255;
    }

    // Check if we can reuse the texture
    bool size_changed = m_texture_width != w || m_texture_height != h;

    if (!m_texture || size_changed) {
        Render::Device& device = m_sla_viewer_wrapper->device();
        m_texture = device.create_texture();
        m_texture->set_object_name("sla_layer_image");
        m_texture->set_filtering(Render::TextureMinFilter::Nearest, Render::TextureMagFilter::Nearest);
        m_texture->set_wrap_s(Render::TextureWrap::ClampToEdge);
        m_texture->set_wrap_t(Render::TextureWrap::ClampToEdge);
        m_texture_width = w;
        m_texture_height = h;
    }

    m_texture->set_data(
        Render::PixelFormat::RGBA8,
        0,
        static_cast<int>(w),
        static_cast<int>(h),
        rgba_data.data(),
        rgba_data.size()
    );

    // Update image container
    auto* image_item = dynamic_cast<ImageItem*>(m_image_container);
    if (image_item) {
        image_item->set_texture(m_texture, ImVec2(static_cast<float>(w), static_cast<float>(h)));
    }

    // Update image container size for full resolution mode
    if (m_full_resolution_checkbox->checked()) {
        m_image_container->set_min_width(static_cast<float>(w));
        m_image_container->set_min_height(static_cast<float>(h));
        m_image_container->set_max_width(static_cast<float>(w));
        m_image_container->set_max_height(static_cast<float>(h));
    } else {
        m_image_container->set_min_width(0.f);
        m_image_container->set_min_height(0.f);
        m_image_container->set_max_width(FLT_MAX);
        m_image_container->set_max_height(FLT_MAX);
    }
}

void SlaLayerImageWindow::render_body(const Domain::Vec2f& pos, const Domain::Vec2f& size)
{
    // Render the standard content (layer info, controls, scroll area)
    // The ImageItem will render the texture automatically during its render (full resolution mode)
    CollapsibleWindow::render_body(pos, size);

    // In fit-to-window mode, render the image centered in the available space
    if (!m_full_resolution_checkbox->checked() && m_current_layer_image.has_value() && m_texture) {
        const auto& img = *m_current_layer_image;
        float img_w = static_cast<float>(img.width);
        float img_h = static_cast<float>(img.height);

        Render::ImguiRender* imgui_render = Yoga::Item::imgui_render();
        if (!imgui_render) {
            return;
        }

        ImVec2 avail = ImGui::GetContentRegionAvail();
        float scale = std::min(avail.x / img_w, avail.y / img_h);
        scale = std::max(scale, 0.1f);

        float draw_w = img_w * scale;
        float draw_h = img_h * scale;

        // Center the image in the available space
        ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImVec2 center_offset = (avail - ImVec2(draw_w, draw_h)) * 0.5f;
        ImGui::SetCursorScreenPos(cursor + center_offset);

        ImGui::PushStyleVar(ImGuiStyleVar_ImageRounding, 0.f);
        ImGui::ImageWithBg(
            (ImTextureID)(intptr_t)m_texture.get(),
            ImVec2(draw_w, draw_h),
            ImVec2(0, 0),
            ImVec2(1, 1),
            ImVec4(0, 0, 0, 1),
            ImVec4(1, 1, 1, 1)
        );
        imgui_render->use_texture(m_texture);
        ImGui::PopStyleVar();
    }
}

void SlaLayerImageWindow::on_prev_layer()
{
    m_sla_viewer_wrapper->slider_layers_move_current_thumb(-1);
}

void SlaLayerImageWindow::on_next_layer()
{
    m_sla_viewer_wrapper->slider_layers_move_current_thumb(1);
}

void SlaLayerImageWindow::on_full_resolution_changed(bool checked)
{
    // Force texture rebuild on next update
    m_last_full_resolution = !checked;
    // Request extra frame to update immediately
    m_sla_viewer_wrapper->request_extra_frames(1);
}

} // namespace Slic3r::App::Preview