#include <catch2/catch_test_macros.hpp>

#include <Slic3r/App/Theme.hpp>
#include <Slic3r/App/ThemeTypes.hpp>
#include <cmath>

using namespace Slic3r::App;

// ColorRGBA stores floats in [0, 1]; compare as rounded 8-bit channels.
static int u8(float v) { return static_cast<int>(std::lround(v * 255.0f)); }

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
            Platform::Color::NeutralGrey,
            Platform::Color::OnAccentPrimary,
            Platform::Color::Shadow,
            Platform::Color::PickerHandle,
            Platform::Color::Outline,
            Platform::Color::OnWarning,
            Platform::Color::MeasureFeature1,
            Platform::Color::MeasureFeature2,
            Platform::Color::IconTint,
            Platform::Color::CursorHighlight,
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
    REQUIRE(u8(text.r()) == 202);
    REQUIRE(u8(text.g()) == 210);
    REQUIRE(u8(text.b()) == 197);

    // Slate900 = #2F3E46 = (47, 62, 70)
    REQUIRE(u8(window_bg.r()) == 47);
    REQUIRE(u8(window_bg.g()) == 62);
    REQUIRE(u8(window_bg.b()) == 70);

    // Slate700 = #354F52 = (53, 79, 82)
    REQUIRE(u8(window_bg_alt.r()) == 53);
    REQUIRE(u8(window_bg_alt.g()) == 79);
    REQUIRE(u8(window_bg_alt.b()) == 82);

    // Sage300 = #84A98C = (132, 169, 140)
    REQUIRE(u8(accent_primary.r()) == 132);
    REQUIRE(u8(accent_primary.g()) == 169);
    REQUIRE(u8(accent_primary.b()) == 140);

    // Teal500 = #52796F = (82, 121, 111)
    REQUIRE(u8(accent_secondary.r()) == 82);
    REQUIRE(u8(accent_secondary.g()) == 121);
    REQUIRE(u8(accent_secondary.b()) == 111);

    // Button uses WindowBgAlternate as default
    REQUIRE(u8(button.r()) == 53);
    REQUIRE(u8(button.g()) == 79);
    REQUIRE(u8(button.b()) == 82);

    // SLA semantic colors
    REQUIRE(u8(sla_model_resin.r()) == 132); // Sage300 fallback
    REQUIRE(u8(sla_model_resin.g()) == 169);
    REQUIRE(u8(sla_model_resin.b()) == 140);

    REQUIRE(u8(sla_support.r()) == 202); // Sage100
    REQUIRE(u8(sla_support.g()) == 210);
    REQUIRE(u8(sla_support.b()) == 197);

    REQUIRE(u8(sla_pad.r()) == 82); // Teal500
    REQUIRE(u8(sla_pad.g()) == 121);
    REQUIRE(u8(sla_pad.b()) == 111);

    // Warning dark = #E3A857 = (227, 168, 87)
    REQUIRE(u8(warning.r()) == 227);
    REQUIRE(u8(warning.g()) == 168);
    REQUIRE(u8(warning.b()) == 87);

    // Error dark = #E07A6B = (224, 122, 107)
    REQUIRE(u8(error.r()) == 224);
    REQUIRE(u8(error.g()) == 122);
    REQUIRE(u8(error.b()) == 107);

    // New tokens (M1.5)
    const auto& neutral_grey = dark_theme.color(Platform::Color::NeutralGrey);
    const auto& on_accent_primary = dark_theme.color(Platform::Color::OnAccentPrimary);
    const auto& shadow = dark_theme.color(Platform::Color::Shadow);
    const auto& picker_handle = dark_theme.color(Platform::Color::PickerHandle);
    const auto& outline = dark_theme.color(Platform::Color::Outline);
    const auto& on_warning = dark_theme.color(Platform::Color::OnWarning);
    const auto& measure_feature1 = dark_theme.color(Platform::Color::MeasureFeature1);
    const auto& measure_feature2 = dark_theme.color(Platform::Color::MeasureFeature2);
    const auto& icon_tint = dark_theme.color(Platform::Color::IconTint);
    const auto& cursor_highlight = dark_theme.color(Platform::Color::CursorHighlight);

    // NeutralGrey = #808080 = (128, 128, 128)
    REQUIRE(u8(neutral_grey.r()) == 128);
    REQUIRE(u8(neutral_grey.g()) == 128);
    REQUIRE(u8(neutral_grey.b()) == 128);

    // OnAccentPrimary = White = (255, 255, 255)
    REQUIRE(u8(on_accent_primary.r()) == 255);
    REQUIRE(u8(on_accent_primary.g()) == 255);
    REQUIRE(u8(on_accent_primary.b()) == 255);

    // Shadow = Black 43% alpha = (0, 0, 0, 110)
    REQUIRE(u8(shadow.r()) == 0);
    REQUIRE(u8(shadow.g()) == 0);
    REQUIRE(u8(shadow.b()) == 0);
    REQUIRE(u8(shadow.a()) == 110);

    // PickerHandle = White = (255, 255, 255)
    REQUIRE(u8(picker_handle.r()) == 255);
    REQUIRE(u8(picker_handle.g()) == 255);
    REQUIRE(u8(picker_handle.b()) == 255);

    // Outline = Black 50% alpha = (0, 0, 0, 128)
    REQUIRE(u8(outline.r()) == 0);
    REQUIRE(u8(outline.g()) == 0);
    REQUIRE(u8(outline.b()) == 0);
    REQUIRE(u8(outline.a()) == 128);

    // OnWarning = White = (255, 255, 255)
    REQUIRE(u8(on_warning.r()) == 255);
    REQUIRE(u8(on_warning.g()) == 255);
    REQUIRE(u8(on_warning.b()) == 255);

    // MeasureFeature1 = #40BFBF = (64, 191, 191)
    REQUIRE(u8(measure_feature1.r()) == 64);
    REQUIRE(u8(measure_feature1.g()) == 191);
    REQUIRE(u8(measure_feature1.b()) == 191);

    // MeasureFeature2 = #BF40BF = (191, 64, 191)
    REQUIRE(u8(measure_feature2.r()) == 191);
    REQUIRE(u8(measure_feature2.g()) == 64);
    REQUIRE(u8(measure_feature2.b()) == 191);

    // IconTint = Black = (0, 0, 0)
    REQUIRE(u8(icon_tint.r()) == 0);
    REQUIRE(u8(icon_tint.g()) == 0);
    REQUIRE(u8(icon_tint.b()) == 0);

    // CursorHighlight = Yellow = (255, 255, 0, 255)
    REQUIRE(u8(cursor_highlight.r()) == 255);
    REQUIRE(u8(cursor_highlight.g()) == 255);
    REQUIRE(u8(cursor_highlight.b()) == 0);
    REQUIRE(u8(cursor_highlight.a()) == 255);
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
    REQUIRE(u8(text.r()) == 47);
    REQUIRE(u8(text.g()) == 62);
    REQUIRE(u8(text.b()) == 70);

    // WindowBg: Sage100 mixed 60% with white = #DFE4DC = (223, 228, 220)
    REQUIRE(u8(window_bg.r()) == 223);
    REQUIRE(u8(window_bg.g()) == 228);
    REQUIRE(u8(window_bg.b()) == 220);

    // WindowBgAlternate: Sage100 = #CAD2C5 = (202, 210, 197)
    REQUIRE(u8(window_bg_alt.r()) == 202);
    REQUIRE(u8(window_bg_alt.g()) == 210);
    REQUIRE(u8(window_bg_alt.b()) == 197);

    // AccentPrimary: Teal500 = #52796F = (82, 121, 111)
    REQUIRE(u8(accent_primary.r()) == 82);
    REQUIRE(u8(accent_primary.g()) == 121);
    REQUIRE(u8(accent_primary.b()) == 111);

    // AccentSecondary: Sage300 = #84A98C = (132, 169, 140)
    REQUIRE(u8(accent_secondary.r()) == 132);
    REQUIRE(u8(accent_secondary.g()) == 169);
    REQUIRE(u8(accent_secondary.b()) == 140);

    // Button uses WindowBgAlternate as default
    REQUIRE(u8(button.r()) == 202);
    REQUIRE(u8(button.g()) == 210);
    REQUIRE(u8(button.b()) == 197);

    // SLA semantic colors
    REQUIRE(u8(sla_model_resin.r()) == 132); // Sage300 fallback
    REQUIRE(u8(sla_model_resin.g()) == 169);
    REQUIRE(u8(sla_model_resin.b()) == 140);

    REQUIRE(u8(sla_support.r()) == 53); // Slate700
    REQUIRE(u8(sla_support.g()) == 79);
    REQUIRE(u8(sla_support.b()) == 82);

    REQUIRE(u8(sla_pad.r()) == 82); // Teal500
    REQUIRE(u8(sla_pad.g()) == 121);
    REQUIRE(u8(sla_pad.b()) == 111);

    // Warning light = #9A6A1F = (154, 106, 31)
    REQUIRE(u8(warning.r()) == 154);
    REQUIRE(u8(warning.g()) == 106);
    REQUIRE(u8(warning.b()) == 31);

    // Error light = #B5483E = (181, 72, 62)
    REQUIRE(u8(error.r()) == 181);
    REQUIRE(u8(error.g()) == 72);
    REQUIRE(u8(error.b()) == 62);

    // New tokens (M1.5) - same values in light theme
    const auto& neutral_grey = light_theme.color(Platform::Color::NeutralGrey);
    const auto& on_accent_primary = light_theme.color(Platform::Color::OnAccentPrimary);
    const auto& shadow = light_theme.color(Platform::Color::Shadow);
    const auto& picker_handle = light_theme.color(Platform::Color::PickerHandle);
    const auto& outline = light_theme.color(Platform::Color::Outline);
    const auto& on_warning = light_theme.color(Platform::Color::OnWarning);
    const auto& measure_feature1 = light_theme.color(Platform::Color::MeasureFeature1);
    const auto& measure_feature2 = light_theme.color(Platform::Color::MeasureFeature2);
    const auto& icon_tint = light_theme.color(Platform::Color::IconTint);
    const auto& cursor_highlight = light_theme.color(Platform::Color::CursorHighlight);

    // NeutralGrey = #808080 = (128, 128, 128)
    REQUIRE(u8(neutral_grey.r()) == 128);
    REQUIRE(u8(neutral_grey.g()) == 128);
    REQUIRE(u8(neutral_grey.b()) == 128);

    // OnAccentPrimary = White = (255, 255, 255)
    REQUIRE(u8(on_accent_primary.r()) == 255);
    REQUIRE(u8(on_accent_primary.g()) == 255);
    REQUIRE(u8(on_accent_primary.b()) == 255);

    // Shadow = Black 43% alpha = (0, 0, 0, 110)
    REQUIRE(u8(shadow.r()) == 0);
    REQUIRE(u8(shadow.g()) == 0);
    REQUIRE(u8(shadow.b()) == 0);
    REQUIRE(u8(shadow.a()) == 110);

    // PickerHandle = White = (255, 255, 255)
    REQUIRE(u8(picker_handle.r()) == 255);
    REQUIRE(u8(picker_handle.g()) == 255);
    REQUIRE(u8(picker_handle.b()) == 255);

    // Outline = Black 50% alpha = (0, 0, 0, 128)
    REQUIRE(u8(outline.r()) == 0);
    REQUIRE(u8(outline.g()) == 0);
    REQUIRE(u8(outline.b()) == 0);
    REQUIRE(u8(outline.a()) == 128);

    // OnWarning = White = (255, 255, 255)
    REQUIRE(u8(on_warning.r()) == 255);
    REQUIRE(u8(on_warning.g()) == 255);
    REQUIRE(u8(on_warning.b()) == 255);

    // MeasureFeature1 = #40BFBF = (64, 191, 191)
    REQUIRE(u8(measure_feature1.r()) == 64);
    REQUIRE(u8(measure_feature1.g()) == 191);
    REQUIRE(u8(measure_feature1.b()) == 191);

    // MeasureFeature2 = #BF40BF = (191, 64, 191)
    REQUIRE(u8(measure_feature2.r()) == 191);
    REQUIRE(u8(measure_feature2.g()) == 64);
    REQUIRE(u8(measure_feature2.b()) == 191);

    // IconTint = Black = (0, 0, 0)
    REQUIRE(u8(icon_tint.r()) == 0);
    REQUIRE(u8(icon_tint.g()) == 0);
    REQUIRE(u8(icon_tint.b()) == 0);

    // CursorHighlight = Yellow = (255, 255, 0, 255)
    REQUIRE(u8(cursor_highlight.r()) == 255);
    REQUIRE(u8(cursor_highlight.g()) == 255);
    REQUIRE(u8(cursor_highlight.b()) == 0);
    REQUIRE(u8(cursor_highlight.a()) == 255);
}