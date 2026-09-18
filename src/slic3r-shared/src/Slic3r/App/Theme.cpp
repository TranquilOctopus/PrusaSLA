#include "Slic3r/App/Theme.hpp"

#include "Slic3r/App/Scene/Scene.hpp"
#include "Slic3r/App/Imgui/ImguiExtension.hpp"
#include "Slic3r/App/Yoga/Icon.hpp"

#include <Slic3r/Assert.hpp>

#include <imgui_internal.h>

namespace Slic3r::App {

// Palette table (PLAN 2.1): the five base colors plus warning amber and error red.
// These are the only hard-coded hex values allowed outside this table.
// All other colors must be derived from these tokens.
static constexpr ImVec4 k_palette_sage100  = ImColor(0xCA, 0xD2, 0xC5); // #CAD2C5
static constexpr ImVec4 k_palette_sage300  = ImColor(0x84, 0xA9, 0x8C); // #84A98C
static constexpr ImVec4 k_palette_teal500  = ImColor(0x52, 0x79, 0x6F); // #52796F
static constexpr ImVec4 k_palette_slate700 = ImColor(0x35, 0x4F, 0x52); // #354F52
static constexpr ImVec4 k_palette_slate900 = ImColor(0x2F, 0x3E, 0x46); // #2F3E46

// Warning / Error colors (PLAN 2.1 rule 3)
static constexpr ImVec4 k_warning_dark  = ImColor(0xE3, 0xA8, 0x57); // #E3A857
static constexpr ImVec4 k_error_dark    = ImColor(0xE0, 0x7A, 0x6B); // #E07A6B
static constexpr ImVec4 k_warning_light = ImColor(0x9A, 0x6A, 0x1F); // #9A6A1F
static constexpr ImVec4 k_error_light   = ImColor(0xB5, 0x48, 0x3E); // #B5483E

// Prusa brand orange - used as accent in light mode (legacy, to be removed)
static constexpr ImVec4 k_prusa_orange{0.874f, 0.365f, 0.176f, 1.00f};

Theme::Theme(Style style)
{
    set_style(style);
}

void Theme::set_style(Style style)
{
    m_style = style;
    m_colors.clear();
    switch (style) {
    case Style::Dark:
        initialize_dark_colors();
        break;
    case Style::Light:
        initialize_light_colors();
        break;
    }

    // Todo: This should be moved, once we will support changing theme on the fly
    Scene::Scene::set_scene_colors(
        Scene::SceneColors{
            color(Platform::Color::SceneBgTop),
            color(Platform::Color::SceneBgBottom),
            color(Platform::Color::SceneBgErrorTop),
            color(Platform::Color::SceneBgErrorBottom)
        }
    );

    if (m_style == Style::Light) {
        Yoga::Icon::set_replace_strings(
            {{"#FFFFFF", "#0B0B0B"},
             {"#ffffff", "#0B0B0B"},
             {"white", "#0B0B0B"},
             {"#F7F7F7", "#0B0B0B"},
             {"#f7f7f7", "#0B0B0B"}}
        );
    }
}

void Theme::initialize_dark_colors()
{
    m_colors[Platform::Color::Transparent] = ColorEntry{0, 0, 0, 0};

    m_colors[Platform::Color::Text] = ColorEntry{
        k_palette_sage100,
        std::make_unique<ImColor>(k_palette_sage100.Value.x * 0.5f, k_palette_sage100.Value.y * 0.5f, k_palette_sage100.Value.z * 0.5f, 1.00f) // disabled
    };
    m_colors[Platform::Color::TextLink] = ColorEntry{
        k_palette_sage300,
        std::make_unique<ImColor>(k_palette_sage300.Value.x * 0.5f, k_palette_sage300.Value.y * 0.5f, k_palette_sage300.Value.z * 0.5f, 1.00f) // disabled
    };
    m_colors[Platform::Color::WindowBg]          = auto_entry(k_palette_slate900);
    m_colors[Platform::Color::WindowBgAlternate] = auto_entry(k_palette_slate700);

    m_colors[Platform::Color::Scrollbar] = ColorEntry{
        ImColor(0.31f, 0.31f, 0.31f, 1.00f),
        nullptr,
        std::make_unique<ImColor>(0.41f, 0.41f, 0.41f, 1.00f),
        std::make_unique<ImColor>(0.51f, 0.51f, 0.51f, 1.00f)
    };

    m_colors[Platform::Color::NavCursor] = ColorEntry{k_palette_teal500};

    m_colors[Platform::Color::Button] = ColorEntry{
        m_colors[Platform::Color::WindowBgAlternate].color_default,
        std::make_unique<ImColor>(
            Theme::color_imgui(Platform::Color::WindowBgAlternate, Platform::ColorGroup::Disabled)
        ),
        std::make_unique<ImColor>(
            Theme::color_imgui(Platform::Color::WindowBgAlternate, Platform::ColorGroup::Hovered)
        ),
        std::make_unique<ImColor>(k_palette_sage300),
        std::make_unique<ImColor>(Theme::color_imgui(
            Platform::Color::WindowBgAlternate,
            Platform::ColorGroup::ActiveDisabled
        ))
    };

    m_colors[Platform::Color::RadioButtonBackground] = ColorEntry{
        ImColor(127, 127, 127),
        std::make_unique<ImColor>(Imgui::adjust_brightness(ImColor(127, 127, 127), 1.5f)),
        std::make_unique<ImColor>(Imgui::adjust_brightness(ImColor(127, 127, 127), 1.2f)),
        std::make_unique<ImColor>(217, 217, 217),
        std::make_unique<ImColor>(Imgui::adjust_brightness(ImColor(127, 127, 127), 1.7f))
    };

    m_colors[Platform::Color::RadioButton] = ColorEntry{
        Theme::color_imgui(Platform::Color::WindowBgAlternate),
        std::make_unique<ImColor>(
            Imgui::adjust_brightness(Theme::color_imgui(Platform::Color::WindowBgAlternate), 1.5f)
        ),
        std::make_unique<ImColor>(
            Theme::color_imgui(Platform::Color::WindowBgAlternate, Platform::ColorGroup::Hovered)
        ),
        std::make_unique<ImColor>(k_palette_teal500),
        std::make_unique<ImColor>(
            Imgui::adjust_brightness(Theme::color_imgui(Platform::Color::WindowBgAlternate), 1.9f)
        )
    };

    m_colors[Platform::Color::ButtonTransparent] = ColorEntry{
        Theme::color_imgui(Platform::Color::Transparent),
        std::make_unique<ImColor>(
            Theme::color_imgui(Platform::Color::WindowBgAlternate, Platform::ColorGroup::Disabled)
        ),
        std::make_unique<ImColor>(
            Theme::color_imgui(Platform::Color::WindowBgAlternate, Platform::ColorGroup::Hovered)
        ),
        std::make_unique<ImColor>(k_palette_sage300),
        std::make_unique<ImColor>(Theme::color_imgui(
            Platform::Color::WindowBgAlternate,
            Platform::ColorGroup::ActiveDisabled
        ))
    };

    m_colors[Platform::Color::AccentPrimary] = auto_entry(k_palette_sage300);
    m_colors[Platform::Color::AccentPrimary].color_disabled =
        std::make_unique<ImColor>(k_palette_sage300.Value.x * 0.7f, k_palette_sage300.Value.y * 0.7f, k_palette_sage300.Value.z * 0.7f, 1.0f);
    m_colors[Platform::Color::AccentSecondary] = auto_entry(k_palette_teal500);
    m_colors[Platform::Color::AccentTertiary]  = ColorEntry{ImColor(175, 119, 255)};
    m_colors[Platform::Color::Error]           = ColorEntry{k_error_dark};
    m_colors[Platform::Color::Warning]         = ColorEntry{k_warning_dark};
    m_colors[Platform::Color::ModalWindowDimBg] = ColorEntry{ImColor(0.80f, 0.80f, 0.80f, 0.35f)};

    m_colors[Platform::Color::SceneBgTop]         = ColorEntry{k_palette_slate700};
    m_colors[Platform::Color::SceneBgBottom]      = ColorEntry{k_palette_slate900};
    m_colors[Platform::Color::SceneBgErrorTop]    = ColorEntry{ImColor(1.00f, 0.20f, 0.20f, 1.0f)};
    m_colors[Platform::Color::SceneBgErrorBottom] = ColorEntry{ImColor(0.60f, 0.20f, 0.20f, 1.0f)};

    // SLA semantic colors (PLAN 2.1 / A8)
    m_colors[Platform::Color::SlaModelResin]      = ColorEntry{k_palette_sage300}; // fallback; overridden by material_colour at runtime
    m_colors[Platform::Color::SlaSupport]         = ColorEntry{k_palette_sage100};
    m_colors[Platform::Color::SlaPad]             = ColorEntry{k_palette_teal500};
    m_colors[Platform::Color::SlaSupportPointAuto] = ColorEntry{k_palette_sage300};
    m_colors[Platform::Color::SlaSupportPointManual] = ColorEntry{
        k_palette_sage100,
        nullptr,
        nullptr,
        std::make_unique<ImColor>(k_palette_slate900), // outline for active
        nullptr
    };
    m_colors[Platform::Color::SlaIslandWarning]   = ColorEntry{k_warning_dark};
    m_colors[Platform::Color::SlaDrainHole]       = ColorEntry{k_palette_sage100};
    m_colors[Platform::Color::SlaHollowInterior]  = ColorEntry{k_palette_slate700};
    m_colors[Platform::Color::SlaCupWarning]      = ColorEntry{k_warning_dark};
    // SlaLayerArea: line and fill handled at render time; token maps to line color
    m_colors[Platform::Color::SlaLayerArea]       = ColorEntry{k_palette_sage300};
}

void Theme::initialize_light_colors()
{
    m_colors[Platform::Color::Transparent] = ColorEntry{0, 0, 0, 0};

    m_colors[Platform::Color::Text] = ColorEntry{
        k_palette_slate900,
        std::make_unique<ImColor>(k_palette_slate900.Value.x * 0.5f, k_palette_slate900.Value.y * 0.5f, k_palette_slate900.Value.z * 0.5f, 1.00f) // disabled
    };
    m_colors[Platform::Color::TextLink] = ColorEntry{
        k_palette_slate700,
        std::make_unique<ImColor>(k_palette_slate700.Value.x * 0.5f, k_palette_slate700.Value.y * 0.5f, k_palette_slate700.Value.z * 0.5f, 1.00f) // disabled
    };
    // WindowBg: Sage100 mixed 60% with white = #DFE4DC
    m_colors[Platform::Color::WindowBg]          = auto_entry_light(ImColor(0xDF, 0xE4, 0xDC));
    m_colors[Platform::Color::WindowBgAlternate] = auto_entry_light(k_palette_sage100);

    // Scrollbar: visible dark greys on the light background; hover/active are darker
    m_colors[Platform::Color::Scrollbar] = ColorEntry{
        ImColor(0.58f, 0.58f, 0.58f, 1.00f),
        nullptr,
        std::make_unique<ImColor>(0.45f, 0.45f, 0.45f, 1.00f), // hovered - darker
        std::make_unique<ImColor>(0.33f, 0.33f, 0.33f, 1.00f)  // active - darkest
    };

    m_colors[Platform::Color::NavCursor] = ColorEntry{k_palette_teal500};

    m_colors[Platform::Color::Button] = ColorEntry{
        m_colors[Platform::Color::WindowBgAlternate].color_default,
        std::make_unique<ImColor>(
            Theme::color_imgui(Platform::Color::WindowBgAlternate, Platform::ColorGroup::Disabled)
        ),
        std::make_unique<ImColor>(
            Theme::color_imgui(Platform::Color::WindowBgAlternate, Platform::ColorGroup::Hovered)
        ),
        std::make_unique<ImColor>(k_palette_slate700),
        std::make_unique<ImColor>(Theme::color_imgui(
            Platform::Color::WindowBgAlternate,
            Platform::ColorGroup::ActiveDisabled
        ))
    };

    m_colors[Platform::Color::ButtonTransparent] = ColorEntry{
        Theme::color_imgui(Platform::Color::Transparent),
        std::make_unique<ImColor>(
            Theme::color_imgui(Platform::Color::WindowBgAlternate, Platform::ColorGroup::Disabled)
        ),
        std::make_unique<ImColor>(
            Theme::color_imgui(Platform::Color::WindowBgAlternate, Platform::ColorGroup::Hovered)
        ),
        std::make_unique<ImColor>(k_palette_slate700),
        std::make_unique<ImColor>(Theme::color_imgui(
            Platform::Color::WindowBgAlternate,
            Platform::ColorGroup::ActiveDisabled
        ))
    };

    m_colors[Platform::Color::RadioButtonBackground] = ColorEntry{
        ImColor(127, 127, 127),
        std::make_unique<ImColor>(Imgui::adjust_brightness(ImColor(127, 127, 127), 1.5f)),
        std::make_unique<ImColor>(Imgui::adjust_brightness(ImColor(127, 127, 127), 1.2f)),
        std::make_unique<ImColor>(217, 217, 217),
        std::make_unique<ImColor>(Imgui::adjust_brightness(ImColor(127, 127, 127), 1.7f))
    };

    m_colors[Platform::Color::RadioButton] = ColorEntry{
        Theme::color_imgui(Platform::Color::WindowBgAlternate),
        std::make_unique<ImColor>(
            Imgui::adjust_brightness(Theme::color_imgui(Platform::Color::WindowBgAlternate), 1.5f)
        ),
        std::make_unique<ImColor>(
            Theme::color_imgui(Platform::Color::WindowBgAlternate, Platform::ColorGroup::Hovered)
        ),
        std::make_unique<ImColor>(k_palette_teal500),
        std::make_unique<ImColor>(
            Imgui::adjust_brightness(Theme::color_imgui(Platform::Color::WindowBgAlternate), 1.9f)
        )
    };

    m_colors[Platform::Color::AccentPrimary]   = auto_entry_light(k_palette_teal500);
    m_colors[Platform::Color::AccentSecondary] = auto_entry_light(k_palette_sage300);
    m_colors[Platform::Color::AccentTertiary]  = ColorEntry{ImColor(175, 119, 255)};
    m_colors[Platform::Color::Error]           = ColorEntry{k_error_light};
    m_colors[Platform::Color::Warning]         = ColorEntry{k_warning_light};
    m_colors[Platform::Color::ModalWindowDimBg] = ColorEntry{ImColor(0.20f, 0.20f, 0.20f, 0.35f)};

    // SceneBgTop -> SceneBgBottom: #EEF1EC -> Sage100
    m_colors[Platform::Color::SceneBgTop]         = ColorEntry{ImColor(0xEE, 0xF1, 0xEC)};
    m_colors[Platform::Color::SceneBgBottom]      = ColorEntry{k_palette_sage100};
    m_colors[Platform::Color::SceneBgErrorTop]    = ColorEntry{ImColor(0.95f, 0.70f, 0.70f, 1.0f)};
    m_colors[Platform::Color::SceneBgErrorBottom] = ColorEntry{ImColor(0.85f, 0.55f, 0.55f, 1.0f)};

    // SLA semantic colors (PLAN 2.1 / A8)
    m_colors[Platform::Color::SlaModelResin]      = ColorEntry{k_palette_sage300}; // fallback; overridden by material_colour at runtime
    m_colors[Platform::Color::SlaSupport]         = ColorEntry{k_palette_slate700};
    m_colors[Platform::Color::SlaPad]             = ColorEntry{k_palette_teal500};
    m_colors[Platform::Color::SlaSupportPointAuto] = ColorEntry{k_palette_teal500};
    m_colors[Platform::Color::SlaSupportPointManual] = ColorEntry{k_palette_slate900};
    m_colors[Platform::Color::SlaIslandWarning]   = ColorEntry{k_warning_light};
    m_colors[Platform::Color::SlaDrainHole]       = ColorEntry{k_palette_slate900};
    m_colors[Platform::Color::SlaHollowInterior]  = ColorEntry{k_palette_sage300};
    m_colors[Platform::Color::SlaCupWarning]      = ColorEntry{k_warning_light};
    // SlaLayerArea: line and fill handled at render time; token maps to line color
    m_colors[Platform::Color::SlaLayerArea]       = ColorEntry{k_palette_teal500};
}

const Domain::ColorRGBA& Theme::color(Platform::Color color_id, Platform::ColorGroup group_id) const
{
    return m_colors.at(color_id).color(group_id);
}

const ImColor& Theme::color_imgui(Platform::Color color_id, Platform::ColorGroup group_id) const
{
    return m_colors.at(color_id).color_imgui(group_id);
}

void Theme::initialize_imgui_dark_style()
{
    ImGuiStyle* style = &ImGui::GetStyle();
    ImVec4* colors    = style->Colors;

    // IMPORTANT: If the color is also used outside ImGui internals, it nees to be defined
    // in the Theme color system

    colors[ImGuiCol_Text] = color_imgui(Platform::Color::Text);
    colors[ImGuiCol_TextDisabled] =
        color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled);
    colors[ImGuiCol_WindowBg]         = color_imgui(Platform::Color::WindowBg);
    colors[ImGuiCol_ChildBg]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg]          = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
    colors[ImGuiCol_Border]           = colors[ImGuiCol_WindowBg];
    colors[ImGuiCol_BorderShadow]     = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]          = ImVec4(0.16f, 0.29f, 0.48f, 0.54f);
    colors[ImGuiCol_FrameBgHovered]   = ImVec4(0.26f, 0.59f, 0.98f, 0.40f);
    colors[ImGuiCol_FrameBgActive]    = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
    colors[ImGuiCol_TitleBg]          = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
    colors[ImGuiCol_TitleBgActive]    = ImVec4(0.16f, 0.29f, 0.48f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
    colors[ImGuiCol_MenuBarBg]        = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]      = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_ScrollbarGrab]    = color_imgui(Platform::Color::Scrollbar);
    colors[ImGuiCol_ScrollbarGrabHovered] =
        color_imgui(Platform::Color::Scrollbar, Platform::ColorGroup::Hovered);
    colors[ImGuiCol_ScrollbarGrabActive] =
        color_imgui(Platform::Color::Scrollbar, Platform::ColorGroup::Active);
    colors[ImGuiCol_CheckMark]        = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_SliderGrab]       = ImVec4(0.24f, 0.52f, 0.88f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_Button]           = color_imgui(Platform::Color::Transparent);
    colors[ImGuiCol_ButtonHovered] =
        color_imgui(Platform::Color::Button, Platform::ColorGroup::Hovered);
    colors[ImGuiCol_ButtonActive] =
        color_imgui(Platform::Color::Button, Platform::ColorGroup::Active);
    colors[ImGuiCol_Header]            = ImVec4(0.21f, 0.29f, 0.46f, 0.31f);
    colors[ImGuiCol_HeaderHovered]     = ImVec4(1.00f, 1.00f, 1.00f, 0.10f);
    colors[ImGuiCol_HeaderActive]      = ImVec4(0.21f, 0.29f, 0.46f, 1.00f);
    colors[ImGuiCol_Separator]         = colors[ImGuiCol_Border];
    colors[ImGuiCol_SeparatorHovered]  = ImVec4(0.10f, 0.40f, 0.75f, 0.78f);
    colors[ImGuiCol_SeparatorActive]   = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);
    colors[ImGuiCol_ResizeGrip]        = ImVec4(0.26f, 0.59f, 0.98f, 0.20f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
    colors[ImGuiCol_ResizeGripActive]  = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
    colors[ImGuiCol_TabHovered]        = colors[ImGuiCol_HeaderHovered];
    colors[ImGuiCol_Tab] = ImLerp(colors[ImGuiCol_Header], colors[ImGuiCol_TitleBgActive], 0.80f);
    colors[ImGuiCol_TabSelected] =
        ImLerp(colors[ImGuiCol_HeaderActive], colors[ImGuiCol_TitleBgActive], 0.60f);
    colors[ImGuiCol_TabSelectedOverline] = colors[ImGuiCol_HeaderActive];
    colors[ImGuiCol_TabDimmed] = ImLerp(colors[ImGuiCol_Tab], colors[ImGuiCol_TitleBg], 0.80f);
    colors[ImGuiCol_TabDimmedSelected] =
        ImLerp(colors[ImGuiCol_TabSelected], colors[ImGuiCol_TitleBg], 0.40f);
    colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.50f, 0.50f, 0.50f, 0.00f);
    colors[ImGuiCol_PlotLines]                 = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]          = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    colors[ImGuiCol_PlotHistogram]             = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]      = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]             = ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] =
        ImVec4(0.31f, 0.31f, 0.35f, 1.00f); // Prefer using Alpha=1.0 here
    colors[ImGuiCol_TableBorderLight] =
        ImVec4(0.23f, 0.23f, 0.25f, 1.00f); // Prefer using Alpha=1.0 here
    colors[ImGuiCol_TableRowBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]         = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    colors[ImGuiCol_TextLink]              = color_imgui(Platform::Color::TextLink);
    colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
    colors[ImGuiCol_DragDropTarget]        = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    colors[ImGuiCol_NavCursor]             = color_imgui(Platform::Color::NavCursor);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]      = color_imgui(Platform::Color::ModalWindowDimBg);
    colors[ImGuiCol_InputTextCursor]       = color_imgui(Platform::Color::Text);
}

