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
#include "Slic3r/Domain/ConfigDefsSLA.hpp"

#include <libslic3r/SLAResult.hpp>
#include <libslic3r/SLALayerImage.hpp>

#include <imgui/imgui.h>

#include <fmt/format.h>

#include <algorithm>

namespace Slic3r::App::Preview {

using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;
using Yoga::operator""_fpx;

static constexpr size_t zoom_region_size_px = 256;
static constexpr float  zoom_panel_gap      = 5.f;
static constexpr float  zoom_header_height  = 22.f;
static constexpr float  zoom_panel_height =
    zoom_header_height + zoom_panel_gap + float(zoom_region_size_px);

SlaLayerImageWindow::SlaLayerImageWindow(Render::Device& device, Biz::ProjectInteractor& project_interactor)
    : CollapsibleWindow(_u8L("Layer image"), "SlaLayerImageWindow")
    , m_device(device)
    , m_project_interactor(project_interactor)
{
    set_min_height(320_fpx);
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

    // Pixel zoom panel, only visible after a click on the layer image. The image itself is
    // drawn by render_body(), the panel only reserves the space and holds the label and
    // the close button.
    m_zoom_panel = content()->emplace_back<Item>();
    m_zoom_panel->set_orientation(Orientation::Vertical);
    m_zoom_panel->set_gap(zoom_panel_gap);
    m_zoom_panel->set_flex_shrink(0.f);
    m_zoom_panel->set_width(float(zoom_region_size_px));
    m_zoom_panel->set_height(zoom_panel_height);
    m_zoom_panel->set_visible(false);

    m_zoom_header = m_zoom_panel->emplace_back<Item>();
    m_zoom_header->set_orientation(Orientation::Horizontal);
    m_zoom_header->set_justify_content(YGJustifySpaceBetween);
    m_zoom_header->set_align_items(YGAlignCenter);
    m_zoom_header->set_height(zoom_header_height);

    m_zoom_label = m_zoom_header->emplace_back<Text>(_u8L("Zoom"));
    m_zoom_label->set_font_type(Render::ImguiFontType::Bold);

    m_zoom_close_button = m_zoom_header->emplace_back<LayoutButton>(std::string{}, Render::Icon::DeleteBtnIcon);
    m_zoom_close_button->callbacks().action = [this]() { close_zoom(); };
    m_zoom_close_button->set_tooltip(_u8L("Close zoom"));
    m_zoom_close_button->set_width(zoom_header_height);
    m_zoom_close_button->set_height(zoom_header_height);
    m_zoom_close_button->set_content_padding({});
    m_zoom_close_button->set_background_color(Platform::Color::ButtonTransparent);

    set_visible(false);
}

SlaLayerImageWindow::~SlaLayerImageWindow() = default;

void SlaLayerImageWindow::set_slider(DoubleSliderForLayers* slider)
{
    m_slider = slider;
}

size_t SlaLayerImageWindow::current_layer_index() const
{
    if (!m_slider || !m_last_result || m_last_result->slices.empty())
        return 0;

    size_t layer = size_t(std::max(0, m_slider->higher_pos()));
    if (layer >= m_last_result->slices.size())
        layer = m_last_result->slices.size() - 1;
    return layer;
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

    // Get current layer index from the slider's higher thumb (upper bound of layer range)
    int higher_pos = m_slider->higher_pos();
    int lower_pos = m_slider->lower_pos();
    int max_pos = m_slider->max_pos();

    // Check if we need to rebuild the texture
    bool result_changed = m_last_result != result;
    bool layer_changed = m_last_layer_index != size_t(higher_pos);

    bool needs_rebuild = false;
    if (result_changed || layer_changed) {
        m_last_result = result;
        size_t current_layer = current_layer_index();
        m_last_layer_index = current_layer;
        needs_rebuild = true;

        // Render the layer image (max 1024x1024)
        const Domain::ConfigView& printer_config = result->export_data->config;
        m_current_layer_image = ::Slic3r::sla::render_sla_layer_image(
            result->slices[current_layer], printer_config, 1024, 1024);

        rebuild_texture_if_needed();

        // Follow the layer changes until the zoom is closed.
        if (m_zoom_panel->is_visible())
            render_zoom_region(current_layer);
    }

    size_t current_layer = current_layer_index();

    // Update layer info text
    std::string layer_text = _u8L("Layer") + " " + std::to_string(current_layer + 1) + " / " + std::to_string(result->slices.size());
    if (current_layer < result->heights.size()) {
        layer_text += "  Z = " + fmt::format("{:.2f} mm", result->heights[current_layer]);
    }
    m_layer_info_text->set_text(layer_text);

    // Update button states: Previous enabled when higher_pos > lower_pos, Next when higher_pos < max_pos
    m_prev_button->set_enabled(higher_pos > lower_pos);
    m_next_button->set_enabled(higher_pos < max_pos);

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

void SlaLayerImageWindow::rebuild_zoom_texture()
{
    if (!m_zoom_image.has_value() || m_zoom_image->pixels.empty()) {
        return;
    }

    const auto& img = *m_zoom_image;
    size_t w = img.width;
    size_t h = img.height;

    std::vector<uint8_t> rgba_data;
    rgba_data.resize(w * h * 4);
    for (size_t i = 0; i < w * h; ++i) {
        uint8_t g = img.pixels[i];
        rgba_data[i * 4 + 0] = g;
        rgba_data[i * 4 + 1] = g;
        rgba_data[i * 4 + 2] = g;
        rgba_data[i * 4 + 3] = 255;
    }

    bool size_changed = m_zoom_texture_width != w || m_zoom_texture_height != h;
    if (!m_zoom_texture || size_changed) {
        m_zoom_texture = m_device.create_texture();
        m_zoom_texture->set_object_name("sla_layer_image_zoom");
        // Nearest filtering, the pixels are shown 1:1.
        m_zoom_texture->set_filtering(Render::TextureMinFilter::Nearest, Render::TextureMagFilter::Nearest);
        m_zoom_texture->set_wrap_s(Render::TextureWrap::ClampToEdge);
        m_zoom_texture->set_wrap_t(Render::TextureWrap::ClampToEdge);
        m_zoom_texture_width = w;
        m_zoom_texture_height = h;
    }

    using Slic3r::Domain::PixelFormat;
    m_zoom_texture->set_data(
        PixelFormat::RGBA8,
        0,
        static_cast<int>(w),
        static_cast<int>(h),
        rgba_data.data(),
        rgba_data.size()
    );
}

void SlaLayerImageWindow::on_layer_image_clicked()
{
    if (!m_last_result || !m_current_layer_image.has_value())
        return;

    ImVec2 rect_min  = ImGui::GetItemRectMin();
    ImVec2 rect_size = ImGui::GetItemRectSize();
    if (rect_size.x <= 0.f || rect_size.y <= 0.f)
        return;

    // The fitted image covers the whole display, the click position converted to the
    // image coordinates is the position on the display in mm.
    const Domain::ConfigView& printer_config = m_last_result->export_data->config;
    double display_w = printer_config.get<double>("display_width");
    double display_h = printer_config.get<double>("display_height");
    bool portrait = printer_config.get<Domain::SLADisplayOrientation>("display_orientation")
        == Domain::SLADisplayOrientation::sladoPortrait;
    if (portrait)
        std::swap(display_w, display_h);

    double u = (ImGui::GetMousePos().x - rect_min.x) / rect_size.x;
    double v = (ImGui::GetMousePos().y - rect_min.y) / rect_size.y;

    // Same x mirroring the raster uses, see RasterBase::Trafo.
    bool mirror_x = portrait ? !printer_config.get<bool>("display_mirror_x")
                             : printer_config.get<bool>("display_mirror_x");
    if (mirror_x)
        u = 1. - u;

    // The raster always puts the origin of the slice to the bottom of the image.
    m_zoom_center_x_mm = std::clamp(u, 0., 1.) * display_w;
    m_zoom_center_y_mm = std::clamp(1. - v, 0., 1.) * display_h;

    m_zoom_panel->set_visible(true);
    // The panel is a regular layout child, the window has to be tall enough to hold it.
    set_min_height(320_fpx + Unit(zoom_panel_height));
    render_zoom_region(current_layer_index());
    Biz::Platform::PlatformServices::instance().render_request_handler().request_render();
}

void SlaLayerImageWindow::render_zoom_region(size_t layer_index)
{
    if (!m_last_result || m_last_result->slices.empty() || layer_index >= m_last_result->slices.size())
        return;

    m_zoom_image = ::Slic3r::sla::render_sla_layer_image_region(
        m_last_result->slices[layer_index],
        m_last_result->export_data->config,
        m_zoom_center_x_mm,
        m_zoom_center_y_mm,
        zoom_region_size_px,
        zoom_region_size_px);
    rebuild_zoom_texture();
}

void SlaLayerImageWindow::close_zoom()
{
    m_zoom_panel->set_visible(false);
    m_zoom_image.reset();
    set_min_height(320_fpx);
    Biz::Platform::PlatformServices::instance().render_request_handler().request_render();
}

void SlaLayerImageWindow::render_body(const Domain::Vec2f& pos, const Domain::Vec2f& size)
{
    // Render the standard content (layer info, controls)
    CollapsibleWindow::render_body(pos, size);

    const bool zoom_visible = m_zoom_panel->is_visible();

    // Render the image centered in the available space
    if (m_current_layer_image.has_value() && m_texture) {
        const auto& img = *m_current_layer_image;
        float img_w = static_cast<float>(img.width);
        float img_h = static_cast<float>(img.height);

        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (zoom_visible)
            avail.y -= m_zoom_panel->height();
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

        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            on_layer_image_clicked();
    }

    // The native resolution window is drawn below the label and the close button of the zoom panel.
    if (zoom_visible && m_zoom_texture && m_zoom_panel->width() > 0.f) {
        Domain::Vec2f panel_pos = m_zoom_panel->get_global_pos();
        float header_h           = m_zoom_header->height();

        ImGui::SetCursorScreenPos(
            ImVec2(panel_pos.x(), panel_pos.y() + header_h + zoom_panel_gap));
        render_image(
            m_zoom_texture,
            ImVec2(m_zoom_panel->width(), m_zoom_panel->height() - header_h - zoom_panel_gap),
            ImVec2(0, 0),
            ImVec2(1, 1),
            ImVec4(0, 0, 0, 1),
            ImVec4(1, 1, 1, 1));
    }
}

void SlaLayerImageWindow::on_prev_layer()
{
    if (m_slider) {
        int higher_pos = m_slider->higher_pos();
        int lower_pos = m_slider->lower_pos();
        int new_pos = higher_pos - 1;
        if (new_pos < lower_pos) new_pos = lower_pos;
        m_slider->set_higher_pos(new_pos);
    }
}

void SlaLayerImageWindow::on_next_layer()
{
    if (m_slider) {
        int higher_pos = m_slider->higher_pos();
        int max_pos = m_slider->max_pos();
        int new_pos = higher_pos + 1;
        if (new_pos > max_pos) new_pos = max_pos;
        m_slider->set_higher_pos(new_pos);
    }
}

} // namespace Slic3r::App::Preview
