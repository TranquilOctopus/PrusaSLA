#include "Slic3r/App/Preview/SlaLayerImageWindow.hpp"

#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/SlaIssueRows.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
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
#include <libslic3r/SLA/LayerStats.hpp>

#include <imgui/imgui.h>

#include <fmt/format.h>

#include <algorithm>
#include <iterator>

namespace Slic3r::App::Preview {

using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;
using Yoga::operator""_fpx;

static constexpr size_t zoom_region_size_px = 256;
static constexpr float  zoom_panel_gap      = 5.f;
static constexpr float  zoom_header_height  = 22.f;
static constexpr float  zoom_panel_height =
    zoom_header_height + zoom_panel_gap + float(zoom_region_size_px);

// Height of a single statistics plot in the layer image window
static constexpr float plot_height = 50.f;

// The island list is capped, both in the number of rows and in its height so that a print
// with hundreds of islands cannot push the rest of the window away.
static constexpr size_t max_island_rows         = 50;
static constexpr float  islands_list_max_height = 140.f;

// Ring drawn on an island in the layer image, in screen pixels.
static constexpr float island_marker_radius    = 6.f;
static constexpr float island_marker_thickness = 2.f;

namespace {

// Display geometry of the printer, resolved for the display orientation the same way
// sla::render_sla_layer_image() does it.
struct DisplayMapping
{
    double width_mm{0.};  //< display size along the image x axis
    double height_mm{0.}; //< display size along the image y axis
    bool mirror_x{false};
};

DisplayMapping display_mapping(const Domain::ConfigView& printer_config)
{
    DisplayMapping mapping;

    double width  = printer_config.get<double>("display_width");
    double height = printer_config.get<double>("display_height");
    bool portrait = printer_config.get<Domain::SLADisplayOrientation>("display_orientation")
        == Domain::SLADisplayOrientation::sladoPortrait;
    if (portrait)
        std::swap(width, height);

    mapping.width_mm  = width;
    mapping.height_mm = height;
    // Same x mirroring the raster uses, see RasterBase::Trafo.
    mapping.mirror_x = portrait ? !printer_config.get<bool>("display_mirror_x")
                                : printer_config.get<bool>("display_mirror_x");

    return mapping;
}

// Slice mm to a pixel in the fitted image rect. The raster puts the origin of the slice to
// the bottom of the image and mirrors around its edges, the same way the click handler of
// the image reads the position back.
ImVec2 bed_mm_to_image_px(
    const DisplayMapping& mapping,
    const ImVec2& rect_min,
    const ImVec2& rect_size,
    double x_mm,
    double y_mm
)
{
    if (mapping.width_mm <= 0. || mapping.height_mm <= 0.)
        return rect_min;

    double u = x_mm / mapping.width_mm;
    if (mapping.mirror_x)
        u = 1. - u;
    const double v = 1. - y_mm / mapping.height_mm;

    return ImVec2(rect_min.x + float(u) * rect_size.x, rect_min.y + float(v) * rect_size.y);
}

// The vat film the peel force coefficients came from, spelled as the vat film combo box spells
// it. These are the chemical names of the films, not words to translate.
std::string vat_film_name(const Domain::ConfigView& config)
{
    switch (config.get<Domain::sla::VatFilmType>("vat_film_type")) {
    case Domain::sla::VatFilmType::nFEP: return "nFEP";
    case Domain::sla::VatFilmType::PFA:  return "PFA";
    case Domain::sla::VatFilmType::ACF:  return "ACF";
    case Domain::sla::VatFilmType::FEP:  break;
    }
    return "FEP";
}

} // namespace

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

    // Island list, the last child of the content so that it sits below the image and the
    // charts. Its rows are built from the issues of a result, see rebuild_island_list().
    m_islands_panel = content()->emplace_back<Item>();
    m_islands_panel->set_orientation(Orientation::Vertical);
    m_islands_panel->set_gap(4.f);
    m_islands_panel->set_flex_shrink(0.f);
    m_islands_panel->set_padding(Paddings(10.f, 5.f, 10.f, 5.f));
    m_islands_panel->set_visible(false);

    m_islands_header = m_islands_panel->emplace_back<Item>();
    m_islands_header->set_orientation(Orientation::Horizontal);
    m_islands_header->set_justify_content(YGJustifySpaceBetween);
    m_islands_header->set_align_items(YGAlignCenter);
    m_islands_header->set_gap(6.f);
    m_islands_header->set_flex_shrink(0.f);