void Theme::initialize_imgui_light_style()
{
    ImGuiStyle* style = &ImGui::GetStyle();
    ImVec4* colors    = style->Colors;

    const ImColor active = color_imgui(Platform::Color::Button, Platform::ColorGroup::Active);

    colors[ImGuiCol_Text] = color_imgui(Platform::Color::Text);
    colors[ImGuiCol_TextDisabled] =
        color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled);
    colors[ImGuiCol_WindowBg]     = color_imgui(Platform::Color::WindowBg);
    colors[ImGuiCol_ChildBg]      = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg]      = ImVec4(0.97f, 0.97f, 0.97f, 0.97f);
    colors[ImGuiCol_Border]       = colors[ImGuiCol_WindowBg];
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    // Input fields: slightly darker than window, with orange on interaction
    colors[ImGuiCol_FrameBg] = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] =
        ImVec4(k_prusa_orange.x, k_prusa_orange.y, k_prusa_orange.z, 0.25f);
    colors[ImGuiCol_FrameBgActive] =
        ImVec4(k_prusa_orange.x, k_prusa_orange.y, k_prusa_orange.z, 0.50f);
    colors[ImGuiCol_TitleBg]          = color_imgui(Platform::Color::WindowBg);
    colors[ImGuiCol_TitleBgActive]    = color_imgui(Platform::Color::WindowBgAlternate);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.96f, 0.96f, 0.96f, 0.51f);
    colors[ImGuiCol_MenuBarBg]        = color_imgui(Platform::Color::WindowBgAlternate);
    colors[ImGuiCol_ScrollbarBg]      = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_ScrollbarGrab]    = color_imgui(Platform::Color::Scrollbar);
    colors[ImGuiCol_ScrollbarGrabHovered] =
        color_imgui(Platform::Color::Scrollbar, Platform::ColorGroup::Hovered);
    colors[ImGuiCol_ScrollbarGrabActive] =
        color_imgui(Platform::Color::Scrollbar, Platform::ColorGroup::Active);
    // Orange checkmarks, sliders, and other interactive controls
    colors[ImGuiCol_CheckMark] = k_prusa_orange;
    colors[ImGuiCol_SliderGrab] =
        ImVec4(k_prusa_orange.x, k_prusa_orange.y, k_prusa_orange.z, 0.85f);
    colors[ImGuiCol_SliderGrabActive] = k_prusa_orange;
    colors[ImGuiCol_Button]           = color_imgui(Platform::Color::Transparent);
    colors[ImGuiCol_ButtonHovered] =
        color_imgui(Platform::Color::Button, Platform::ColorGroup::Hovered);
    colors[ImGuiCol_ButtonActive] =
        color_imgui(Platform::Color::Button, Platform::ColorGroup::Active);
    colors[ImGuiCol_Header]        = ImVec4(0.00f, 0.00f, 0.00f, 0.08f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.00f, 0.00f, 0.00f, 0.05f);
    colors[ImGuiCol_HeaderActive]  = active;
    colors[ImGuiCol_Separator]     = colors[ImGuiCol_Border];
    colors[ImGuiCol_SeparatorHovered] =
        ImVec4(active.Value.x, active.Value.y, active.Value.z, 0.78f);
    colors[ImGuiCol_SeparatorActive] = k_prusa_orange;
    colors[ImGuiCol_ResizeGrip] = ImVec4(active.Value.x, active.Value.y, active.Value.z, 0.20f);
    colors[ImGuiCol_ResizeGripHovered] =
        ImVec4(active.Value.x, active.Value.y, active.Value.z, 0.67f);
    colors[ImGuiCol_ResizeGripActive] = k_prusa_orange;
    colors[ImGuiCol_TabHovered]       = colors[ImGuiCol_HeaderHovered];
    colors[ImGuiCol_Tab] = ImLerp(colors[ImGuiCol_Header], colors[ImGuiCol_TitleBgActive], 0.80f);
    colors[ImGuiCol_TabSelected] =
        ImLerp(colors[ImGuiCol_HeaderActive], colors[ImGuiCol_TitleBgActive], 0.60f);
    colors[ImGuiCol_TabSelectedOverline] = colors[ImGuiCol_HeaderActive];
    colors[ImGuiCol_TabDimmed] = ImLerp(colors[ImGuiCol_Tab], colors[ImGuiCol_TitleBg], 0.80f);
    colors[ImGuiCol_TabDimmedSelected] =
        ImLerp(colors[ImGuiCol_TabSelected], colors[ImGuiCol_TitleBg], 0.40f);
    colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.50f, 0.50f, 0.50f, 0.00f);
    colors[ImGuiCol_PlotLines]                 = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]          = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    colors[ImGuiCol_PlotHistogram]             = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]      = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]             = ImVec4(0.78f, 0.78f, 0.78f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]         = ImVec4(0.57f, 0.57f, 0.64f, 1.00f);
    colors[ImGuiCol_TableBorderLight]          = ImVec4(0.68f, 0.68f, 0.74f, 1.00f);
    colors[ImGuiCol_TableRowBg]                = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]             = ImVec4(0.00f, 0.00f, 0.00f, 0.06f);
    colors[ImGuiCol_TextLink]                  = color_imgui(Platform::Color::TextLink);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(active.Value.x, active.Value.y, active.Value.z, 0.30f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    colors[ImGuiCol_NavCursor]      = color_imgui(Platform::Color::NavCursor);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.10f, 0.10f, 0.10f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.20f, 0.20f, 0.20f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]      = color_imgui(Platform::Color::ModalWindowDimBg);
    colors[ImGuiCol_InputTextCursor]       = color_imgui(Platform::Color::Text);
}

