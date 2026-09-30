#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Slic3r/Domain/Color.hpp"
#include "Slic3r/Domain/Image.hpp"
#include "Slic3r/Domain/Size.hpp"

namespace Slic3r::App {

class Theme;

/**
 * @brief Which view of the plater a fixture render shows (roadmap M6.2, PLAN G3).
 *
 * Prepare is the bed with the models on it, Preview is the sliced print. Both are rendered
 * offscreen through the thumbnail path, so a render is the scene without the window around it.
 */
enum class FixtureView
{
    Prepare,
    Preview,
};

/// The name of a view on the command line, and the one that goes into a sidecar.
std::optional<FixtureView> fixture_view_from_string(std::string_view name);
std::string fixture_view_to_string(FixtureView view);

/**
 * @brief A "WIDTHxHEIGHT" string, the way --render-size takes a size.
 *
 * No value when the text is not a size, and when a side is not in 1..8192: a render is a
 * multisampled framebuffer, and a mistyped size should say so instead of asking for a gigabyte.
 */
std::optional<Domain::Size> parse_render_size(std::string_view text);

/**
 * @brief CIE 1976 L* of an sRGB colour: 0 for black, 100 for white.
 *
 * PLAN 2.1 wants the model, the supports and the pad to be told apart by lightness and not by
 * hue, and PLAN G3 wants that checked. The grayscale check in doc/sla-fork/tools/visual_diff.py
 * compares these numbers, so this is the same formula the tool uses.
 */
double cie_lightness(const Domain::ColorRGBA& color);

/// One role of the grayscale check: a name, the token it is drawn with, and what that token is.
struct RoleLightness
{
    std::string role;
    std::string token;
    Domain::ColorRGBA color;
    double l{};
};

/// The darkest end of the scene background, which is also what a transparent render is put over.
Domain::ColorRGBA scene_background(const Theme& theme);

/**
 * @brief Every role the grayscale check compares, in a stable order: the model, the supports,
 * the pad, then both ends of the background gradient.
 *
 * The colours are resolved from the theme tokens, never from a hex value written here: the
 * palette lives in Theme.cpp alone. That is what lets a render and its sidecar describe the
 * colours the app really drew with, in both themes.
 */
std::vector<RoleLightness> scene_lightness(const Theme& theme);

/// The RGB bytes of an RGBA8 image with everything composited over @p background.
std::vector<uint8_t>
composite_over(const Domain::Image& image, const Domain::ColorRGBA& background);

/**
 * @brief The sidecar of a render: which view, how big, and the L* of every role.
 *
 * Read by doc/sla-fork/tools/visual_diff.py, which owns the check and the threshold. Written as
 * ordered JSON so a diff of two sidecars is readable.
 */
std::string render_sidecar(FixtureView view, const Domain::Image& image, const Theme& theme);

/**
 * @brief Writes a render to @p png_path as RGB and its sidecar to <png_path>.json next to it.
 *
 * An RGB PNG, not RGBA: the grayscale check reads the lightness of a pixel, and a transparent
 * pixel has none. Whatever was transparent is composited over the scene background first, which
 * is what a person sees behind the geometry anyway.
 */
bool write_fixture_render(
    const Domain::Image& image,
    const std::string& png_path,
    FixtureView view,
    const Theme& theme
);

} // namespace Slic3r::App