    m_islands_title = m_islands_header->emplace_back<Text>(_u8L("Islands"));
    m_islands_title->set_font_type(Render::ImguiFontType::Bold);
    m_islands_title->set_flex_shrink(1.f);

    m_islands_prev_button = m_islands_header->emplace_back<LayoutButton>(_u8L("Previous island"));
    m_islands_prev_button->set_content_padding({8.f, 2.f});
    m_islands_prev_button->set_tooltip(_u8L("Jump to the closest island below the current layer"));
    m_islands_prev_button->callbacks().action = [this]() { on_prev_island(); };

    m_islands_next_button = m_islands_header->emplace_back<LayoutButton>(_u8L("Next island"));
    m_islands_next_button->set_content_padding({8.f, 2.f});
    m_islands_next_button->set_tooltip(_u8L("Jump to the closest island above the current layer"));
    m_islands_next_button->callbacks().action = [this]() { on_next_island(); };

    m_islands_rows = m_islands_panel->emplace_back<ScrollArea>();
    m_islands_rows->set_orientation(Orientation::Vertical);
    m_islands_rows->set_max_height(islands_list_max_height);
    m_islands_rows->set_flex_shrink(0.f);

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

    // A row of the sidebar issues list asked for a layer of this bed (M1.11b). Answered before
    // the layer is read below, so the image of the asked layer is built in this same update.
    if (std::optional<size_t> layer = AppServices::instance().sla_layer_jump().take(
            m_project_interactor.selected_bed_slicing_id()
        ))
    {
        go_to_layer(*layer);
    }

    set_visible(true);

    // Get current layer index from the slider's higher thumb (upper bound of layer range)
    int higher_pos = m_slider->higher_pos();
    int lower_pos = m_slider->lower_pos();
    int max_pos = m_slider->max_pos();

    // Check if we need to rebuild the texture
    bool result_changed = m_last_result != result;
    bool layer_changed = m_last_layer_index != size_t(higher_pos);

    if (result_changed) {
        // The statistics are owned by the result, alias them to keep them alive while we draw
        m_result_data = result->export_data;
        auto alias    = [this](const std::vector<float>& values) {
            return std::shared_ptr<const std::vector<float>>(m_result_data, &values);
        };
        m_layer_areas      = m_result_data ? alias(m_result_data->layer_areas) : nullptr;
        m_layer_peel_force = m_result_data ? alias(m_result_data->layer_peel_force) : nullptr;
        rebuild_island_list();
    }

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

    // The chart marker has to follow the layer the image above was rendered for.
    m_current_layer = m_last_layer_index;

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

    // The island list can be walked through even while the layer changes under it.
    m_islands_prev_button->set_enabled(prev_island_layer().has_value());
    m_islands_next_button->set_enabled(next_island_layer().has_value());

    update_min_height();

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
    const DisplayMapping mapping = display_mapping(m_last_result->export_data->config);

    double u = (ImGui::GetMousePos().x - rect_min.x) / rect_size.x;
    double v = (ImGui::GetMousePos().y - rect_min.y) / rect_size.y;

    if (mapping.mirror_x)
        u = 1. - u;

    // The raster always puts the origin of the slice to the bottom of the image.
    m_zoom_center_x_mm = std::clamp(u, 0., 1.) * mapping.width_mm;
    m_zoom_center_y_mm = std::clamp(1. - v, 0., 1.) * mapping.height_mm;

    m_zoom_panel->set_visible(true);
    // The panel is a regular layout child, the window has to be tall enough to hold it.
    update_min_height();
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
    update_min_height();
    Biz::Platform::PlatformServices::instance().render_request_handler().request_render();
}

