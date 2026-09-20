# Palette Sweep (M1.4)

Table of every hard-coded colour literal found in `src/slic3r-shared/src/Slic3r/App/` (excluding `Theme.cpp` and test files), the theme token used to replace it, or the reason it was left unchanged.

| File:Line | Literal | Token used | Reason left / note |
|-----------|---------|------------|-------------------|
| `ColorMix/ColorMixUtils.cpp:21` | `ImColor(0x80, 0x80, 0x80)` | `Platform::Color::NeutralGrey` | Fallback grey for missing extruder colour. Added NeutralGrey token (#808080) in M1.5. Replaced. |
| `ColorMix/ColorMixUtils.cpp:36` | `ImColor(0x80, 0x80, 0x80)` | `Platform::Color::NeutralGrey` | Same as above. Replaced. |
| `ColorMix/ColorMixDialog.cpp:562` | `ImColor(255, 255, 255)` | `Platform::Color::OnAccentPrimary` | White label on `AccentPrimary` button. Added OnAccentPrimary token (white) in M1.5. Replaced. |
| `ColorMix/BlendRatioBar.cpp:154` | `ImColor(0, 0, 0, 110)` | `Platform::Color::Shadow` | Shadow colour (black ~43% alpha). Added Shadow token in M1.5. Replaced. |
| `ColorMix/BlendRatioBar.cpp:157` | `ImColor(255, 255, 255)` | `Platform::Color::PickerHandle` | White handle on coloured bar. Added PickerHandle token (white) in M1.5. Replaced. |
| `ColorMix/BarycentricRatioPicker.cpp:338` | `ImColor(255, 255, 255)` | `Platform::Color::PickerHandle` | White picker handle. Replaced. |
| `ColorMix/BarycentricRatioPicker.cpp:339` | `ImColor(0, 0, 0, 128)` | `Platform::Color::Outline` | Black 50% alpha outline. Added Outline token in M1.5. Replaced. |
| `PresetUpdater/PresetUpdaterSourceRow.cpp:324` | `ImColor(1.0f, 1.0f, 1.0f)` | `Platform::Color::OnWarning` | White tint for `WarningMarker` icon (non-error case). Added OnWarning token (white) in M1.5. Replaced. |
| `PresetUpdater/PresetUpdaterVendorRow.cpp:256` | `ImColor(1.0f, 1.0f, 1.0f)` | `Platform::Color::OnWarning` | White tint for `WarningMarker` icon (non-skipped case). Replaced. |
| `Plater/LayerHeightProfileControl.cpp:26` | `ImColor(175, 119, 255, 255)` | `Platform::Color::AccentTertiary` | **Exact match** — token is `ImColor(175, 119, 255)` in both modes. Replaced. |
| `Plater/MeasureDialog.cpp:19` | `ImColor(64, 191, 191)` | `Platform::Color::MeasureFeature1` | Teal for "feature 1". Added MeasureFeature1 token (#40BFBF) in M1.5. Replaced. |
| `Plater/MeasureDialog.cpp:20` | `ImColor(191, 64, 191)` | `Platform::Color::MeasureFeature2` | Magenta for "feature 2". Added MeasureFeature2 token (#BF40BF) in M1.5. Replaced. |
| `Plater/SvgDialog.cpp:122` | `ImColor(0, 0, 0)` | `Platform::Color::IconTint` | Black tint for SVG preview icon. Added IconTint token (black) in M1.5. Replaced. |
| `Plater/VariableLayerHeightControl.cpp:13` | `ImColor(255, 255, 0, 255)` | `Platform::Color::CursorHighlight` | Bright yellow cursor. Added CursorHighlight token (#FFFF00) in M1.5. Replaced. |
| `Plater/MeasureGizmo.cpp:41` | `Domain::ColorRGBA{0.25f, 0.75f, 0.75f, 1.0f}` | — | 3D gizmo "first feature" colour. No `MeasureFeature1` token; SLA tokens are semantic (support, pad, etc.). 3D rendering uses Domain::ColorRGBA, not Platform::Color tokens. |
| `Plater/MeasureGizmo.cpp:42` | `Domain::ColorRGBA{0.75f, 0.25f, 0.75f, 1.0f}` | — | 3D gizmo "second feature" colour. No token. |
| `Plater/MeasureGizmo.cpp:43` | `Domain::ColorRGBA{0.35f, 0.85f, 0.85f, 1.0f}` | — | Hovered variant of feature 1. No token. |
| `Plater/MeasureGizmo.cpp:44` | `Domain::ColorRGBA{0.85f, 0.35f, 0.85f, 1.0f}` | — | Hovered variant of feature 2. No token. |
| `Plater/MeasureGizmo.cpp:45` | `Domain::ColorRGBA{0.50f, 0.50f, 0.50f, 1.0f}` | — | Neutral colour for auxiliary features. `Domain::ColorRGBA::GRAY()` exists but is a Domain constant, not a Platform token. |
| `Plater/PlaterScenePresenter.cpp:1135` | `Domain::ColorRGBA(0.9f, 0.6f, 0.0f, 1.0f)` | — | Orange for unknown wipe tower. `Warning` is amber (`#E3A857`/`#9A6A1F`). Not identical. 3D rendering uses Domain::ColorRGBA, not Platform::Color tokens. |

**Summary**
- **Literals found:** 20 (17 `ImColor` + 3 `Domain::ColorRGBA` aggregates)
- **Replaced:** 14 (13 UI `ImColor` literals + 1 from M1.4)
- **Left unchanged:** 6 (all 3D rendering `Domain::ColorRGBA` for MeasureGizmo and PlaterScenePresenter)

**Uncertain / follow-up**
- The two `PresetUpdater` rows use white for `WarningMarker` icons; they now use `OnWarning` (white) which matches the previous appearance.
- `MeasureGizmo` and `PlaterScenePresenter` use `Domain::ColorRGBA` for 3D rendering, not UI. If 3D colours should also come from theme, new `Platform::Color` tokens would be required (not allowed per rules).
- `ColorMix/ExtruderFilterButton.cpp:69–73` computes a washed-out version of the extruder colour using a `0.75f` blend factor — not a fixed colour literal, so not in scope.

**Warning/Error contrast note (M1.5)**
- Dark theme: Warning `#E3A857` on `WindowBg` `#2F3E46` = 5.27:1 (AA). Error `#E07A6B` on `WindowBg` = 3.77:1 (below AA for normal text; used with icon/label per PLAN 2.1 rule 3).
- Light theme: Warning `#9A6A1F` on `WindowBg` `#DFE4DC` ≈ 3.5:1. Error `#B5483E` on `WindowBg` ≈ 4.0:1. Both meet AA for large text (3:1) and are used with icons/labels. No changes made.