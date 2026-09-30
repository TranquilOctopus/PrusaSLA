#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <nlohmann/json.hpp>

#include <Slic3r/App/FixtureRender.hpp>
#include <Slic3r/App/Theme.hpp>
#include <Slic3r/App/ThemeTypes.hpp>
#include <Slic3r/Domain/Color.hpp>
#include <Slic3r/Domain/Image.hpp>
#include <Slic3r/Domain/PixelFormat.hpp>

#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace Slic3r::App;
using Catch::Approx;

namespace {

// The roles of the grayscale check, and the threshold doc/sla-fork/tools/visual_diff.py applies.
constexpr double lightness_threshold = 10.0;
const char* const object_roles[]     = {"model", "supports", "pad"};
const char* const background_roles[] = {"background_top", "background_bottom"};

std::map<std::string, const RoleLightness*> by_role(const std::vector<RoleLightness>& roles)
{
    std::map<std::string, const RoleLightness*> ret;
    for (const RoleLightness& role : roles)
        ret[role.role] = &role;
    return ret;
}

std::optional<Platform::Color> color_of_token(const std::string& token)
{
    if (token == "SlaModelResin")
        return Platform::Color::SlaModelResin;
    if (token == "SlaSupport")
        return Platform::Color::SlaSupport;
    if (token == "SlaPad")
        return Platform::Color::SlaPad;
    if (token == "SceneBgTop")
        return Platform::Color::SceneBgTop;
    if (token == "SceneBgBottom")
        return Platform::Color::SceneBgBottom;
    return std::nullopt;
}

Domain::Image rgba_image(int width, int height, const std::vector<uint8_t>& pixels)
{
    return Domain::Image(Domain::PixelFormat::RGBA8, width, height, std::vector<uint8_t>(pixels));
}

} // namespace

TEST_CASE("[FixtureRender] A view name is parsed, and only a known one is")
{
    REQUIRE(fixture_view_from_string("prepare") == FixtureView::Prepare);
    REQUIRE(fixture_view_from_string("preview") == FixtureView::Preview);

    CHECK_FALSE(fixture_view_from_string("").has_value());
    CHECK_FALSE(fixture_view_from_string("Prepare").has_value());
    CHECK_FALSE(fixture_view_from_string("gcode").has_value());
    CHECK_FALSE(fixture_view_from_string("prepare preview").has_value());

    CHECK(fixture_view_to_string(FixtureView::Prepare) == "prepare");
    CHECK(fixture_view_to_string(FixtureView::Preview) == "preview");
}

TEST_CASE("[FixtureRender] A render size is parsed, and only a usable one is")
{
    const std::optional<Domain::Size> size = parse_render_size("1280x960");
    REQUIRE(size.has_value());
    CHECK(size->width == 1280);
    CHECK(size->height == 960);

    REQUIRE(parse_render_size("64x64").has_value());

    // Not a size, a side out of range, a side that is not a number, or a size with a tail.
    CHECK_FALSE(parse_render_size("").has_value());
    CHECK_FALSE(parse_render_size("1280").has_value());
    CHECK_FALSE(parse_render_size("1280x").has_value());
    CHECK_FALSE(parse_render_size("0x960").has_value());
    CHECK_FALSE(parse_render_size("1280x0").has_value());
    CHECK_FALSE(parse_render_size("16384x960").has_value());
    CHECK_FALSE(parse_render_size("1280x16384").has_value());
    CHECK_FALSE(parse_render_size("1280X960").has_value());
    CHECK_FALSE(parse_render_size("1280x960x2").has_value());
    CHECK_FALSE(parse_render_size("12a0x960").has_value());
    CHECK_FALSE(parse_render_size("-1280x960").has_value());
    CHECK_FALSE(parse_render_size("1280x960 ").has_value());
}