void SlaLayerImageWindow::render_body(const Domain::Vec2f& pos, const Domain::Vec2f& size)
{
    // Render the standard content (layer info, controls)
    CollapsibleWindow::render_body(pos, size);

    const bool zoom_visible = m_zoom_panel->is_visible();

    // The island list is a regular layout child at the bottom of the content, both the
    // image and the charts have to stay above it.
    const float islands_height = m_islands_panel->is_visible() ? m_islands_panel->height() : 0.f;

    const bool has_areas = m_layer_areas && !m_layer_areas->empty();
    const bool has_peel  = m_layer_peel_force && !m_layer_peel_force->empty();

    const int   plot_count  = static_cast<int>(has_areas) + static_cast<int>(has_peel);
    const float text_height = ImGui::GetTextLineHeightWithSpacing();

    // Reserve a fixed strip at the bottom for the charts, the image takes the rest
    const float stats_height = plot_count * plot_height + (plot_count > 0 ? text_height : 0.f);

    const ImVec2 region      = ImGui::GetContentRegionAvail();
    const ImVec2 region_min  = ImGui::GetCursorScreenPos();
    const ImVec2 region_max  = region_min + region;

    // Stacked from the bottom of the window up: island list, zoom panel, charts. The image
    // takes whatever is left on top.
    const float zoom_height  = zoom_visible ? m_zoom_panel->height() : 0.f;
    const float bottom_strip = islands_height + zoom_height + stats_height;

    // The image is fitted into the space left above the chart strip, between the controls
    // and the zoom panel on top.
    ImVec2 avail = region;
    avail.y      = std::max(avail.y - bottom_strip, 0.f);

    // Render the image centered in that space. The click to mm mapping uses the rect of
    // this image, so the image must be drawn as the last item before handling the click.
    if (m_current_layer_image.has_value() && m_texture && avail.y > 0.f) {
        const auto& img = *m_current_layer_image;
        float img_w = static_cast<float>(img.width);
        float img_h = static_cast<float>(img.height);

        float scale = std::min(avail.x / img_w, avail.y / img_h);
        scale = std::max(scale, 0.1f);

        float draw_w = img_w * scale;
        float draw_h = img_h * scale;

        // Center the image in the available space
        float center_offset_x = (avail.x - draw_w) * 0.5f;
        float center_offset_y = (avail.y - draw_h) * 0.5f;
        ImGui::SetCursorScreenPos(ImVec2(region_min.x + center_offset_x, region_min.y + center_offset_y));

        ImGui::PushStyleVar(ImGuiStyleVar_ImageRounding, 0.f);
        render_image(m_texture, ImVec2(draw_w, draw_h), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 1), ImVec4(1, 1, 1, 1));
        ImGui::PopStyleVar();

        // The rect of the image is what the islands are mapped onto.
        const ImVec2 image_rect_min  = ImGui::GetItemRectMin();
        const ImVec2 image_rect_size = ImGui::GetItemRectSize();

        render_island_markers(image_rect_min.x, image_rect_min.y, image_rect_size.x, image_rect_size.y);


        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            on_layer_image_clicked();
    }

    // The charts fill the reserved strip at the bottom of the window, right above the
    // zoom panel and the island list.
    if (plot_count > 0) {
        ImGui::SetCursorScreenPos(ImVec2(region_min.x, region_max.y - bottom_strip));

        if (has_areas) {
            render_plot(
                *m_layer_areas,
                _u8L("Area (mm²)"),
                m_current_layer,
                Platform::Color::SlaLayerArea,
                region.x,
                plot_height
            );
        }
        if (has_peel) {
            render_plot(
                *m_layer_peel_force,
                // The film in the title: the force is in the same newtons for every film, but the
                // coefficients behind it are not, so a chart without it says nothing about which
                // film the numbers came from.
                fmt::format(
                    fmt::runtime(_u8L("Peel force (N, {0})")),
                    m_result_data ? vat_film_name(m_result_data->config) : std::string()),
                m_current_layer,
                Platform::Color::AccentSecondary,
                region.x,
                plot_height
            );
        }

        ImGui::TextUnformatted(layer_stats_text().c_str());
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

void SlaLayerImageWindow::render_plot(
    const std::vector<float>& values,
    const std::string& title,
    size_t current_layer,
    Platform::Color line_color,
    float width,
    float height
)
{
    if (values.empty()) {
        return;
    }

    // Scale from zero so that the peaks (high cross sections) are easy to spot
    const float max_value = *std::max_element(values.begin(), values.end());

    // The label is drawn on top of the plot, hide it from the plot itself
    const std::string id = "##" + title;

    ImGui::PushStyleColor(ImGuiCol_PlotLines, m_theme->color_imgui(line_color).Value);
    ImGui::PlotLines(
        id.c_str(),
        values.data(),
        static_cast<int>(values.size()),
        0,
        title.c_str(),
        0.f,
        max_value > 0.f ? max_value : 1.f,
        ImVec2(width, height)
    );
    ImGui::PopStyleColor();

    // Marker of the layer currently shown in the slider
    const ImVec2 padding   = ImGui::GetStyle().FramePadding;
    const ImVec2 inner_min = ImGui::GetItemRectMin() + padding;
    const ImVec2 inner_max = ImGui::GetItemRectMax() - padding;
    if (inner_max.x <= inner_min.x) {
        return;
    }

    float t = 0.f;
    if (values.size() > 1) {
        t = static_cast<float>(current_layer) / static_cast<float>(values.size() - 1);
        t = std::clamp(t, 0.f, 1.f);
    }

    const float  x     = inner_min.x + t * (inner_max.x - inner_min.x);
    const ImU32  color = ImU32(m_theme->color_imgui(Platform::Color::Text));
    ImGui::GetWindowDrawList()->AddLine(ImVec2(x, inner_min.y), ImVec2(x, inner_max.y), color);
}

std::string SlaLayerImageWindow::layer_stats_text() const
{
    std::string text;

    if (m_layer_areas && !m_layer_areas->empty() && m_current_layer < m_layer_areas->size()) {
        const float area = (*m_layer_areas)[m_current_layer];
        text += fmt::format(fmt::runtime(_u8L("Area {:.1f} mm²")), area);
    }

    if (m_layer_peel_force && !m_layer_peel_force->empty() && m_current_layer < m_layer_peel_force->size()) {
        const float peel = (*m_layer_peel_force)[m_current_layer];
        if (!text.empty()) {
            text += "  ·  ";
        }
        text += fmt::format(fmt::runtime(_u8L("Peel {:.1f} N")), peel);
        // The layer is over the peel_force_warning: the chart has no room for a threshold line,
        // so the line of text below it says so instead.
        const double warning = peel_warning_n();
        if (warning > 0. && double(peel) > warning) {
            text += fmt::format(fmt::runtime(_u8L(" (over {:.1f} N)")), warning);
        }
    }

    return text;
}

double SlaLayerImageWindow::peel_warning_n() const
{
    if (!m_result_data)
        return 0.;

    // The same resolution of the setting the slicer used, see peel_force_warning_n() in
    // SLA/LayerStats.hpp: a negative setting takes the default of the vat film.
    const Domain::ConfigView& config = m_result_data->config;
    ::Slic3r::sla::PeelForceSettings settings;
    settings.film                 = config.get<Domain::sla::VatFilmType>("vat_film_type");
    settings.area_coefficient     = config.get<double>("peel_area_coefficient");
    settings.perimeter_coefficient = config.get<double>("peel_perimeter_coefficient");
    return ::Slic3r::sla::peel_force_warning_n(
        config.get<double>("peel_force_warning"), settings.coefficients());
}

void SlaLayerImageWindow::rebuild_island_list()
{
    m_islands.clear();
    m_island_layers.clear();

    // Drop the rows of the previous result
    while (m_islands_rows->object_count() > 0)
        m_islands_rows->remove(m_islands_rows->get_object(0));

    if (m_result_data) {
        for (const Biz::Slicing::Sla::SlaIssue& issue : m_result_data->issues) {
            if (issue.kind != Biz::Slicing::Sla::SlaIssue::Kind::Island)
                continue;
            m_islands.push_back(
                Island{
                    issue.layer,
                    issue.position.x(),
                    issue.position.y(),
                    sla_issue_area_mm2(issue.note),
                    issue.object_name
                }
            );
        }
    }

    std::sort(m_islands.begin(), m_islands.end(), [](const Island& a, const Island& b) {
        if (a.layer != b.layer) return a.layer < b.layer;
        if (a.y_mm != b.y_mm) return a.y_mm < b.y_mm;
        return a.x_mm < b.x_mm;
    });

    for (const Island& island : m_islands)
        m_island_layers.push_back(island.layer);
    m_island_layers.erase(std::unique(m_island_layers.begin(), m_island_layers.end()), m_island_layers.end());

    const size_t shown = std::min(m_islands.size(), max_island_rows);
    for (size_t i = 0; i < shown; ++i) {
        const size_t layer = m_islands[i].layer;

        Item* row = m_islands_rows->emplace_back<Item>();
        row->set_orientation(Orientation::Horizontal);
        row->set_align_items(YGAlignCenter);
        row->set_justify_content(YGJustifySpaceBetween);
        row->set_gap(6.f);
        row->set_flex_shrink(0.f);

        Text* label = row->emplace_back<Text>(island_row_text(m_islands[i]));
        label->set_text_color(m_theme->color_imgui(Platform::Color::SlaIslandWarning));
        label->set_flex_shrink(1.f);

        LayoutButton* go_button = row->emplace_back<LayoutButton>(_u8L("Go"));
        go_button->set_content_padding({8.f, 2.f});
        go_button->set_tooltip(_u8L("Jump to this layer"));
        go_button->callbacks().action = [this, layer]() { go_to_layer(layer); };
    }

    if (m_islands.size() > shown) {
        Text* more = m_islands_rows->emplace_back<Text>(
            fmt::format(fmt::runtime(_u8L("and {0} more")), m_islands.size() - shown));
        more->set_flex_shrink(0.f);
    }

    m_islands_title->set_text(fmt::format(fmt::runtime(_u8L("Islands ({0})")), m_islands.size()));
    m_islands_panel->set_visible(!m_islands.empty());
}

std::string SlaLayerImageWindow::island_row_text(const Island& island) const
{
    // The window shows 1 based layer numbers, so the list does the same.
    std::string text;
    if (island.object_name.empty()) {
        // TRN: One row of the island list in the layer image window. {0} is the number of the
        // layer, counted from one as the user counts layers.
        text = fmt::format(fmt::runtime(_u8L("Layer {0}")), island.layer + 1);
    } else {
        // TRN: One row of the island list in the layer image window, for an island the slicer
        // could put on a model. {0} is the name of the model, {1} the number of the layer,
        // counted from one as the user counts layers.
        text = fmt::
            format(fmt::runtime(_u8L("{0} - Layer {1}")), island.object_name, island.layer + 1);
    }
    if (island.area_mm2)
        text += fmt::format(fmt::runtime(_u8L(" · {0:.1f} mm²")), *island.area_mm2);
    text += fmt::format(fmt::runtime(_u8L(" at ({0:.1f}, {1:.1f})")), island.x_mm, island.y_mm);
    return text;
}

std::optional<size_t> SlaLayerImageWindow::prev_island_layer() const
{
    if (m_island_layers.empty())
        return std::nullopt;

    // The closest island layer strictly below the current one.
    const size_t current = current_layer_index();
    const auto   it      = std::lower_bound(m_island_layers.begin(), m_island_layers.end(), current);
    if (it == m_island_layers.begin())
        return std::nullopt;
    return *std::prev(it);
}

std::optional<size_t> SlaLayerImageWindow::next_island_layer() const
{
    if (m_island_layers.empty())
        return std::nullopt;

    // The closest island layer strictly above the current one.
    const size_t current = current_layer_index();
    const auto   it      = std::upper_bound(m_island_layers.begin(), m_island_layers.end(), current);
    if (it == m_island_layers.end())
        return std::nullopt;
    return *it;
}

void SlaLayerImageWindow::go_to_layer(size_t layer)
{
    if (m_slider)
        m_slider->set_higher_pos(int(layer));
}

void SlaLayerImageWindow::on_prev_island()
{
    if (std::optional<size_t> layer = prev_island_layer())
        go_to_layer(*layer);
}

void SlaLayerImageWindow::on_next_island()
{
    if (std::optional<size_t> layer = next_island_layer())
        go_to_layer(*layer);
}

void SlaLayerImageWindow::update_min_height()
{
    Unit min_height(320.f, Unit::Type::FigmaPixel);

    if (m_zoom_panel->is_visible()) {
        Unit zoom_height(zoom_panel_height, Unit::Type::FigmaPixel);
        min_height += zoom_height;
    }
    if (m_islands_panel->is_visible()) {
        Unit islands_height(m_islands_panel->height(), Unit::Type::FigmaPixel);
        min_height += islands_height;
    }

    set_min_height(min_height);
}

void SlaLayerImageWindow::render_island_markers(
    float rect_min_x,
    float rect_min_y,
    float rect_width,
    float rect_height
) const
{
    if (m_islands.empty() || !m_last_result || !m_last_result->export_data)
        return;
    if (rect_width <= 0.f || rect_height <= 0.f)
        return;

    const DisplayMapping mapping = display_mapping(m_last_result->export_data->config);

    const ImVec2 rect_min(rect_min_x, rect_min_y);
    const ImVec2 rect_size(rect_width, rect_height);
    const ImU32 color = ImU32(m_theme->color_imgui(Platform::Color::SlaIslandWarning));
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    for (const Island& island : m_islands) {
        if (island.layer != m_current_layer)
            continue;
        draw_list->AddCircle(
            bed_mm_to_image_px(mapping, rect_min, rect_size, island.x_mm, island.y_mm),
            island_marker_radius,
            color,
            0,
            island_marker_thickness);
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
