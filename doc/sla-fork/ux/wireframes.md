# UX wireframes, part 2 (M1.2)

Companion to [journeys.md](journeys.md) (M1.1a). These are spec wireframes for M1.3 review, described textually so they render in the repo; they use only `Platform::Color` tokens. Hex values stay in the `Theme.cpp` palette table (PLAN 2.1); nothing here introduces literals. M0.5 will add the SLA semantic tokens referenced below (`SlaModelResin`, `SlaSupport`, `SlaPad`, `SlaSupportPointAuto`, `SlaSupportPointManual`, `SlaIslandWarning`, `SlaDrainHole`, `SlaHollowInterior`, `SlaCupWarning`, `SlaLayerArea`); until then these specs are design intent, not code.

Wireframe conventions: `[ ]` button, `( )` radio, `[x]` checkbox, `|A|B|` segmented control, `___` input, `···` truncated text, `░` token-filled area. Every screen: `WindowBg` background, `Text` primary text, `Control` input surfaces, `AccentPrimary` for the active/focused element, `Warning`/`Error` only for their semantic cases.

---

## W1. Support points tool (M2.1–M2.3; journeys J1 step 4)

Dialog docked beside the Plater canvas, `WindowBg` on `Slate900`-equivalent tokens; opened from a `sla_*.svg` toolbar icon (M1.9) shown only for SLA printers (M0.7).

```
┌ Support points ────────────────────────────────┐
│ [ Auto-generate ]        Generates points for  │
│                          the whole selection.  │
│ Head diameter [0.4 __] mm                      │
│ Point density [50 __] %        (maps to        │
│            support_points_density_relative)    │
│                                                │
│ Editing:  ( ) Add  ( ) Remove  ( ) Move        │
│ [x] Show island markers   → SlaIslandWarning   │
│ [x] Clipping plane  height [12.0 __] mm        │
│                                                │
│ Points: 214 total · 12 manual                  │
│ ┌────────────────────────────────────────────┐ │
│ │ Apply changes            Discard           │ │  Apply = Button (white text), Discard = ButtonTransparent
│ └────────────────────────────────────────────┘ │
└────────────────────────────────────────────────┘
```

Interaction spec:
- Canvas overlays: auto points rendered as `SlaSupportPointAuto` dots; manually placed as `SlaSupportPointManual` with a distinct outline (never hue-only — see PLAN 2.1 lightness rule). Islands drawn as `SlaIslandWarning` outlines with an icon; clicking one moves the camera to it.
- Auto-generate runs through `GeneratedSupportPointsCache` with a progress state; the dialog disables editing until it finishes or is cancelled.
- Apply commits points to the model and triggers a re-slice of the affected steps; Discard reverts to the last applied state. Both are disabled while a generation job runs.
- The header counter must distinguish manual from auto points so users can see what will persist (M2.2).
- Clipping plane is local to this tool; leaving the tool restores the previous clip state.

## W2. Hollow and drain holes tool (M2.4–M2.5; journeys J1 step 5)

Same dock as W1; the two tools are mutually exclusive tabs, not simultaneous dialogs.

```
┌ Hollow ────────────────────────────────────────┐
│ [x] Enable hollowing                           │
│ Wall thickness [2.0 __] mm                     │
│ Quality |Fast|Balanced|Quality|                │
│                                                │
│ Drain holes: 2                                 │
│ Mode: ( ) Place  ( ) Move  ( ) Resize          │
│ Diameter [4.0 __] mm                           │
│ [ Suggest positions ]  (disabled until B5b;    │
│    tooltip: "Needs trapped-resin analysis")    │
│                                                │
│ Preview is live; slicing happens on Apply.     │
│ ┌────────────────────────────────────────────┐ │
│ │ Apply changes            Discard           │ │
│ └────────────────────────────────────────────┘ │
└────────────────────────────────────────────────┘
```

