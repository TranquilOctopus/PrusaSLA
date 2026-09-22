#include "Slic3r/App/Preview/SlaLayerViewPanel.hpp"

#include "Slic3r/App/Imgui/ImguiExtension.hpp"
#include "Slic3r/App/Render/Context.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Render/TextureManager.hpp"
#include "Slic3r/App/Yoga/ImGuiUtils.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/Biz/Algorithms/PNGReadWrite.hpp"
#include "Slic3r/Domain/Color.hpp"

#include <fmt/core.h>
#include <imgui.h>

namespace Slic3r::App::Preview {

SlaLayerViewPanel::SlaLayerViewPanel() : Yoga::Window("SlaLayerViewPanel"), m_cache(4)
{
    set_padding({10.f, 10.f});
    set_orientation(Yoga::Orientation::Vertical);
    set_gap(5.f);
    set_min_width(240.f);
    set_min_height(240.f);
    set_flex_grow(1.f);

    m_layer_info_text = emplace_back<Yoga::Text>("", Yoga::Text::Align::Center);
    m_layer_info_text->set_wrap(true);

    m_no_preview_text = emplace_back<Yoga::Text>(
        _u8L("No preview available for this format"), Yoga::Text::Align::Center);
    m_no_preview_text->set_wrap(true);
    m_no_preview_text->set_visible(false);

    m_no_data_text = emplace_back<Yoga::Text>(
        _u8L("No layer data available"), Yoga::Text::Align::Center);
    m_no_data_text->set_wrap(true);
    m_no_data_text->set_visible(false);
}

void SlaLayerViewPanel::set_sla_result(const Biz::Slicing::SLAResult* result)
{
    m_sla_result = result;
    m_cache.clear();
    m_texture.reset();
    on_sla_result_changed();
}

void SlaLayerViewPanel::set_current_layer(size_t layer_index, float layer_z)
{
    m_current_layer_index = layer_index;
    m_current_layer_z = layer_z;
    decode_and_cache_layer(layer_index);
}

void SlaLayerViewPanel::on_sla_result_changed()
{
    if (!m_sla_result || m_sla_result->export_data == nullptr) {
        m_layer_info_text->set_visible(false);
        m_no_preview_text->set_visible(false);
        m_no_data_text->set_visible(true);
        m_texture.reset();
        return;
    }

    size_t layer_count = m_sla_result->export_data->files.data.size();
    if (layer_count == 0) {
        m_layer_info_text->set_visible(false);
        m_no_preview_text->set_visible(false);
        m_no_data_text->set_visible(true);
        m_texture.reset();
        return;
    }

    if (m_current_layer_index >= layer_count)
        m_current_layer_index = layer_count - 1;

    m_layer_info_text->set_visible(true);
    m_no_preview_text->set_visible(false);
    m_no_data_text->set_visible(false);

    char buf[64];
    snprintf(buf, sizeof(buf), _u8L("Layer %zu / %zu\nZ: %.3f mm"),
             m_current_layer_index + 1, layer_count, m_current_layer_z);
    m_layer_info_text->set_text(buf);

    decode_and_cache_layer(m_current_layer_index);
}

void SlaLayerViewPanel::render_body(const Yoga::Vec2f& pos, const Yoga::Vec2f& size)
{
    if (!is_visible())
        return;

    Yoga::Window::render_body(pos, size);

    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x <= 0 || avail.y <= 0)
        return;

    if (m_texture) {
        float img_aspect = float(m_texture->width()) / float(m_texture->height());
        float avail_aspect = avail.x / avail.y;

        ImVec2 img_size;
        if (img_aspect > avail_aspect) {
            img_size.x = avail.x;
            img_size.y = avail.x / img_aspect;
        } else {
            img_size.y = avail.y;
            img_size.x = avail.y * img_aspect;
        }

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail.x - img_size.x) * 0.5f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (avail.y - img_size.y) * 0.5f);

        ImGui::Image(
            (ImTextureID)(intptr_t)m_texture.get(),
            img_size,
            ImVec2(0, 0),
            ImVec2(1, 1),
            ImVec4(0, 0, 0, 0),
            ImVec4(1, 1, 1, 1)
        );
    } else if (m_no_preview_text->visible()) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail.x * 0.5f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + avail.y * 0.5f);
        ImGui::TextUnformatted(m_no_preview_text->text().c_str());
    }
}

void SlaLayerViewPanel::decode_and_cache_layer(size_t layer_index)
{
    if (!m_sla_result || m_sla_result->export_data == nullptr)
        return;

    const auto& files = m_sla_result->export_data->files;
    if (layer_index >= files.data.size())
        return;

    auto cached = m_cache.get(layer_index);
    if (cached.has_value()) {
        if (!m_texture || m_texture->width() != cached->width || m_texture->height() != cached->height) {
            m_texture_name = fmt::format("sla_layer_view_{}_{}x{}", layer_index, cached->width, cached->height);
            m_texture = std::make_unique<Render::Texture>(
                Render::Context::get().texture_manager().get_or_create_dynamic(
                    m_texture_name, Domain::PixelFormat::RGBA8, cached->width, cached->height));
        }
        m_texture->set_data(Domain::PixelFormat::RGBA8, 0, cached->width, cached->height,
                           cached->image_data.data(), cached->image_data.size());
        m_no_preview_text->set_visible(false);
        return;
    }

    const auto& file_data = files.data[layer_index];
    std::vector<uint8_t> image_data;
    unsigned width = 0, height = 0;

    bool decoded = false;
    if (files.type == Slic3r::Sla::FileDataType::sl1_png) {
        decoded = try_decode_png(file_data, image_data, width, height);
    }

    if (decoded) {
        m_cache.put(layer_index, std::move(image_data), width, height);
        on_sla_result_changed();
    } else {
        m_no_preview_text->set_visible(true);
        m_texture.reset();
    }
}

bool SlaLayerViewPanel::try_decode_png(const Slic3r::Sla::OutputFiles::FileData& file_data,
                                       std::vector<uint8_t>& out_image_data,
                                       unsigned& out_width,
                                       unsigned& out_height)
{
    std::string png_data(reinterpret_cast<const char*>(file_data.data()), file_data.size());
    return Slic3r::png::decode_png(png_data, out_image_data, out_width, out_height);
}

} // namespace Slic3r::App::Preview