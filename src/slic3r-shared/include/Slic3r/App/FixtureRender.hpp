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

namespace Platform {
struct CameraSynchData;
} // namespace Platform

namespace Scene {
class Camera;
class ISceneRenderCustomizer;
class Scene;
} // namespace Scene

/**
 * @brief Which view of the plater a fixture render shows (roadmap M6.2, PLAN G3).
 *
 * Prepare is the bed with the models on it, Preview is the Preview view of the print, with the
 * scene and the camera of that view (M6.2b). Both are rendered offscreen, so a render is a view
 * without the window around it.
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
 * @brief Which scene a view of a fixture render draws (roadmap M6.2b).
 *
 * Prepare draws the bed of the Prepare tab. Preview draws the Preview view itself: its own scene,
 * with its own camera, and not the plater scene the objects list thumbnails draw, which is what
 * made a change in that view invisible to the check.
 */
enum class FixtureViewScene
{
    Plater,
    Preview,
};

FixtureViewScene fixture_view_scene(FixtureView view);

/// The name of a scene, the one that goes into a sidecar.
std::string fixture_view_scene_to_string(FixtureViewScene scene);

/**
 * @brief Puts the camera of a 3D view into @p camera, so a render of the view looks at what the
 * view looks at.
 *
 * The very handover the views do between each other when a tab is switched (CameraHelper's
 * synchronize_camera), and not a camera of the render path's own: the distance to the target, the
 * angles around it and the zoom of the view are what a change in the view has to show up in.
 *
 * The viewport is left alone on purpose. The renderer gives the camera the viewport of the size it
 * renders, which is the one thing a render has to differ in, or two sizes of the same view are not
 * comparable.
 */
void set_fixture_view_camera(const Platform::CameraSynchData& data, Scene::Camera& camera);

/**
 * @brief What a fixture render needs from a 3D view (roadmap M6.2b).
 *
 * A view that can be rendered offscreen as itself: its scene, the customizer it draws that scene
 * through and its camera. The Prepare view is a project scene of the workbench and needs none of
 * this, which is why the Preview view, which has a scene of its own, is asked for it.
 */
class IFixtureViewSource
{
public:
    virtual ~IFixtureViewSource() = default;

    /// The scene the view draws, which is what a render of the view puts in the framebuffer.
    [[nodiscard]] virtual const Scene::Scene& fixture_render_scene() const = 0;

    /// The customizer the view draws that scene through, so a render draws the same passes.
    /// Not const, because the scene render drives the customizer while it draws.
    [[nodiscard]] virtual Scene::ISceneRenderCustomizer& fixture_render_customizer() = 0;

    /// The camera of the view, as the data the views hand over to each other on a tab switch.
    [[nodiscard]] virtual Platform::CameraSynchData fixture_render_camera() const = 0;

    /**
     * @brief Whether the view has the print of a slice in its scene, which is what it is for.
     *
     * The bed is in the scene from the moment the view is, so a bed on its own is a picture of
     * nothing: a render of a view that has no print yet would be a reference of an empty bed and
     * would quietly accept a slice that never made it into the view.
     */
    [[nodiscard]] virtual bool fixture_render_has_print() const = 0;
};

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