Interaction spec:
- Preview shows the hollowed interior with `SlaHollowInterior`; drain holes are `SlaDrainHole` rings oriented to the surface normal.
- Parameter changes rebuild the preview in the background; the Apply button shows a busy state during rebuild. Cancelling leaves the last preview intact.
- "Suggest positions" is shown disabled with a tooltip until M4.8; there is no hidden state.
- Holes affect the sliced mesh only after Apply (M2.5 acceptance); until then the preview is visibly marked as a preview, not a result.

## W3. Preview layer inspector (M5.6; journeys J1 step 7)

Left: existing 3D SLA viewer (clipping behavior per `SlaViewer::update_preview_range`, `src/libvgcode/src/Slic3r/App/libvgcode/SlaViewer.cpp:499`). Right: new 2D panel; no change to the 3D path.

```
┌ Preview ───────────────────────────────────────────────────────┐
│  ┌ 3D view ─────────────────────┐  ┌ Layer inspector ───────┐ │
│  │  model  → SlaModelResin      │  │  current layer, 2D:    │ │
│  │  support → SlaSupport        │  │  ░░░░░░░░░░░░░░░░░     │ │
│  │  pad    → SlaPad             │  │  ░░░██░░░██░░░░░░░     │ │
│  │  (clipped to layer)          │  │  ░░░██░░░██░░░░░░░     │ │
│  │                              │  │  pixel grid at zoom    │ │
│  │  layer 341 / 1024            │  │  ░░░░░░░░░░░░░░░░░     │ │
│  └──────────────────────────────┘  │  █ = cured pixels      │ │
│  ┌ Layer slider ─────────────────┐ │      (SlaModelResin)   │ │
│  │        ●──────────────────    │ │ └────────────────────────┘ │
│  └───────────────────────────────┘  [x] Sync with 3D layer  │
│  └───────────────────────────────┘  [x] Sync with 3D layer  │
└────────────────────────────────────────────────────────────────┘
```

Interaction spec:
- The 2D view renders the layer's polygons (later the raster) at display resolution; zooming past a threshold shows the pixel grid (M5.6 acceptance).
- The layer-area chart (M5.7) docks under the 2D panel: `SlaLayerArea` line over a 40%-alpha token fill, x = layer, y = area/peel estimate; scrubbing the chart moves the slider and vice versa.
- Island and cup markers (M5.8) appear on the chart and 2D view as `SlaIslandWarning`/`SlaCupWarning` glyphs; clicking jumps to that layer and object.
- Performance target (F4/PLAN): scrubbing a 2000-layer fixture at 60 fps; the 2D panel must not force re-tessellation of the 3D scene.
- The panel is toggled and hidden by default when the window is below its minimum comfortable width (F5 layout rule).

## W4. SLA sidebar summary (M1.11 / PLAN F5; journeys J1 step 8)

Sidebar section below the existing object list; visible when the active printer is SLA.

```
┌ Print summary ─────────────────────────────────┐
│ Resin       84.2 ml          (M1.10 interactor)│
│ Cost        6.10             bottles 0.08 used │
│ Layers      1024             height 102.4 mm   │
│ Volume      model 61.0 · supports 14.2 · pad 9.0 ml │
│                                                │
│ Issues (2)                                     │
│  ⚠ 1 island  layer 218   → jump  (SlaIslandWarning) │
│  ⚠ 1 cup    layer 96     → jump  (SlaCupWarning)    │
└────────────────────────────────────────────────┘
```

Interaction spec:
- Values come from the D4 economics interactor (M1.10) and B6 stats (M4.9); before those land, the section shows placeholders, never invented numbers (A5 mock-data rule).
- Issue rows link into the W3 inspector (jump to layer); rows are `Warning` amber with icon + label (PLAN 2.1 rule 3). When no issues exist the section collapses to one quiet line.
- At minimum window size, volume breakdown wraps to two lines; no horizontal scroll.

## W5. Resin import review dialog (M3.10a; journeys J1 and M3 pipeline)

