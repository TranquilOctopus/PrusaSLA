#include <catch2/catch_test_macros.hpp>

#include <Slic3r/App/Theme.hpp>
#include <Slic3r/App/ThemeTypes.hpp>

using namespace Slic3r::App;

TEST_CASE("[Theme] Every Platform::Color has an entry in both themes")
{
    // Test dark theme
    Theme dark_theme(Theme::Style::Dark);
    // Test light theme
    Theme light_theme(Theme::Style::Light);

    // Iterate through all Platform::Color enum values and verify they have entries
    // We use the fact that color() throws if the key is missing
    const auto verify_theme = [](const Theme& theme, const char* theme_name) {
        // List all Platform::Color enum values
        constexpr Platform::Color all_colors[] = {
            Platform::Color::Text,
            Platform::Color::TextLink,
            Platform::Color::WindowBg,
            Platform::Color::WindowBgAlternate,
            Platform::Color::Control,
            Platform::Color::AccentPrimary,
            Platform::Color::AccentSecondary,
            Platform::Color::AccentTertiary,
            Platform::Color::Button,
            Platform::Color::ButtonTransparent,
            Platform::Color::RadioButtonBackground,
            Platform::Color::RadioButton,
            Platform::Color::Scrollbar,
            Platform::Color::NavCursor,
            Platform::Color::ModalWindowDimBg,
            Platform::Color::Warning,
            Platform::Color::Error,
            Platform::Color::SceneBgTop,
            Platform::Color::SceneBgBottom,
            Platform::Color::SceneBgErrorTop,
            Platform::Color::SceneBgErrorBottom,
            Platform::Color::SlaModelResin,
            Platform::Color::SlaSupport,
            Platform::Color::SlaPad,
            Platform::Color::SlaSupportPointAuto,
            Platform::Color::SlaSupportPointManual,
            Platform::Color::SlaIslandWarning,
            Platform::Color::SlaDrainHole,
            Platform::Color::SlaHollowInterior,
            Platform::Color::SlaCupWarning,
            Platform::Color::SlaLayerArea,
            Platform::Color::Transparent,
        };

        for (Platform::Color color : all_colors) {
            INFO("Theme: " << theme_name << ", Color: " << static_cast<int>(color));
            REQUIRE_NOTHROW(theme.color(color));
            REQUIRE_NOTHROW(theme.color_imgui(color));
        }
    };

    verify_theme(dark_theme, "Dark");
    verify_theme(light_theme, "Light");
}

TEST_CASE("[Theme] Dark theme uses SLA palette colors")
{
    Theme dark_theme(Theme::Style::Dark);

    // Verify key palette mappings per PLAN 2.1
    const auto& text = dark_theme.color(Platform::Color::Text);
    const auto& window_bg = dark_theme.color(Platform::Color::WindowBg);
    const auto& window_bg_alt = dark_theme.color(Platform::Color::WindowBgAlternate);
    const auto& accent_primary = dark_theme.color(Platform::Color::AccentPrimary);
    const auto& accent_secondary = dark_theme.color(Platform::Color::AccentSecondary);
    const auto& button = dark_theme.color(Platform::Color::Button);
    const auto& sla_model_resin = dark_theme.color(Platform::Color::SlaModelResin);
    const auto& sla_support = dark_theme.color(Platform::Color::SlaSupport);
    const auto& sla_pad = dark_theme.color(Platform::Color::SlaPad);
    const auto& warning = dark_theme.color(Platform::Color::Warning);
    const auto& error = dark_theme.color(Platform::Color::Error);

    // Sage100 = #CAD2C5 = (202, 210, 197)
    REQUIRE(text.r == 202);
    REQUIRE(text.g == 210);
    REQUIRE(text.b == 197);

    // Slate900 = #2F3E46 = (47, 62, 70)
    REQUIRE(window_bg.r == 47);
    REQUIRE(window_bg.g == 62);
    REQUIRE(window_bg.b == 70);

    // Slate700 = #354F52 = (53, 79, 82)
    REQUIRE(window_bg_alt.r == 53);
    REQUIRE(window_bg_alt.g == 79);
    REQUIRE(window_bg_alt.b == 82);

    // Sage300 = #84A98C = (132, 169, 140)
    REQUIRE(accent_primary.r == 132);
    REQUIRE(accent_primary.g == 169);
    REQUIRE(accent_primary.b == 140);

    // Teal500 = #52796F = (82, 121, 111)
    REQUIRE(accent_secondary.r == 82);
    REQUIRE(accent_secondary.g == 121);
    REQUIRE(accent_secondary.b == 111);

    // Button uses WindowBgAlternate as default
    REQUIRE(button.r == 53);
    REQUIRE(button.g == 79);
    REQUIRE(button.b == 82);

    // SLA semantic colors
    REQUIRE(sla_model_resin.r == 132); // Sage300 fallback
    REQUIRE(sla_model_resin.g == 169);
    REQUIRE(sla_model_resin.b == 140);

    REQUIRE(sla_support.r == 202); // Sage100
    REQUIRE(sla_support.g == 210);
    REQUIRE(sla_support.b == 197);

    REQUIRE(sla_pad.r == 82); // Teal500
    REQUIRE(sla_pad.g == 121);
    REQUIRE(sla_pad.b == 111);

    // Warning dark = #E3A857 = (227, 168, 87)
    REQUIRE(warning.r == 227);
    REQUIRE(warning.g == 168);
    REQUIRE(warning.b == 87);

    // Error dark = #E07A6B = (224, 122, 107)
    REQUIRE(error.r == 224);
    REQUIRE(error.g == 122);
    REQUIRE(error.b == 107);
}