TEST_CASE("[FixtureRender] L* of a colour is the CIE one")
{
    CHECK(cie_lightness(Domain::ColorRGBA::BLACK()) == Approx(0.0).margin(0.01));
    CHECK(cie_lightness(Domain::ColorRGBA::WHITE()) == Approx(100.0).margin(0.01));

    // The palette of PLAN 2.1, so a mistake in the formula is a test failure and not a number
    // that quietly drifts. These are the same values ThemeTests pins; a test has to name them.
    struct Palette
    {
        const char* token;
        Domain::ColorRGBA color;
        double l;
    };
    const Palette palette[]{
        {"Sage100", Domain::ColorRGBA(202, 210, 197, 255), 83.27},
        {"Sage300", Domain::ColorRGBA(132, 169, 140, 255), 65.88},
        {"Teal500", Domain::ColorRGBA(82, 121, 111, 255), 47.77},
        {"Slate700", Domain::ColorRGBA(53, 79, 82, 255), 31.71},
        {"Slate900", Domain::ColorRGBA(47, 62, 70, 255), 25.23},
    };
    for (const Palette& entry : palette) {
        INFO("Palette token: " << entry.token);
        CHECK(cie_lightness(entry.color) == Approx(entry.l).margin(0.01));
    }

    // The Rec. 709 weights, not a plain average of the channels: green reads lighter than red
    // and red lighter than blue.
    CHECK(cie_lightness(Domain::ColorRGBA(0, 255, 0, 255)) == Approx(87.74).margin(0.05));
    CHECK(cie_lightness(Domain::ColorRGBA(255, 0, 0, 255)) == Approx(53.24).margin(0.05));
    CHECK(cie_lightness(Domain::ColorRGBA(0, 0, 255, 255)) == Approx(32.30).margin(0.05));

    // The alpha of a colour says nothing about its lightness.
    CHECK(cie_lightness(Domain::ColorRGBA(0, 0, 0, 0)) == Approx(0.0).margin(0.01));
}

TEST_CASE("[FixtureRender] The lightness of the roles comes from the tokens, in both themes")
{
    // PLAN 2.1: the model, the supports and the pad have to be told apart by lightness and not by
    // hue, in both themes. This is the check doc/sla-fork/tools/visual_diff.py runs on the sidecar
    // of a render, run here on the theme so a palette change cannot pass unnoticed.
    const auto check_theme = [](const Theme& theme, const char* theme_name)
    {
        const std::vector<RoleLightness> all                    = scene_lightness(theme);
        const std::map<std::string, const RoleLightness*> roles = by_role(all);
        REQUIRE(roles.size() == 5);

        for (const char* role : object_roles) {
            INFO("Theme: " << theme_name << ", role: " << role);
            REQUIRE(roles.contains(role));
            // Every role names the token it was resolved from, so a render says which token it
            // drew with and a hex value never has to be written outside Theme.cpp.
            const RoleLightness& entry = *roles.at(role);
            CHECK_FALSE(entry.token.empty());
            const std::optional<Platform::Color> color = color_of_token(entry.token);
            REQUIRE(color.has_value());
            CHECK(entry.color == theme.color(*color));
            CHECK(entry.l == Approx(cie_lightness(entry.color)));
        }
        for (const char* role : background_roles) {
            INFO("Theme: " << theme_name << ", role: " << role);
            REQUIRE(roles.contains(role));
            const std::optional<Platform::Color> color = color_of_token(roles.at(role)->token);
            REQUIRE(color.has_value());
            CHECK(roles.at(role)->color == theme.color(*color));
        }

        // The pairs the check compares: every object role against every other one, and against
        // both ends of the background gradient. The two ends are not compared with each other,
        // because PLAN 2.1 allows that difference for surface layering only.
        for (const char* first : object_roles) {
            for (const char* second : object_roles) {
                if (std::string(first) == second)
                    continue;
                INFO("Theme: " << theme_name << ", roles: " << first << " and " << second);
                CHECK(std::abs(roles.at(first)->l - roles.at(second)->l) >= lightness_threshold);
            }
            for (const char* background : background_roles) {
                INFO("Theme: " << theme_name << ", roles: " << first << " and " << background);
                CHECK(
                    std::abs(roles.at(first)->l - roles.at(background)->l) >= lightness_threshold
                );
            }
        }
    };

    check_theme(Theme(Theme::Style::Dark), "Dark");
    check_theme(Theme(Theme::Style::Light), "Light");
}

