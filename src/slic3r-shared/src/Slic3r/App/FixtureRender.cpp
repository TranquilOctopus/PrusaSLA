#include "Slic3r/App/FixtureRender.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "Slic3r/App/Theme.hpp"
#include "Slic3r/App/ThemeTypes.hpp"
#include "Slic3r/Biz/Algorithms/PNGReadWrite.hpp"
#include "Slic3r/Log.hpp"

namespace Slic3r::App {

using Domain::ColorRGBA;

std::optional<FixtureView> fixture_view_from_string(std::string_view name)
{
    if (name == "prepare")
        return FixtureView::Prepare;
    if (name == "preview")
        return FixtureView::Preview;
    return std::nullopt;
}

std::string fixture_view_to_string(FixtureView view)
{
    return view == FixtureView::Preview ? "preview" : "prepare";
}

std::optional<Domain::Size> parse_render_size(std::string_view text)
{
    constexpr int max_side = 8192;
    const size_t separator = text.find('x');
    if (separator == std::string_view::npos)
        return std::nullopt;
    const std::string width  = std::string(text.substr(0, separator));
    const std::string height = std::string(text.substr(separator + 1));
    if (width.empty() || height.empty())
        return std::nullopt;
    // Digits only: a std::stoi would take a prefix and leave the rest to nobody.
    const auto digits_only = [](const std::string& s)
    { return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); }); };
    if (!digits_only(width) || !digits_only(height))
        return std::nullopt;
    const long w = std::stol(width);
    const long h = std::stol(height);
    if (w < 1 || h < 1 || w > max_side || h > max_side)
        return std::nullopt;
    return Domain::Size{int(w), int(h)};
}

namespace {

/// sRGB to linear light, per channel, as in IEC 61966-2-1. The input is 0..1.
double srgb_to_linear(double channel)
{
    return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
}

double round_to_hundredth(double value)
{
    return std::round(value * 100.0) / 100.0;
}

uint8_t blend(uint8_t source, uint8_t destination, double alpha)
{
    const double value = source * alpha + destination * (1.0 - alpha);
    return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0, 255.0)));
}

} // namespace

double cie_lightness(const ColorRGBA& color)
{
    const double y = 0.2126 * srgb_to_linear(color.r())
        + 0.7152 * srgb_to_linear(color.g())
        + 0.0722 * srgb_to_linear(color.b());
    return y > 216.0 / 24389.0 ? 116.0 * std::cbrt(y) - 16.0 : 903.3 * y;
}

ColorRGBA scene_background(const Theme& theme)
{
    return theme.color(Platform::Color::SceneBgBottom);
}

std::vector<RoleLightness> scene_lightness(const Theme& theme)
{
    // The tokens, not the colours: the palette hex values live in Theme.cpp and nowhere else
    // (PLAN 2.1 rule 1), and a render has to be described by the tokens it was drawn with.
    struct Entry
    {
        std::string role;
        std::string token;
        Platform::Color color;
    };

    static const std::vector<Entry> entries{
        {"model", "SlaModelResin", Platform::Color::SlaModelResin},
        {"supports", "SlaSupport", Platform::Color::SlaSupport},
        {"pad", "SlaPad", Platform::Color::SlaPad},
        {"background_top", "SceneBgTop", Platform::Color::SceneBgTop},
        {"background_bottom", "SceneBgBottom", Platform::Color::SceneBgBottom},
    };

    std::vector<RoleLightness> ret;
    ret.reserve(entries.size());
    for (const Entry& entry : entries) {
        const ColorRGBA& color = theme.color(entry.color);
        ret.push_back(RoleLightness{entry.role, entry.token, color, cie_lightness(color)});
    }
    return ret;
}

std::vector<uint8_t> composite_over(const Domain::Image& image, const ColorRGBA& background)
{
    std::vector<uint8_t> ret;
    const size_t bytes_per_pixel = Domain::pixel_format_bytes_per_pixel(image.format());
    if (image.pixels.empty() || bytes_per_pixel < 3) {
        SPDLOG_ERROR("Fixture render has no pixels or an unexpected format, nothing to composite.");
        return ret;
    }
    ret.reserve(image.pixels.size() * 3 / bytes_per_pixel);
    for (size_t i = 0; i + bytes_per_pixel <= image.pixels.size(); i += bytes_per_pixel) {
        // A format without an alpha channel is already opaque.
        const double alpha = bytes_per_pixel > 3 ? image.pixels[i + 3] / 255.0 : 1.0;
        ret.push_back(blend(image.pixels[i + 0], background.r_uchar(), alpha));
        ret.push_back(blend(image.pixels[i + 1], background.g_uchar(), alpha));
        ret.push_back(blend(image.pixels[i + 2], background.b_uchar(), alpha));
    }
    return ret;
}

std::string render_sidecar(FixtureView view, const Domain::Image& image, const Theme& theme)
{
    nlohmann::ordered_json sidecar;
    sidecar["tool"]                        = "M6.2 visual regression render";
    sidecar["view"]                        = fixture_view_to_string(view);
    sidecar["width"]                       = image.width();
    sidecar["height"]                      = image.height();
    sidecar["composited_background_token"] = "SceneBgBottom";
    nlohmann::ordered_json lightness;
    for (const RoleLightness& role : scene_lightness(theme)) {
        lightness[role.role] = nlohmann::ordered_json{
            {"token", role.token},
            {"rgb", {role.color.r_uchar(), role.color.g_uchar(), role.color.b_uchar()}},
            {"L", round_to_hundredth(role.l)},
        };
    }
    sidecar["lightness"] = lightness;
    return sidecar.dump(2);
}

bool write_fixture_render(
    const Domain::Image& image,
    const std::string& png_path,
    FixtureView view,
    const Theme& theme
)
{
    const std::vector<uint8_t> rgb = composite_over(image, scene_background(theme));
    if (rgb.size() != size_t(image.width()) * size_t(image.height()) * 3) {
        SPDLOG_ERROR(
            "Fixture render did not give one RGB triple per pixel, {} bytes for {} pixels.",
            rgb.size(),
            size_t(image.width()) * size_t(image.height())
        );
        return false;
    }
    if (!png::write_rgb_to_file(png_path, size_t(image.width()), size_t(image.height()), rgb)) {
        SPDLOG_ERROR("Failed to write the fixture render to {}.", png_path);
        return false;
    }
    const std::string sidecar_path = png_path + ".json";
    std::ofstream sidecar(sidecar_path);
    if (!sidecar) {
        SPDLOG_ERROR("Failed to open the render sidecar for writing: {}.", sidecar_path);
        return false;
    }
    sidecar << render_sidecar(view, image, theme) << "\n";
    if (!sidecar) {
        SPDLOG_ERROR("Failed to write the render sidecar to {}.", sidecar_path);
        return false;
    }
    SPDLOG_INFO(
        "Wrote the {} render to {} and its sidecar to {}",
        fixture_view_to_string(view),
        png_path,
        sidecar_path
    );
    return true;
}

} // namespace Slic3r::App
