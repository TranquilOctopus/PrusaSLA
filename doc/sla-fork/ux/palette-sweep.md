# Palette Sweep (M1.4)

Table of every hard-coded colour literal found in `src/slic3r-shared/src/Slic3r/App/` (excluding `Theme.cpp` and test files), the theme token used to replace it, or the reason it was left unchanged.

| File:Line | Literal | Token used | Reason left / note |
|-----------|---------|------------|-------------------|
| `ColorMix/ColorMixUtils.cpp:21` | `ImColor(0x80, 0x80, 0x80)` | — | Fallback grey for missing extruder colour. No neutral "medium grey" token exists (`Text`/`Control` are theme-dependent). Changing would alter fallback appearance. |
| `ColorMix/ColorMixUtils.cpp:36` | `ImColor(0x80, 0x80, 0x80)` | — | Same as above. |
| `ColorMix/ColorMixDialog.cpp:562` | `ImColor(255, 255, 255)` | — | White label on `AccentPrimary` button. No `OnAccentPrimary` token; `Text` is not white in either mode. Would change contrast. |
| `ColorMix/BlendRatioBar.cpp:154` | `ImColor(0, 0, 0, 110)` | — | Shadow colour (black ~43% alpha). No shadow/overlay token. |
| `ColorMix/BlendRatioBar.cpp:157` | `ImColor(255, 255, 255)` | — | White handle on coloured bar. No "white" token. |
| `ColorMix/BarycentricRatioPicker.cpp:338` | `ImColor(255, 255, 255)` | — | White picker handle. No "white" token. |
| `ColorMix/BarycentricRatioPicker.cpp:339` | `ImColor(0, 0, 0, 128)` | — | Black 50% alpha outline. No shadow/outline token. |
| `PresetUpdater/PresetUpdaterSourceRow.cpp:324` | `ImColor(1.0f, 1.0f, 1.0f)` | — | White tint for `WarningMarker` icon (non-error case). `Warning` token is amber, not white. Would change appearance. |
| `PresetUpdater/PresetUpdaterVendorRow.cpp:256` | `ImColor(1.0f, 1.0f, 1.0f)` | — | White tint for `WarningMarker` icon (non-skipped case). Same as above. |
| `Plater/LayerHeightProfileControl.cpp:26` | `ImColor(175, 119, 255, 255)` | `Platform::Color::AccentTertiary` | **Exact match** — token is `ImColor(175, 119, 255)` in both modes. Replaced. |
| `Plater/MeasureDialog.cpp:19` | `ImColor(64, 191, 191)` | — | Teal for "feature 1". `AccentSecondary` (dark) = `#52796F`, `SlaPad` = `#52796F`. Literal `#40BFBF` is brighter. No exact token. |
| `Plater/MeasureDialog.cpp:20` | `ImColor(191, 64, 191)` | — | Magenta for "feature 2". `AccentTertiary` = `#AF77FF`. Close but not identical. |
| `Plater/SvgDialog.cpp:122` | `ImColor(0, 0, 0)` | — | Black tint for SVG preview icon. `Text` is light in dark mode, dark in light mode — not a stable black. No "black" token. |
| `Plater/VariableLayerHeightControl.cpp:13` | `ImColor(255, 255, 0, 255)` | — | Bright yellow cursor. `Warning`/`SlaIslandWarning`/`SlaCupWarning` map to amber, not pure yellow. |
| `Plater/MeasureGizmo.cpp:41` | `Domain::ColorRGBA{0.25f, 0.75f, 0.75f, 1.0f}` | — | 3D gizmo "first feature" colour. No `MeasureFeature1` token; SLA tokens are semantic (support, pad, etc.). |
| `Plater/MeasureGizmo.cpp:42` | `Domain::ColorRGBA{0.75f, 0.25f, 0.75f, 1.0f}` | — | 3D gizmo "second feature" colour. No token. |
| `Plater/MeasureGizmo.cpp:43` | `Domain::ColorRGBA{0.35f, 0.85f, 0.85f, 1.0f}` | — | Hovered variant of feature 1. No token. |
| `Plater/MeasureGizmo.cpp:44` | `Domain::ColorRGBA{0.85f, 0.35f, 0.85f, 1.0f}` | — | Hovered variant of feature 2. No token. |
| `Plater/MeasureGizmo.cpp:45` | `Domain::ColorRGBA{0.50f, 0.50f, 0.50f, 1.0f}` | — | Neutral colour for auxiliary features. `Domain::ColorRGBA::GRAY()` exists but is a Domain constant, not a Platform token. |
| `Plater/PlaterScenePresenter.cpp:1135` | `Domain::ColorRGBA(0.9f, 0.6f, 0.0f, 1.0f)` | — | Orange for unknown wipe tower. `Warning` is amber (`#E3A857`/`#9A6A1F`). Not identical. |

**Summary**
- **Literals found:** 20 (17 `ImColor` + 3 `Domain::ColorRGBA` aggregates)
- **Replaced:** 1 (`LayerHeightProfileControl.cpp:26` → `AccentTertiary`)
- **Left unchanged:** 19 (no matching token, or token would change visual appearance)

**Uncertain / follow-up**
- The two `PresetUpdater` rows use white for `WarningMarker` icons; arguably they should use `Platform::Color::Warning` but that would change the look from white to amber. Product/design input needed.
- `MeasureGizmo` and `PlaterScenePresenter` use `Domain::ColorRGBA` for 3D rendering, not UI. If 3D colours should also come from theme, new `Platform::Color` tokens would be required (not allowed per rules).
- `ColorMix/ExtruderFilterButton.cpp:69–73` computes a washed-out version of the extruder colour using a `0.75f` blend factor — not a fixed colour literal, so not in scope.