Opened from MaterialSelectionDialog (an "Import resin profile…" button in its toolbar area, M3.10b adds drag-and-drop).

```
┌ Import resin profile ────────────────────────────────────────┐
│ Source: chitubox.cfg · Chitubox 1.9 · 2026-09-17             │
│ Target printer: [ Prusa SL1S ▾ ] (all SLA printers listed;   │
│                  suggested one preselected)                  │
│ Base material: [ Prusa Orange Resin ▾ ]  (inherits tilt etc.)│
│ Preset name: [ Imported — Prusa Orange _________________ ]   │
│                                                              │
│ ┌ Mapping report ───────────────────────────────────────────┐│
│ │ exposure_time   normalExposureTime 2.5 s → 2.5 s          ││
│ │                                        Exact  (AccentPrimary)│
│ │ faded_layers    bottomLayerCount 40 → 20 (clamped to 3–20)││
│ │                                        Approximated (Warning) │
│ │ lift_speed      normalLayerLiftSpeed 30 mm/min —          ││
│ │                                        Not applicable (Text) │
│ │ light_pwm       normalLightIntensityPWM 255 —             ││
│ │                                        Not applicable (Text) │
│ │ lift_height     normalLayerLiftHeight —   Not applicable  ││
│ │                                        (Text, disabled)   ││
│ │ —               `custom_key_xyz` 42  report only, never   ││
│ │                                        imported; Unknown  ││
│ └──────────────────────────────────────────────────────────┘│
│ 1 exact · 1 approximated · 3 n/a · 1 unknown                 │
│ ┌──────────────────────────────────────────────────────────┐│
│ │ Save        Save & select        Cancel                  ││
│ └──────────────────────────────────────────────────────────┘│
└──────────────────────────────────────────────────────────────┘
```

Interaction spec:
- Status badges use exactly the tokens in the M3 design table (AccentPrimary/AccentSecondary/Warning/Text-disabled); the summary counts show **all five statuses** (Exact, Converted, Approximated, Not applicable, Unknown) so nothing is silently dropped. Each row shows source key/value → target key/value.
- Unknown keys are report-only: they never touch the material and are kept in the report for traceability. `material_source_note` records origin only (source app, file name, import date) — never mapped values or unknown keys.
- The printer picker lists **all SLA printers**, with the printer suggested from source hints preselected; it never writes machine values into the material.
- "Save" writes a user preset inheriting the base material and records `material_source_note`; "Save & select" additionally selects it. Name collisions prompt inline, preserving the report.
- The `faded_layers` clamp (3–20, per M3 mapping table) is shown with both values so approximations are auditable, e.g. `bottomLayerCount 40 → 20`.
- Every status row is selectable to reveal the raw key/value in the note pane (traceability rule in M3 "Rules").

---

## Decisions (M1.3, 2026-09-22)

1. Support points and hollow are **two separate toolbar tools**, matching `ToolType::SlaSupportPoints` and `ToolType::SlaHollow` in the code.
2. Their panels **dock on the right**, beside the canvas, like the existing gizmo dialogs.
3. The 2D layer panel is **hidden until toggled**, so slicing does not rearrange the view.
4. Summary rows **show real values**: resin economics (M1.10) and per-layer stats (M4.9) have landed, so no placeholders.
5. The import report is a **summary with a scrollable detail section**, not summary-only.

## Open questions for M1.3 review

1. W1/W2 as tabs inside one dock (spec's current assumption) vs. two separate toolbar tools — which is how the `ToolType::SlaSupportPoints` and `ToolType::SlaHollow` slots reserved in M0.7 should appear?
2. W1/W2 dock on the right beside the canvas vs. a floating panel — spec assumes right dock, matching existing gizmo dialogs.
3. W3 2D panel default state: open on first SLA preview, or hidden until toggled?
4. W4 placeholder values before M1.10/M4.9 land: show "—" or hide rows?
5. W5 report length cap for very long foreign profiles (scroll vs. summary-only).
