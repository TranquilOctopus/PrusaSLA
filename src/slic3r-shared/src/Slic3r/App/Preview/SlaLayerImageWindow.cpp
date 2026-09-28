#include "Slic3r/App/Preview/SlaLayerImageWindow.hpp"

#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Render/Texture.hpp"
#include "Slic3r/App/Preview/DoubleSliderForLayers.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/App/IsSlaActive.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"

#include <libslic3r/SLAResult.hpp>
#include <libslic3r/SLALayerImage.hpp>

#include <imgui/imgui.h>

#include <fmt/format.h>

namespace Slic3r::App::Preview {

using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;

SlaLayerImageWindow::SlaLayerImageWindow(Render::Device& device, Biz::ProjectInteractor& project_interactor)
    : CollapsibleWindow(_u8L("Layer image"), "SlaLayerImageWindow")
    , m_device(device)
    , m_project_interactor(project_interactor)
{
    set_default_size({420.f, 320.f});
    set_collapsible(true);
    content()->set_padding(0.f);

    // Layer info text
    m_layer_info_text = content()->emplace_back<Text>("");
    m_layer_info_text->set_padding(Paddings(10.f, 5.f, 10.f, 5.f));

    // Controls row: Previous / Next
    Item* controls_row = content()->emplace_back<Item>();
    controls_row->set_orientation(Orientation::Horizontal);
    controls_row->set_justify_content(YGJustifySpaceBetween);
    controls_row->set_padding(Paddings(10.f, 5.f, 10.f, 10.f));
    controls_row->set_gap(10.f);

    m_prev_button = controls_row->emplace_back<LayoutButton>(_u8L("Previous"), Render::Icon::CaretLeft);
    m_prev_button->callbacks().action = [this]() { on_prev_layer(); };

    m_next_button = controls_row->emplace_back<LayoutButton>(_u8L("Next"), Render::Icon::CaretRight);
    m_next_button->callbacks().action = [this]() { on_next_layer(); };

    set_visible(false);
}

SlaLayerImageWindow::~SlaLayerImageWindow() = default;

void SlaLayerImageWindow::set_slider(DoubleSliderForLayers* slider)
{
    m_slider = slider;
}

void SlaLayerImageWindow::update(const Biz::Slicing::SLAResult* result)
{
    if (!result || result->slices.empty()) {
        set_visible(false);
        return;
    }

    // Check if SLA is active
    if (!is_sla_active(m_project_interactor)) {
        set_visible(false);
        return;
    }

    if (!m_slider) {
        set_visible(false);
        return;
    }

    set_visible(true);

    // Get current layer index from the slider (active_pos is the current thumb position)
    size_t current_layer = static_cast<size_t>(m_slider->active_pos());

    // Clamp to valid range
    if (current_layer >= result->slices.size()) {
        current_layer = result->slices.empty() ? 0 : result->slices.size() - 1;
    }

    // Check if we need to rebuild the texture
    bool result_changed = m_last_result != result;
    bool layer_changed = m_last_layer_index != current_layer;

    bool needs_rebuild = false;
    if (result_changed || layer_changed) {
        m_last_result = result;
        m_last_layer_index = current_layer;
        needs_rebuild = true;

        // Render the layer image (max 1024x1024)
        const Domain::ConfigView& printer_config = result->export_data->config;
        m_current_layer_image = sla::render_sla_layer_image(
            result->slices[current_layer], printer_config, 1024, 1024);

        rebuild_texture_if_needed();
    }

    // Update layer info text
    std::string layer_text = _u8L("Layer") + " " + std::to_string(current_layer + 1) + " / " + std::to_string(result->slices.size());
    if (current_layer < result->heights.size()) {
        layer_text += "  Z = " + fmt::format("{:.2f} mm", result->heights[current_layer]);
    }
    m_layer_info_text->set_text(layer_text);

    // Update button states
    m_prev_button->set_enabled(current_layer > 0);
    m_next_button->set_enabled(current_layer + 1 < result->slices.size());

    // Request render only when image was rebuilt or text changed
    if (needs_rebuild || layer_changed || result_changed) {
        Biz::Platform::PlatformServices::instance().render_request_handler().request_render();
    }
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
        m_texture = m_device.create_texture();
        m_texture->set_object_name("sla_layer_image");
        m_texture->set_filtering(Render::TextureMinFilter::Nearest, Render::TextureMagFilter::Nearest);
        m_texture->set_wrap_s(Render::TextureWrap::ClampToEdge);
        m_texture->set_wrap_t(Render::TextureWrap::ClampToEdge);
        m_texture_width = w;
        m_texture_height = h;
    }

    using Slic3r::Domain::PixelFormat;
    m_texture->set_data(
        PixelFormat::RGBA8,
        0,
        static_cast<int>(w),
        static_cast<int>(h),
        rgba_data.data(),
        rgba_data.size()
    );
}

void SlaLayerImageWindow::render_body(const Domain::Vec2f& pos, const Domain::Vec2f& size)
{
    // Render the standard content (layer info, controls)
    CollapsibleWindow::render_body(pos, size);

    // Render the image centered in the available space
    if (m_current_layer_image.has_value() && m_texture) {
        const auto& img = *m_current_layer_image;
        float img_w = static_cast<float>(img.width);
        float img_h = static_cast<float>(img.height);

        ImVec2 avail = ImGui::GetContentRegionAvail();
        float scale = std::min(avail.x / img_w, avail.y / img_h);
        scale = std::max(scale, 0.1f);

        float draw_w = img_w * scale;
        float draw_h = img_h * scale;

        // Center the image in the available space
        ImVec2 cursor = ImGui::GetCursorScreenPos();
        float center_offset_x = (avail.x - draw_w) * 0.5f;
        float center_offset_y = (avail.y - draw_h) * 0.5f;
        ImGui::SetCursorScreenPos(ImVec2(cursor.x + center_offset_x, cursor.y + center_offset_y));

        ImGui::PushStyleVar(ImGuiStyleVar_ImageRounding, 0.f);
        render_image(m_texture, ImVec2(draw_w, draw_h), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 1), ImVec4(1, 1, 1, 1));
        ImGui::PopStyleVar();
    }
}

void SlaLayerImageWindow::on_prev_layer()
{
    if (m_slider) {
        m_slider->move_current_thumb(-1);
    }
}

void SlaLayerImageWindow::on_next_layer()
{
    if (m_slider) {
        m_slider->move_current_thumb(1);
    }
}

} // namespace Slic3r::App::Preview