TEST_CASE("[FixtureRender] A render is composited over the scene background")
{
    const Domain::ColorRGBA background{0.0f, 0.0f, 1.0f, 1.0f}; // a blue no theme uses

    // Opaque pixels are kept as they are.
    const std::vector<uint8_t> pixels{10, 20, 30, 255, 40, 50, 60, 255};
    const std::vector<uint8_t> opaque = composite_over(rgba_image(2, 1, pixels), background);
    REQUIRE(opaque.size() == 6);
    CHECK(opaque[0] == 10);
    CHECK(opaque[1] == 20);
    CHECK(opaque[2] == 30);
    CHECK(opaque[3] == 40);
    CHECK(opaque[4] == 50);
    CHECK(opaque[5] == 60);

    // A fully transparent pixel is the background.
    const std::vector<uint8_t> clear_pixels{10, 20, 30, 0, 40, 50, 60, 0};
    const std::vector<uint8_t> clear = composite_over(rgba_image(2, 1, clear_pixels), background);
    REQUIRE(clear.size() == 6);
    CHECK(clear[0] == 0);
    CHECK(clear[1] == 0);
    CHECK(clear[2] == 255);

    // Halfway is halfway, per channel: black at 128 of 255 over a blue background keeps half the
    // blue (127), and white at the same alpha is half white and half blue.
    const std::vector<uint8_t> half_pixels{0, 0, 0, 128, 255, 255, 255, 128};
    const std::vector<uint8_t> half = composite_over(rgba_image(2, 1, half_pixels), background);
    REQUIRE(half.size() == 6);
    CHECK(half[0] == 0);
    CHECK(half[1] == 0);
    CHECK(half[2] == 127);
    CHECK(half[3] == 128);
    CHECK(half[4] == 128);
    CHECK(half[5] == 255);
}

TEST_CASE("[FixtureRender] The sidecar names the view, the size and every role")
{
    const Theme theme(Theme::Style::Dark);
    const Domain::Image image = rgba_image(3, 2, std::vector<uint8_t>(3 * 2 * 4, 0));
    const std::string sidecar = render_sidecar(FixtureView::Preview, image, theme);

    CHECK(sidecar.find("\"view\": \"preview\"") != std::string::npos);
    CHECK(sidecar.find("\"width\": 3") != std::string::npos);
    CHECK(sidecar.find("\"height\": 2") != std::string::npos);
    CHECK(sidecar.find("\"composited_background_token\": \"SceneBgBottom\"") != std::string::npos);
    for (const char* role : object_roles)
        CHECK(sidecar.find("\"" + std::string(role) + "\"") != std::string::npos);
    for (const char* role : background_roles)
        CHECK(sidecar.find("\"" + std::string(role) + "\"") != std::string::npos);
    // The keys visual_diff.py reads.
    CHECK(sidecar.find("\"token\"") != std::string::npos);
    CHECK(sidecar.find("\"rgb\"") != std::string::npos);
    CHECK(sidecar.find("\"L\"") != std::string::npos);

    // A rendered sidecar is valid JSON with the numbers in it, not a hand written approximation.
    const nlohmann::json parsed = nlohmann::json::parse(sidecar);
    REQUIRE(parsed.contains("lightness"));
    REQUIRE(parsed["lightness"].contains("model"));
    CHECK(parsed["lightness"]["model"]["L"].get<double>() == Approx(65.88).margin(0.01));
    CHECK(parsed["lightness"]["supports"]["token"].get<std::string>() == "SlaSupport");
    CHECK(parsed["lightness"]["background_bottom"]["rgb"].size() == 3);
}