void Theme::initialize_imgui_style()
{
    // IMPORTANT: If a color is also used outside ImGui internals, it needs to be defined
    // in the Theme color system above.
    switch (m_style) {
    case Style::Dark:
        initialize_imgui_dark_style();
        break;
    case Style::Light:
        initialize_imgui_light_style();
        break;
    }
}

Theme::ColorEntry Theme::auto_entry(const ImColor& color) const
{
    return {
        color,
        std::make_unique<ImColor>(Imgui::adjust_brightness(color, 0.8f)),
        std::make_unique<ImColor>(Imgui::adjust_brightness(color, 1.2f)),
        std::make_unique<ImColor>(Imgui::adjust_brightness(color, 1.5f)),
        std::make_unique<ImColor>(Imgui::adjust_brightness(color, 1.7f)),
    };
}

Theme::ColorEntry Theme::auto_entry_light(const ImColor& color) const
{
    // On light backgrounds the hover/active states must be darker, not brighter.
    return {
        color,
        std::make_unique<ImColor>(Imgui::adjust_brightness(color, 1.12f)),
        std::make_unique<ImColor>(Imgui::adjust_brightness(color, 0.92f)),
        std::make_unique<ImColor>(Imgui::adjust_brightness(color, 0.80f)),
        std::make_unique<ImColor>(Imgui::adjust_brightness(color, 0.86f)),
    };
}

const Domain::ColorRGBA& Theme::ColorEntry::color(Platform::ColorGroup group) const
{
    return reinterpret_cast<const Domain::ColorRGBA&>(color_imgui(group));
}

const ImColor& Theme::ColorEntry::color_imgui(Platform::ColorGroup group) const
{
    switch (group) {
    case Platform::ColorGroup::Default:
        return color_default;
    case Platform::ColorGroup::Disabled:
        return color_disabled ? *color_disabled.get() : color_default;
    case Platform::ColorGroup::Active:
        return color_active ? *color_active.get() : color_default;
    case Platform::ColorGroup::ActiveDisabled:
        return color_active_disabled ? *color_active_disabled.get() : color_default;
    case Platform::ColorGroup::Hovered:
        return color_hovered ? *color_hovered.get() : color_default;
    }

    PANIC("ColorGroup is not handled!");
}

} // namespace Slic3r::App