TEST_CASE("[Theme] Light theme uses SLA palette colors")
{
    Theme light_theme(Theme::Style::Light);

    // Verify key palette mappings per PLAN 2.1
    const auto& text = light_theme.color(Platform::Color::Text);
    const auto& window_bg = light_theme.color(Platform::Color::WindowBg);
    const auto& window_bg_alt = light_theme.color(Platform::Color::WindowBgAlternate);
    const auto& accent_primary = light_theme.color(Platform::Color::AccentPrimary);
    const auto& accent_secondary = light_theme.color(Platform::Color::AccentSecondary);
    const auto& button = light_theme.color(Platform::Color::Button);
    const auto& sla_model_resin = light_theme.color(Platform::Color::SlaModelResin);
    const auto& sla_support = light_theme.color(Platform::Color::SlaSupport);
    const auto& sla_pad = light_theme.color(Platform::Color::SlaPad);
    const auto& warning = light_theme.color(Platform::Color::Warning);
    const auto& error = light_theme.color(Platform::Color::Error);

    // Slate900 = #2F3E46 = (47, 62, 70)
    REQUIRE(text.r == 47);
    REQUIRE(text.g == 62);
    REQUIRE(text.b == 70);

    // WindowBg: Sage100 mixed 60% with white = #DFE4DC = (223, 228, 220)
    REQUIRE(window_bg.r == 223);
    REQUIRE(window_bg.g == 228);
    REQUIRE(window_bg.b == 220);

    // WindowBgAlternate: Sage100 = #CAD2C5 = (202, 210, 197)
    REQUIRE(window_bg_alt.r == 202);
    REQUIRE(window_bg_alt.g == 210);
    REQUIRE(window_bg_alt.b == 197);

    // AccentPrimary: Teal500 = #52796F = (82, 121, 111)
    REQUIRE(accent_primary.r == 82);
    REQUIRE(accent_primary.g == 121);
    REQUIRE(accent_primary.b == 111);

    // AccentSecondary: Sage300 = #84A98C = (132, 169, 140)
    REQUIRE(accent_secondary.r == 132);
    REQUIRE(accent_secondary.g == 169);
    REQUIRE(accent_secondary.b == 140);

    // Button uses WindowBgAlternate as default
    REQUIRE(button.r == 202);
    REQUIRE(button.g == 210);
    REQUIRE(button.b == 197);

    // SLA semantic colors
    REQUIRE(sla_model_resin.r == 132); // Sage300 fallback
    REQUIRE(sla_model_resin.g == 169);
    REQUIRE(sla_model_resin.b == 140);

    REQUIRE(sla_support.r == 53); // Slate700
    REQUIRE(sla_support.g == 79);
    REQUIRE(sla_support.b == 82);

    REQUIRE(sla_pad.r == 82); // Teal500
    REQUIRE(sla_pad.g == 121);
    REQUIRE(sla_pad.b == 111);

    // Warning light = #9A6A1F = (154, 106, 31)
    REQUIRE(warning.r == 154);
    REQUIRE(warning.g == 106);
    REQUIRE(warning.b == 31);

    // Error light = #B5483E = (181, 72, 62)
    REQUIRE(error.r == 181);
    REQUIRE(error.g == 72);
    REQUIRE(error.b == 62);
}