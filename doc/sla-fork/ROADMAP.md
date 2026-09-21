# PrusaSLA roadmap

This is the working todo list for turning this PrusaSlicer fork into an SLA-focused slicer. It's meant to be worked through over time, one todo per session, with OpenCode (or any coding agent) or by hand.

- **Background and rules:** [PLAN.md](PLAN.md). Codebase map (section 1), color palette (2.1), file ownership and hotspot files (3.3), definition of done (3.5).
- **Agent instructions:** [/AGENTS.md](../../AGENTS.md). OpenCode loads this file automatically.
- **Tracking:** this file is the only place progress is recorded.

## How to use this file

1. **Pick a todo** whose `needs` are all ticked. In OpenCode, run `/next-todo` to have it picked for you, or `/do-todo M3.4` to name one.
2. **Skip `[human]` todos.** They need a person, for example to supply sample files, make a judgment call, or run a build that takes hours. Agents stop and report when they reach one.
3. **One todo is one branch and one commit (or a small PR):** branch `sla/<ID>-<slug>`. Tick the box **in the same commit** as the work, and add a short result note after it, for example `→ 4f2a1c9, 312 tests pass`.
4. **If a todo turns out bigger than one session,** don't half-finish it. Split it into sub-todos (`M3.4a`, `M3.4b`) in this file, commit the split, then do the first one.
5. **If you're blocked,** leave the box unticked and add a line `  Blocked: <reason>` under the todo.

Sizes: **S** is under an hour of agent work. **M** is one session. **L** must be split before starting.

Milestones are ordered by value but can overlap. Anything whose `needs` are met can start.

---

## M0: Foundation

- [x] **M0.1** `[human]` Build the dependencies once (this takes hours). Configure with `cmake --preset default`. · M · needs —
  Record in `doc/sla-fork/BUILD.md`: the deps prefix path, the configure and build commands for `sla_print_tests` and `slic3r-shared-tests`, and build times.
  Done when: `cmake --build build-default --target sla_print_tests --config RelWithDebInfo` works from a clean checkout by following BUILD.md.
  Result: verified build guide in [BUILD.md](BUILD.md); deps 5h, app 3h51m + 30m (263 min total at /MP2), Release build, 20 GB disk.
- [x] **M0.2** Baseline test run. · S · needs M0.1
  Build and run `sla_print_tests` and `slic3r-shared-tests`. Record pass, fail and skip counts, plus any tests that already fail, in `doc/sla-fork/baseline-tests.md`.
  Done when: the baseline file is committed. Already-failing tests are listed so later todos aren't blamed for them.
  Result: baseline recorded in [baseline-tests.md](baseline-tests.md); sla_print_tests 40 cases/12117 asserts pass, slic3r-shared-tests 444 cases/7573 asserts pass; no pre-existing failures.
- [x] **M0.3** Git setup: add an `upstream` remote → `https://github.com/prusa3d/PrusaSlicer.git`, create the `sla/main` branch, and document the sync procedure in BUILD.md. · S · needs —
  Result: verified local upstream URL and sla/main creation from master; sync procedure documented in [BUILD.md](BUILD.md). No push or build; remote sync not executed.
- [x] **M0.4** Reserve config keys (PLAN A7). · M · needs M0.2 · done: 21 SLA material keys, defaults change nothing (ConfigLoadTests)
  Add hidden keys with no behavior change for everything planned below, **including the generic MSLA motion keys M3 needs**:
  - `lift_height`, `lift_speed`, `lift_height_2`, `lift_speed_2`
  - `retract_speed`, `retract_speed_2`
  - `wait_before_lift`, `wait_after_lift`, `wait_after_retract`
  - `light_pwm`
  - a `bottom_` variant of each key above

  Also add `bottom_layer_count` and `material_source_note` (a free-text record of where an imported profile came from). Finalize the names here and update M3's mapping table to match.
  Done when: the keys load and save through presets and 3MF, and the tests pass.
- [x] **M0.5** Palette table and SLA theme tokens (PLAN A8 and 2.1). · M · needs M0.2 · done: palette + 10 SLA tokens, ThemeTests; screenshots deferred until an app build
  Owns: `ThemeTypes.hpp`, `Theme.cpp`. Add one palette table and the SLA tokens, and switch the existing tokens to the 2.1 default mapping.
  Done when: the app runs in the fork palette in both themes, with screenshots committed to `doc/sla-fork/ux/screens/`.
- [x] **M0.6** Reserve `SLAResult` fields for per-layer area, peel-force estimate, and a list of detected issues (left empty for now). · S · needs M0.2
  Result: `layer_areas`, `layer_peel_force` and a print-level `issues` list (`SlaIssue`) reserved in `SLAResultData`, all empty; compile-time test in `ListenersTests.cpp`. Both suites pass (sla_print_tests 40 cases, slic3r-shared-tests 445 cases). Cost note: touching `SLAResult.hpp` rebuilds most of slic3r-shared (~3.5 h at /MP2), so batch hotspot-header changes. sla_print_tests reported 12111 assertions vs 12117 at baseline with no SLA test touched: its assertion count varies between runs.
- [x] **M0.7** Reserve plater tool slots `ToolType::SlaSupportPoints` and `ToolType::SlaHollow`, with stub gizmos and empty dialogs registered in `PlaterRenderModule`. Show them only when the printer is SLA. · M · needs M0.2
- [x] **M0.8** Hide FFF-only plater tools while an SLA printer is active: seams, fuzzy skin, multi-material painting, variable layer height. · S · needs M0.7
  Result: one central check in `PlaterRenderModule::update_toolbar_visibility()` using a per-tool technology table in `ToolGizmosUiInfo`; FFF-only tools hidden for SLA and SLA tools hidden for FFF; re-evaluated on preset change. Tests in `SlaToolTypesTests.cpp`. Not checked in a running app.
- [x] **M0.9** SLA archive format registry: add the `ISlaArchiveFormat` interface and factory (PLAN A4). · M · needs M0.2 · done: Biz-layer registry with SL1 formats; rasterizer creation stays in SLAPrint
- [x] **M0.10** Move SL1 and SL1_SVG onto the registry. · M · needs M0.9
  Result: export picks the writer from the registry by `FileDataType`; formats are registered once. SL1 output still goes through the same `store_sl1`. Byte identity is covered by the M0.11a export identity test.
  Done when: exported archives are byte-identical to the previous output on the test 3MFs.
- [x] **M0.11a** Headless SLA slicing fixture for tests (`SlaSlicingFixture` in `src/slic3r-shared/test/Slic3r/Biz/SlaFixture.*`), plus determinism and SL1 export identity tests. · M · needs M0.2
  Result: two slices of the same model give byte-identical layer images and identical configs apart from per-run fields (time, config_id, fileCreationTimestamp). SL1 export through the registry equals `store_sl1` entry by entry. The fixture pumps the main-thread dispatcher while waiting and unregisters its listener. `Export sla` keeps its own fixture: two live fixtures tripped the message-handler assertion.
- [x] **M0.11b** `--sla-fixture <3mf>` debug flag that loads a sliced fixture straight into Preview (PLAN A5), plus fixture 3MFs in `doc/sla-fork/fixtures/`. · M · needs M0.11a
  Result: `--sla-fixture <file.3mf>` parsed in `ReadCLI.cpp` and handled in `DesktopApp.cpp`; documented in [BUILD.md](BUILD.md) under Debug flags. Not checked in a running app.
- [ ] **M0.12** `[human]` Choose 10–20 benchmark models. Use only models whose licenses allow redistribution, or store them outside the repo. Include miniatures, hollow figurines, flat parts, lattices, and tall thin parts. · S · needs —
- [ ] **M0.13** Benchmark harness that writes a metrics JSON (PLAN A6). · M · needs M0.11a, M0.12
  Done when: two runs on the same commit give identical layer hashes, and `doc/sla-fork/baseline.json` is committed.
- [ ] **M0.14** CI workflow: build, both test binaries, and a comment with the metrics diff (PLAN G1). · M · needs M0.13

## M1: Look, feel and SLA-first shell

- [x] **M1.1a** UX spec, part 1a: user journeys (import → orient → support → hollow → slice → inspect → export), a source-based audit of the active UI with file/line evidence, and a runtime audit checklist (R1–R10). Saved as `doc/sla-fork/ux/journeys.md`. · M · needs —
  Result: → 17b21a749a. No runtime behavior or screenshots verified; completion of the runtime audit is split into M1.1b.
- [ ] **M1.1b** Runtime screen audit with an SLA printer selected, following the R1–R10 checklist in `ux/journeys.md`. Reconcile source findings with observed screens and finish the `ux/journeys.md` audit. · S · needs M0.1
- [x] **M1.2** UX spec, part 2: wireframes for the support tool, hollow tool, layer inspector, sidebar summary, and the resin import dialog (M3.10), using palette tokens only. · M · needs M1.1a
  Result: → this commit. Text wireframes W1–W5 with interaction specs in [ux/wireframes.md](ux/wireframes.md); palette tokens only; open questions listed for M1.3 review.
- [ ] **M1.3** `[human]` Review and approve the M1.1a/M1.1b and M1.2 spec. · S · needs M1.1b, M1.2
- [x] **M1.4** Palette sweep: move RGB literals in `App/` and `libvgcode` onto theme tokens. · M · needs M0.5
  Result: swept `src/slic3r-shared/src/Slic3r/App/`: 20 colour literals found, 1 moved to a token (`LayerHeightProfileControl.cpp` → AccentTertiary), 19 left because no existing token matches and ThemeTypes.hpp is a hotspot header. Full table with reasons in [ux/palette-sweep.md](ux/palette-sweep.md). The "no RGB literals outside Theme.cpp" goal is NOT met; closing it needs new tokens, which M1.5 can add.
  Done when: a grep finds no RGB literals in `App/` outside `Theme.cpp`.
- [x] **M1.5** Tune the warning and error colors, and verify light-theme icon recoloring (PLAN 2.1 rule 3). · S · needs M1.4
  Result: 10 theme tokens added in both themes with tests; colour literals in App/ replaced where a token now fits; warning and error colours checked against the window background. See [ux/palette-sweep.md](ux/palette-sweep.md). One regression found and fixed during the build: the sweep deleted a `using namespace Biz;` line in MeasureDialog.cpp.
- [x] **M1.6** Bundle the Prusa SLA vendor presets into `resources/presets/prusa-research-sla/` and update `RepositoryManifest.json`. · M · needs M0.2
  Result: restored the Prusa SLA preset bundle deleted in cdfc079b09 (SL1/SL1S printers, sla print and material presets, 153 files), its RepositoryManifest entry and the PrusaResearchSLA vendor in PresetInteractor. Not checked in a running app: no printer has been selected from it.
  Done when: a fresh profile can select the SL1S and its materials.
- [x] **M1.7** SLA-first app setting, on by default: the first run defaults to SLA and FFF-only UI is hidden. · M · needs M0.8, M1.6
  Result: hidden bool setting `sla_first` (default true) in AppConfig; pure helper `SlaFirstPreference` decides which printer is preselected on first run (first SLA one when the flag is on, otherwise unchanged), used at the WelcomeDialog selection site; 8 helper tests. Both suites pass (485 cases). Not checked in a running app. FFF-only UI outside the plater toolbar is still shown with an SLA printer (ColorMixDialog, MaterialSettingsDialog, PrintSettingsDialog and others): that is not covered here and needs its own todo.
  Note: the first attempt (branch `sla/M1.7-sla-first`) was rejected for inventing APIs; this is the redo.
- [ ] **M1.7b** Hide or adapt the FFF-only UI outside the plater toolbar when an SLA printer is active: colour-mix dialog, material and print settings dialogs, logical printer settings, and the menu entries that go with them (M1.7 lists the files). · M · needs M1.7
- [ ] **M1.8** SLA path in the welcome dialog, plus SLA hints and notifications. Also fix the “Export gcode to a file” tooltip for SLA (evidence: `ux/journeys.md` section H). · M · needs M1.7, M1.3
- [x] **M1.9** Toolbar icons (`resources/icons/sla_*.svg`) for support points, hollow, orient, inspector and resin import, following PLAN F7. · M · needs M1.3
  Result: 10 `resources/icons/sla_*.svg` icons for the SLA tools, registered through the icon enum; the M0.7 icon test now expects the SLA-specific icons. Not checked in a running app: nobody has seen them rendered.
- [ ] **M1.10** Resin economics interactor: resin ml, cost and bottles per bed and per project (PLAN D4). · M · needs M0.11a
- [ ] **M1.11** SLA sidebar summary (PLAN F5). · M · needs M1.10, M1.3

## M2: SLA editing tools (porting the legacy gizmos)

- [x] **M2.1** Support points tool, part 1: dialog, Generate (slice until `slaposSupportPoints`, read the points from `Biz::SLAObjectCache` → `Sla::Object::support_points`), and apply/discard. · M · needs M0.7
  Result: gizmo reads points from `SLAObjectCache`; `Sla::Object::object_trafo` (= `SLAPrintObject::trafo()`) added so points convert back to mesh coordinates. Both suites pass (sla_print_tests 40 cases, slic3r-shared-tests 458 cases). Not yet verified in a running app: Generate → Apply placement on a real model (check in M1.1b).
- [x] **M2.2** Support points tool, part 2: add, remove and move points, plus head diameter. · M · needs M2.1
  Result: add (click), remove (Ctrl-click or right-click) and drag points on the surface; click hits are converted from volume to object mesh coordinates; head diameter control; one undo snapshot per edit. Editing-logic tests originally tested an in-test copy; fixed in M2.3b (they now test `SlaSupportPointsEditing`). Not checked in a running app; the select/remove radius (2 x head diameter) may need tuning.
- [x] **M2.3** Support points tool, part 3: island markers and clipping plane. Write a parity checklist against the legacy `GLGizmoSlaSupports` in `doc/sla-fork/parity/support-points.md`. · M · needs M2.2
  Result: points drawn as spheres (auto, manual and island colours from theme tokens; hovered or dragged point highlighted); clipping plane with a dialog slider, and raycasts respect it; parity checklist [parity/support-points.md](parity/support-points.md): 15 done, 3 partial, 9 missing. Both suites pass. Not checked in a running app.
- [x] **M2.3b** Close the support-tool parity gaps listed as missing in `parity/support-points.md`: multi-select, select all, Delete key, lock islands, cone-shaped point visuals, clipping reset and mouse-wheel clipping, live head-diameter update for selected points, persistent selection. · M · needs M2.3
  Result: persistent selection, Shift toggle, Shift+drag rectangle select (no rectangle drawn), Ctrl+A, Delete, live head diameter for selected points, lock-islands toggle, clipping reset and Ctrl+wheel, cone markers (aimed by a downward raycast, not the true surface normal; may tilt on curved surfaces). Editing and selection logic lives in `SlaSupportPointsEditing`, and the tests exercise it directly. Both suites pass (464 cases). Not checked in a running app.
- [x] **M2.4** Hollow tool, part 1: hollowing parameters and preview. · M · needs M0.7
  Result: hollow dialog (enable, wall thickness, accuracy, closing distance), Preview slices to `slaposHollowing` and shows the hollowed mesh, normal view restored on close; tests for parameter clamping and re-slice detection. Not checked in a running app.
- [x] **M2.5** Hollow tool, part 2: place, move and resize drain holes, plus a parity checklist against `GLGizmoHollow`. · M · needs M2.4
  Result: drain holes can be added (click), removed (Ctrl-click or right-click), moved (drag) and resized (radius/height for the selection), applied to `ModelObject::sla_drain_holes` with undo snapshots; holes drawn in the scene; helper tests in `SlaDrainHolesEditingTests.cpp`; parity checklist [parity/hollow.md](parity/hollow.md) 27 done / 1 partial / 5 missing. Both suites pass (477 cases). A supervisor fix was needed: hole normals were transformed in opposite directions in the two code paths, which would have tilted holes on rotated or scaled objects. Not checked in a running app.
- [x] **M2.6** Undo/redo for support point and drain hole edits. · M · needs M2.3, M2.5
  Result: four SLA snapshot types added (`SlaSupportPointsEdit`, `SlaDrainHolesEdit`, `SlaSupportPointsApply`, `SlaDrainHolesApply`) and used by the tools instead of borrowed ones; `sla_drain_holes` added to the undo model serialisation; round-trip test in `ModelSerializeRoundtripTests.cpp`. Not checked in a running app: no actual undo/redo has been performed.
- [x] **M2.7** 3MF round-trip tests with `sla_roundtrip{1,2}.3mf`. · S · needs M2.6
  Result: round-trip tests in `3MFTests.cpp`. Preserved: support point positions, head radius and the island flag. NOT preserved, recorded as M2.7b: point `type` (slope and manual_add both return as manual_add, since 3MF stores only an island flag), `sla_points_status`, and `object_settings_sla` (not written at all).
- [ ] **M2.7b** Extend the 3MF SLA data so nothing is lost on save/reload: support point `type` (only an island flag is stored today, so slope and manual_add both come back as manual_add), `sla_points_status`, and per-object `object_settings_sla`, which is not written at all. Keep older files readable and keep the file readable by upstream PrusaSlicer. Evidence: the M2.7 round-trip tests in `3MFTests.cpp`. · M · needs M2.7
- [x] **M2.8** Per-object SLA overrides in the object list (PLAN E4). · M · needs M0.4
  Result: the per-object override panel offers SLA categories when the printer is SLA (`SidebarObject`). The menu entry the model added was dropped: it reached a private member through the wrong base class. No automated test: the one written used APIs that do not exist. Not checked in a running app.
- [ ] **M2.9** Plater SLA visuals: resin tint, support and pad materials, and overlay styling (PLAN F3). · L → split before starting · needs M0.5, M1.3
- [x] **M2.10** Lychee support option list: the parity target is **every** Lychee support option, in [supports/lychee-features.md](supports/lychee-features.md). · S · needs —
  Result: 21 options in 7 groups (presets, tip, stem incl. geometry, base, bracing, raft, placement), compiled from public knowledge and marked unverified. M2.11 works from this list; wrong names get fixed when someone compares against the Lychee UI.
- [x] **M2.11** Parity table `doc/sla-fork/supports/lychee-parity.md`: map every M2.10 option onto existing SLA keys and mark it covered, partial or missing. Existing keys: `support_head_front_diameter`, `support_head_penetration`, `support_head_width`, `support_pillar_diameter`, `support_small_pillar_diameter_percent`, `support_base_diameter`, `support_base_height`, `support_pillar_connection_mode`, `support_max_bridge_length`, `support_critical_angle`, `support_object_elevation`, `support_tree_type`, and the `pad_*` family. Split every gap into its own todo below. · S · needs M2.10
  Result: [supports/lychee-parity.md](supports/lychee-parity.md): every Lychee option mapped to our settings. Most matches are partial because ours are global or per-object while Lychee is per support point. Missing: presets (light/medium/heavy, mini), tip shape and length, knot, stem cross-section and taper, raft types, separate brace diameter, overhang-angle threshold. 14 numbered gaps, each pointing at M2.12-M2.16 or marked "no todo yet". Note: our pad is not Lychee's raft, so M2.14 is more than a rename.
- [ ] **M2.12** Per-point support sizes (Lychee light/medium/heavy): per-point tip, stem and base dimensions stored on each support point; editor controls in the support tool; the support tree honours per-point values. It changes `Domain/SLA/SupportPoint.hpp` (heavy rebuild), so batch the header change with M2.13. · L → split · needs M2.2, M2.11
  State: data and engine done (branch merged): `SupportPoint` carries `pillar_diameter`, `base_diameter` and `base_height` (0 = use global), `DefaultSupportTree` honours them per point, and they survive undo and 3MF as optional keys with round-trip tests. Supervisor fixes were needed: the fields were first placed before `type`, silently shifting positional initialisers across the engine, the legacy 3MF reader and tests. REMAINING: the editor UI to set them per point, and the light/medium/heavy presets.
- [ ] **M2.13** Tip geometry: tip shape (cone, sphere contact), tip length and contact depth, both globally and per point, carried through to the support tree mesh. · L → split · needs M2.11
- [ ] **M2.14** Rafts: map Lychee raft types onto pad variants (full pad, thin/skate style, around object, none), with any missing variants added to the pad generator. · M · needs M2.11
- [ ] **M2.15** Bracing and cross-bracing controls beyond `support_pillar_connection_mode`, if M2.11 marks them missing. · M · needs M2.11
- [ ] **M2.16** Stem geometry: stem cross-section (round, square, polygon side count), stem taper, and knot/joint balls between tip and stem, both globally and per point, generated in the support tree mesh. Batch the `SupportPoint.hpp` change with M2.12/M2.13. · L → split · needs M2.11

## M3: Resin profile import (Chitubox, Lychee and others)

**Goal:** people coming from Chitubox or Lychee can bring their resin profiles with them. They drop in the profile file, check a mapping report, and save it as a PrusaSLA material preset.

### Design

**Why a mapping report is needed**

Other slicers and PrusaSLA don't describe a resin the same way:
- Chitubox and Lychee target Z-lift MSLA printers: lift height and speed, retract, rest times, LED PWM, and a fixed-count block of bottom layers.
- Prusa SL1/SL1S use a **tilt** mechanism: `tilt_*`, `tower_*`, and settings that differ above and below `area_fill`. They also use `faded_layers`, which steps exposure down gradually (between 3 and 20 layers) instead of switching abruptly after the bottom layers.

Some values therefore map exactly, some need a unit conversion, some can only be approximated, and some can't be carried over. The importer **never loses a value silently.** Every source key ends up in the report with one of these statuses:

| Status | Meaning | UI color token |
|---|---|---|
| Exact | Copied as-is | `AccentPrimary` |
| Converted | Unit or shape changed, for example mm/min → mm/s, or per-litre price → bottle cost | `AccentSecondary` |
| Approximated | Semantics differ; a closest value was chosen | `Warning` |
| Not applicable | Doesn't apply to the target printer, for example lift settings on a tilt printer | `Text` (disabled) |
| Unknown | Key not recognized; kept only in the report | `Text` (disabled) |

**Pipeline**

```
file ──► sniff() picks a reader ──► IResinProfileReader::read()
     ──► ForeignResinProfile[]        (neutral, source-agnostic; a file may hold several)
     ──► ResinProfileMapper::map(profile, target printer, base material)
     ──► MappingResult { PresetValueMap values; vector<MappingNote> notes; conditions }
     ──► review dialog (GUI) or JSON report (CLI)
     ──► PresetInteractor::save_user_preset(...)   (user sla_material preset that inherits from the base)
```

**Code locations** (in `src/slic3r-shared/`, following the `Biz → Domain` layering)
- `include/Slic3r/Biz/Preset/Import/ForeignResinProfile.hpp`: the neutral struct. Optional fields for name, vendor, source app and version, and layer height. Bottom layer count and transition layer count. Normal and bottom versions of exposure, lift, retract, rest times and PWM. Density, price and currency unit. Printer hints: resolution, build volume, printer name. A `raw` map with every key as read.
- `.../Import/IResinProfileReader.hpp` with `ChituboxCfgReader`, `ChituboxCfgxReader`, `LycheeLyrReader`, and `SlicedArchiveResinReader` (fallback).
- `.../Import/ResinProfileMapper.hpp`: pure functions with no UI or IO. Most of the tests live here.
- `.../Import/ResinProfileImportInteractor.hpp`: ties reading, mapping and saving together, handles name collisions (`NameValidator`), and does batch import.
- App side: `App/ResinImportDialog.*`, an entry point in `MaterialSelectionDialog`, and drag-and-drop through `FileLoadingLogic`.
- CLI: `slic3r-app-cli --import-resin-profile <file> --printer <printer-id> [--base-material <id>] [--dry-run] [--report out.json]`.

**Known source formats**

| Source | File | What's known | Plan |
|---|---|---|---|
| Chitubox (v1.x) | `.cfg` | Plain text, one `key:value` per line. String values are quoted with `\n` escapes (e.g. G-code), so **split on the first colon only.** Key spellings vary by version (`bottomLayCount` / `bottomLayerCount`, `bottomLayExposureTime` / `bottomLayerExposureTime`). Newer files add two-stage lift keys (`*2`) and `resetTimeBeforeLift`/`resetTimeAfterLift`. One file mixes machine and resin settings. | Reader in M3.4 |
| Chitubox (v2) | `.cfgx` | Structure isn't publicly documented. | Inspect samples first (M3.1) |
| Lychee Slicer | `.lyr` (resin), `.lyp` (printer) | Structure isn't publicly documented. | Inspect samples first (M3.1) |
| Any slicer's sliced output | `.sl1`/`.sl1s` `config.ini` (`expTime`, `expTimeFirst`, `numFade`, …), plus other archive readers from M5 | Exposure and motion settings are embedded in the sliced file | Fallback reader M3.8. Lychee users can export one of these. |
| Resin vendor datasheet | none (typed in by hand) | Around 6 numbers: layer height, exposure, bottom exposure, bottom layers, lift height, lift speed | Quick-entry form M3.11 |

**Chitubox `.cfg` → PrusaSLA mapping**

Unit and meaning questions marked "verify" get settled in M3.1 by comparing a sample file against the Chitubox UI. Names of new keys follow M0.4.

| Chitubox key(s) | Neutral field | PrusaSLA target | Status |
|---|---|---|---|
| `normalExposureTime` | `exposure` | `exposure_time` | Exact |
| `bottomLayerExposureTime` \| `bottomLayExposureTime` | `bottom_exposure` | `initial_exposure_time` | Exact |
| `bottomLayerCount` \| `bottomLayCount` | `bottom_layers` | tilt printers: `faded_layers`, clamped to 3–20. Generic MSLA: `bottom_layer_count` | Approximated (tilt) / Exact (generic) |
| `layerHeight` | `layer_height` | preset variant condition `print.layer_height == x` (not a material value) | Converted |
| `resinDensity` | `density` | `material_density` | Exact |
| `resinPrice` + `resinUnit` | `price_per_litre` | `bottle_cost = price × bottle_volume / 1000`, only when the unit is per litre. Currency isn't converted, which is noted. | Converted |
| `lightOffTime`, `bottomLightOffTime` | `wait_before_exposure` | `delay_before_exposure: [v, v]` (same value above and below area fill). Verify meaning. | Approximated |
| `resetTimeBeforeLift`, `resetTimeAfterLift` | `wait_before_lift`, `wait_after_lift` | tilt: `delay_after_exposure: [v, v]` for before-lift, drop after-lift. Generic: `wait_*` keys. | Approximated / Exact |
| `normalLayerLiftHeight`, `bottomLayerLiftHeight`, `*2` | `lift_height[_2]` | generic: `lift_height[_2]` and `bottom_` variants. Tilt: not applicable. | Exact / Not applicable |
| `normalLayerLiftSpeed`, `bottomLayerLiftSpeed`, `normalDropSpeed`, `*2` | `lift_speed`, `retract_speed` | generic: `lift_speed`/`retract_speed` in mm/s (source assumed mm/min; verify). Tilt: not applicable. | Converted / Not applicable |
| `normalLightIntensityPWM`, `bottomLightIntensityPWM` | `pwm` | generic: `light_pwm`. Tilt: not applicable. | Exact / Not applicable |
| `bAntiAliasing`, `antiAliasLevel`, `bImageBlur`, `minGreyLevel`, `maxGreyLevel` | raw only | not material settings; shown for information | Not applicable |
| `resolutionX/Y`, `machineWidth/Depth/Height`, `projectType`, `machineType`, `currProfile` | `printer_hints`, `name` | used to **suggest** a matching printer and a preset name; never written to the material | Converted |
| `startGcode`, `layerGcode`, `endGcode`, `displayCorrect*`, `buildAreaOffset*` | raw only | never imported | Not applicable |
| anything else | raw | report only | Unknown |

**Rules**
- **Untrusted input:** cap file size (1 MB for text; for zip-based formats, cap total uncompressed size and entry count), never evaluate G-code, handle a UTF-8 BOM, CRLF line endings, missing keys, and non-numeric values without crashing.
- **Fixtures:** write test fixtures from scratch, with key names taken from observed files. **Don't commit third-party profile files** (vendor profiles are the vendor's content). Real samples go in `local-samples/`, which is gitignored.
- **Interoperability only:** if `.cfgx` or `.lyr` turn out to be encrypted or obfuscated, **don't break the encryption.** Record that in `doc/sla-fork/formats/`, and point users to the sliced-archive fallback (M3.8) and quick entry (M3.11).
- **Traceability:** imported presets inherit from a chosen system base material for the target printer, so tilt, area-fill and other printer-specific settings stay sensible. Only the mapped values are overridden. The origin (source app, file name, import date) goes in `material_source_note`.

### Todos

- [ ] **M3.1** `[human]` Put a few real `.cfg`, `.cfgx`, `.lyr` and `.lyp` files in `local-samples/`, exported from your own Chitubox and Lychee installs or downloaded from resin vendors. For at least one Chitubox profile, write down the values the Chitubox UI shows (with units) for comparison. · S · needs —
- [ ] **M3.2** Add `local-samples/` to `.gitignore`. Document the observed structure of each sample format in `doc/sla-fork/formats/{chitubox-cfg,chitubox-cfgx,lychee-lyr}.md`: container (text, zip, JSON, binary), key list, units, and version differences. Resolve every "verify" in the mapping table. · M · needs M3.1
  Done when: each format is marked **readable**, **readable with caveats**, or **not feasible (encrypted)**, with evidence.
- [ ] **M3.3** `ForeignResinProfile` struct, `IResinProfileReader` interface, and reader registry with `sniff()`. The `.cfg` extension is generic, so detect by content (e.g. a `normalExposureTime:` line), not just the extension. · S · needs M0.2
- [ ] **M3.4** `ChituboxCfgReader`, with hand-written fixtures covering: old and new key spellings, two-stage lift, quoted multi-line G-code containing colons, CRLF line endings, BOM, unknown keys, missing keys, garbage values, and an oversized file. · M · needs M3.3
- [ ] **M3.5** `ResinProfileMapper` for tilt printers (SL1/SL1S). Implement the mapping table with statuses and unit conversions, plus table-driven tests covering every row. · M · needs M3.3
- [ ] **M3.6** `ResinProfileMapper` for generic MSLA printers, using the M0.4 motion keys. · M · needs M3.5, M0.4
- [ ] **M3.7** `ResinProfileImportInteractor`: read, map, pick a base material, save as a user preset, handle name collisions, batch import a folder, and set `material_source_note`. Test with `slic3r-shared-tests` preset fixtures. · M · needs M3.4, M3.5
- [ ] **M3.8** `SlicedArchiveResinReader`: read material settings from `.sl1`/`.sl1s` `config.ini`. Register readers for other archive formats as M5 adds them. · M · needs M3.3, M0.10
- [ ] **M3.9** CLI `--import-resin-profile` with `--dry-run` and a JSON `--report` option. · S · needs M3.7
  Done when: running it on the fixtures produces a stable JSON report, checked by a test.
- [ ] **M3.10** Import review dialog. Contents: source summary, suggested target printer (from printer hints) with a printer picker, base material picker, preset name, and a mapping table with status badges (palette tokens from the Design section). Offers "Save" and "Save & select". Entry points: an "Import resin profile…" button in `MaterialSelectionDialog`, and drag-and-drop of `.cfg`/`.cfgx`/`.lyr` onto the window. · L → split into M3.10a (dialog and table), M3.10b (entry points and drag-drop) · needs M3.7, M1.2
- [ ] **M3.11** Quick-entry form "New resin from datasheet": the ~6 datasheet fields, run through the same mapper and review table. · M · needs M3.10
- [ ] **M3.12** `ChituboxCfgxReader`, if M3.2 marked it feasible; otherwise close this todo with a link to the M3.2 finding. · M · needs M3.2, M3.3
- [ ] **M3.13** `LycheeLyrReader` (and `.lyp` printer hints), if M3.2 marked it feasible; otherwise close it with a link to the finding. · M · needs M3.2, M3.3
- [ ] **M3.14** User documentation: `doc/sla-fork/user-guide/switching-from-chitubox-and-lychee.md`, with screenshots and a table of what carries over and what doesn't. · S · needs M3.10
- [ ] **M3.15** *(optional)* Export back to Chitubox `.cfg` for people going the other way, using the same mapping in reverse. · M · needs M3.7

## M4: Engine quality (measure first; every PR includes before/after metrics)

Support generation quality has its own milestone, **M7**. M4.3–M4.5 cover regression tests and structural fixes; M7 measures quality against expert-made supports.

- [ ] **M4.1** Tracy profiling run over the benchmark set. Write a hotspot report in `doc/sla-fork/profiling/`. No code changes. · M · needs M0.13
- [ ] **M4.2** `[human]` Re-rank M4.3–M4.10 based on the M4.1 report. · S · needs M4.1
- [ ] **M4.3** Support point generation: regression tests for `tests/data/sla_islands/*.svg` and the benchmark set (PLAN B2). · M · needs M4.1
- [ ] **M4.4** Support point generation: fix the worst unsupported-island cases found in M4.3. · L → split · needs M4.3
- [ ] **M4.5** Branching tree: reduce support volume with no new unsupported points (PLAN B3). · L → split · needs M4.1
- [ ] **M4.6** Pad robustness when printing directly on the plate, plus pad generation speed (PLAN B4). · M · needs M4.1
- [ ] **M4.7** Hollowing performance and wall thickness tolerance test (PLAN B5). · M · needs M4.1
- [ ] **M4.8** Trapped-resin and suction-cup detection, with drain hole suggestions (PLAN B5b). · L → split · needs M4.7, M0.6
- [x] **M4.8b** Island detection in sliced layers: after slicing, find every region in a layer that has nothing beneath it in the layer below (no overlap with the previous layer's solid area, supports and pad included), skipping the first printed layer. Record each as a `SlaIssue{Kind::Island, layer, position}` in `SLAResultData::issues` (reserved in M0.6), with its area so tiny specks can be filtered. Report the count before export and refuse silently dropping them. Islands are what fall off the plate mid-print, so this is the highest-value check we can run on a sliced file. Tests: synthetic geometry with a known floating region (a bridge with a gap, a sphere's equator start) plus `tests/data/sla_islands/*.svg` if those fixtures suit. · M · needs M0.6
  Result: `SLA/IslandDetection.{hpp,cpp}` with a pure `detect_islands(layers, min_area_mm2)` that subtracts each layer from the one above and reports the leftovers; called from `merge_slices_and_eval_stats()`, filling `SLAResultData::issues` with `Kind::Island`, layer index, centroid and area note; minimum area 0.05 mm2. 8 tests in `tests/sla_print/sla_island_detection_tests.cpp` (SLA suite 40 -> 48 cases). A supervisor fix was needed: the area conversion was inverted (multiplying by 1/factor squared), which would have reported every speck as an island. Limitations: islands cannot be attributed to a source object (`object_id` left empty) because the layers are merged; nothing warns the user before export yet (needs UI, see M5.8); not verified on a real model.
- [x] **M4.8c** Surface island issues to the user: after slicing, show a notification when `SLAResultData::issues` contains islands (count, worst layer) and make it open the layer once the inspector exists. Blocking export is out of scope here; the full marker UI is M5.8. · S · needs M4.8b
  Result: after slicing, a notification reports island count and the lowest affected layer ("3 islands found, first on layer 42. They can fall off during printing."), closed when a new slice starts; logic in `SlaIssueAnalysis` with tests. A supervisor fix was needed: layer 0 was used as the "nothing found yet" marker, so an island on the first layer was replaced by a later one. Not checked in a running app.
- [ ] **M4.9** Per-layer stats: area, island count, peel-force estimate (PLAN B6). · M · needs M0.6
- [ ] **M4.10** Invalidation test: a table-driven test for every SLA config key, checking that only the expected steps re-run (PLAN B9). · M · needs M0.4
- [ ] **M4.11** SLA auto-orientation algorithm (PLAN B7). · L → split · needs M4.1
- [ ] **M4.12** Auto-orient plater action, as a job with progress, cancel and undo (PLAN E5). · M · needs M4.11
- [ ] **M4.13** Z-correction and anti-aliasing review. Layer hash changes must be intentional and documented (PLAN B8). · M · needs M4.1

## M5: Formats and inspection

- [ ] **M5.1** Import `.sl1`/`.sl1s`/`.slx` archives in the new app, porting the legacy `SLAImportJob` (PLAN C1). · M · needs M0.10
- [x] **M5.2** `[human]` Pick which open archive formats to support next, based on target printers (PLAN D2). · S · needs —
  Result (2026-09-19): Elegoo and Anycubic. Elegoo: `.goo` (published spec; Saturn 4 / Mars 5 families), plus `.ctb` for older machines, unencrypted versions only. Anycubic: Photon Workshop family (`.pwmx`/`.pwma`/`.pm3` older, `.pm5`/`.pm5s`/`.pm7` newer).
- [x] **M5.3** Add one todo per chosen format here (`M5.3.<fmt>`): writer, reader, spec conformance tests, and registration of its resin reader for M3.8. · S · needs M5.2
  Result: split into M5.3.* below. Every writer registers through the M0.9 registry (`ISlaArchiveFormat`) and needs M0.10. Specs and sample files stay out of the repo unless their license allows it; tests compare against locally held sample archives, like the M7 dataset rule.
- [x] **M5.3.goo** Elegoo `.goo` writer: header, preview images, RLE layer encoding and per-layer exposure/motion fields from the M0.4 keys. Tests: round-trip through a `.goo` reader in the test suite, plus a byte-level header check against a locally held Elegoo-sliced sample. · M · needs M0.10
  Result: `FileDataType::goo`; rasterizer encodes each layer with the spec's RLE scheme (magic 0x55, checksum); writer emits the big-endian header, per-layer definitions and the ending string; registered in the archive registry. Test slices a cube for goo and checks magic, layer count, resolution, layer thickness and the ending string. Both suites pass (469 cases). Unverified: no printer has read a file; previews are written as zero-filled blocks, so no thumbnail on the printer; only the fields listed in the test are checked.
- [ ] **M5.3.ctb** Chitubox `.ctb` writer for older Elegoo machines, and possibly the Anycubic Photon Mono M5 (see the M5.3.pw-b question), **unencrypted versions only**. Record which printer firmwares accept it. Encrypted versions (v4+) stay out until someone decides it's legally safe. · M · needs M0.10, M5.3.goo
- [x] **M5.3.pw-a** Anycubic Photon Mono export for the older formats (`.pwmo`, `.pwmx`, `.pwms`), ported from upstream's removed writer. · M · needs M0.10
  Result: `FileDataType::anycubic` added; `SLAPrintSteps` maps the three extensions to an Anycubic rasterizer (upstream RLE encoder); writer in `Biz/ResultExport/SLA/AnycubicSLA.cpp` registered in the archive registry. Test slices a cube for pwmx and checks the exported header (magic, version, 2560x1440, 35 um, 0.05 mm, layer count). Both suites pass (468 cases).
  Unverified: no printer has been fed one of these files; the RLE round-trip test is skipped (the encoder is file-static in libslic3r); preview thumbnails and per-variant parameters for pwmo/pwms are untested.
- [ ] **M5.3.pw-b** Anycubic newer formats (`.pm5`, `.pm5s`, `.pm7`) — **`.pm5` first: it is the format the maintainer's Photon Mono M5 reads, so nothing exported from this fork prints on that machine until it exists**: per-variant tables (resolution, pixel size, format version, preview size) on top of the M5.3.pw-a writer. · L → split per format · needs M5.3.pw-a
  Open question `[human]`: does the Photon Mono M5 also read Chitubox `.ctb`? Check in Chitubox which extension it writes for that machine. If `.ctb` (unencrypted version) works on the printer, **M5.3.ctb covers this printer as well and is the cheaper path than reverse-engineering `.pm5`**, since `.ctb` is documented by the community and shared with older Elegoo machines. Confirm before starting either one.
  Starting point: upstream's own Anycubic writer for `.pwmo`/`.pwmx`/`.pwms`, removed in the 3.0 refactor. Recover it with `git show c80474c8b4^:src/libslic3r/Format/AnycubicSLA.cpp` (and `.hpp`); it's the same repository and license. `SLAPrintSteps.cpp` still maps those extensions to `FileDataType::other`.
- [x] **M5.3.profiles** Printer presets for the target machines, each with build volume, LCD resolution, pixel size and output format, in `resources/presets/`, so the bed and plate size in the 3D view follow the selected printer. · M · needs M5.3.goo or M5.3.pw-a
  Result: new vendor bundle `resources/presets/community-sla/` with six sla_printer presets: Anycubic Photon Mono M5, M5s, M7 Pro; Elegoo Saturn 4 Ultra 12K, 16K, Mars 5 Ultra. Specs are from vendor pages, unverified against official spec sheets, and the presets say so. The Anycubic ones select pm5/pm5s/pm7, for which no writer exists yet (M5.3.pw-b).
  Target list (specs from vendor pages and reviews, **each one to be verified against the official spec sheet before shipping**):
  - **Anycubic Photon Mono M5 (the maintainer's own printer: do this one first and test on it): 218 x 123 x 200 mm, 11520 x 5120 (12K), pixel 19 x 24 um, file format `.pm5`**
  - Anycubic Photon Mono M5s: 219 x 123 x 200 mm, 11520 x 5120 (12K), pixel 19 x 24 um, `.pm5s`
  - Anycubic Photon Mono M7: 223 x 126 x 230 mm, resolution to confirm
  - Anycubic Photon Mono M7 Pro: 223 x 126 x 230 mm, 13320 x 5120 (14K)
  - Elegoo Saturn 4 Ultra 12K: 218.88 x 122.88 x 220 mm, 11520 x 5120, pixel 19 x 24 um
  - Elegoo Saturn 4 Ultra 16K: 211.68 x 118.37 x 220 mm, 15120 x 6230, pixel 14 x 19 um
  - Elegoo Mars 5 Ultra: 9K 7-inch, pixel 18 x 18 um, build volume to confirm
  Older machines (Mars 4 family, Saturn 3) use `.goo` too; add them once the first presets work.
- [ ] **M5.3.samples** `[human]` Provide one sliced sample archive per target printer (from Chitubox/Lychee/Photon Workshop) and list the printer models, so the writers can be checked. Samples stay local, like the M7 dataset. · S · needs —
- [ ] **M5.4** Display mirroring and orientation test pattern for every format (PLAN C3). · M · needs M5.3
- [ ] **M5.5** Upload SLA archives to print hosts and removable drives (PLAN C4). · M · needs M0.10
- [ ] **M5.6** Preview layer inspector: 2D layer view with a pixel grid (PLAN F4). Use mock data until M4.9 lands. · L → split · needs M0.11b, M1.3
- [ ] **M5.7** Per-layer area and peel-force chart beside the layer slider. · M · needs M5.6, M4.9
- [ ] **M5.8** Clickable issue markers (islands, cups) that jump to the layer. · M · needs M5.6, M4.8
- [ ] **M5.9** Pre-export checklist and format picker (PLAN F8). · M · needs M0.9, M1.3
- [ ] **M5.10** Height-band clipping in the 3D view, like Chitubox's preview: two sliders set a lower and an upper Z limit and the scene shows only that slice of the model, with the cut faces capped so the inside is readable. Works on the plater view (model, supports and pad) and follows the selected printer's plate size. Reuse the clipping plane the paint and SLA tools already use (`Scene::Clipper`, `Scene::ClipperPresenter`) rather than adding a second mechanism. · M · needs M1.3

## M6: Quality gates and release

- [ ] **M6.1** Robustness mesh set with no crashes or hangs (PLAN G2). · M · needs M0.13
- [ ] **M6.2** Visual regression renders, including the grayscale lightness check (PLAN G3). · M · needs M0.11b, M0.5
- [ ] **M6.3** Nightly upstream merge rehearsal with a conflict report (PLAN G4). · S · needs M0.3, M0.14
- [ ] **M6.4** `[human]` End-to-end walk through the M1.1 journeys on an integrated build, filing new todos for gaps. · M · needs M1.11, M2.7, M3.10
- [ ] **M6.5** Retune default presets after the M4 changes. · M · needs M4.4, M4.5
- [ ] **M6.6** Fork README and user guide. · S · needs M6.4
- [ ] **M6.7** `[human]` Release candidate: version bump, packaging, known-issues list. · M · needs M6.1–M6.6

## M7: Excellent auto-supports

> **Parked (2026-09-18).** The priority is the regular support function: the support points tool (M2.1–M2.3) and support generation quality (M4.3–M4.5). M7.4 work in progress is on the unmerged branch `sla/M7.4-rule-mining`. Known issues there: tip width saturates at the search radius, and registration error (0.5–0.9 mm RMS) exceeds the contact gap, so most contacts are missed. Refine registration on the model-only vertices before detecting contacts.

**Goal:** on typical models (miniatures, busts, functional parts), auto-generated supports print reliably and match what an experienced person would do. Someone experienced shouldn't *need* to hand-edit them, and cleanup should leave no scars on detailed or cosmetic surfaces.

**Approach**
1. **Interview:** write down expert knowledge as explicit rules (M7.1).
2. **Research dataset:** supported scenes made by an experienced person in another slicer (currently Lychee), exported as plain meshes by that slicer (M7.2).
3. **Offline analysis:** scripts in `tools/support-research/` (Python, not part of the app build) extract every support contact (M7.3) and mine measurable rules from them (M7.4).
4. **Rulebook:** interview rules and mined rules are merged and reviewed (M7.5).
5. **Scoring:** the generator is scored against the expert supports, with a baseline first (M7.6, M7.7).
6. **Implementation:** the generator is improved rule by rule, with every change scored, then tuned (M7.8–M7.11).
7. **Confirmation:** real prints check the result (M7.12).

**Research-only rules**
- **Nothing is ported.** The dataset is only for *learning rules*. PrusaSLA gets no reader for other slicers' project formats, and the data comes from the authoring slicer's own mesh export. Don't decode `.lys` or other protected project files.
- **Personal-use license.** The current dataset is Factory Fortress Trench Crusade miniatures. Their Terms of Use grant a personal-use, single-computer license; forbid sharing, distribution and commercial or employer use; and allow modification only for personal use. So:
  - **Files stay in place.** Leave them in the owner's own folder, referenced through a manifest. Don't copy them into the repo tree or into `local-samples/`.
  - **Files never leave this computer.** No CI, cloud agents, uploads or file sharing.
  - **Agents don't open mesh files** in their context. They run local analysis scripts and read only the aggregate output.
  - **Only aggregate numbers are committed**, such as spacing, angles and diameters, never geometry or per-model contact lists. The rulebook, and synthetic test shapes made from scratch, can be committed.
  - **It stays a personal research activity.** If this work is done for an employer, the owner must check the license first.

- [ ] **M7.1** `[human]` Expert supporting interview. Record the answers in `doc/sla-fork/supports/expert-rules.md`, one numbered rule per practice. Cover: process order, tip sizes, density, surfaces never to support, orientation, structure/base style, and common auto-support failures. · M · needs —
- [x] **M7.2** `[human]` Prepare the research dataset. Factory Fortress Trench Crusade models, already supported by the studio in Lychee: each model folder has `Supported/` (a `.lys` scene plus the model with supports as one STL) and `Unsupported/` (the plain STL).
  - **Download a starter set.** In File Explorer, right-click → "Always keep on this device" for 4–6 varied model folders (iCloud downloads time out from agent sessions). Suggested mix:
    - a small detail part (e.g. `TCDM2009 Shocktroopers` heads)
    - a weapon or arm
    - a base or rock (`TCDM4003 Sorcerer` rock base)
    - a full figure (`TCDM5004 Assassin`)
    - a large model
  - **Record the dataset** in `local-samples/supports/manifest.yaml` (gitignored). For each model: `supported_stl` and `unsupported_stl` paths into the iCloud folder, category, `supported_by: Factory Fortress`, license `personal-use (FF ToU)`, and print outcome if known. Also record the Lychee tip presets if visible when opening the `.lys` in Lychee.

  · S · needs —
  Result: 21 locally available pairs (6 figure bodies, 5 weapons, 5 heads, 2 arms, 1 base, 1 full figure, 1 mechanical part), all with Lychee scenes, recorded in the gitignored `local-samples/supports/manifest.yaml`. Tip presets and print outcomes are still open (optional). Alignment spot check: supported STLs are re-oriented and sometimes remeshed (see M7.3).
- [ ] **M7.3** Contact extraction script. · L → split · needs M7.2
  - **Register the pair.** A check on 3 pairs (2026-09-17) found the supported STLs are **not** in the unsupported model's frame: they are rotated and moved into print orientation, and sometimes remeshed (one head had 31k triangles supported vs 260k unsupported). No triangles matched even at 0.01 mm. So:
    - Estimate the rigid transform (rotation + translation, no scale) from the unsupported model into the supported file: coarse alignment (principal axes plus the ambiguous flips, or feature-based global registration), then point-to-plane ICP on the model surface only, ignoring the support region as outliers.
    - Report the residual (RMS and 95th-percentile surface distance) and the recovered print tilt. Reject pairs above tolerance.
  - **Separate supports from the model:** after registration, take faces of the supported mesh farther than ε from the transformed model surface. Triangle-exact matching must not be assumed, because the model may be remeshed.
  - **Record orientation:** the recovered rotation is itself data for the orientation rules (M7.4).
  - **Describe each contact:** position, surface normal, contact diameter, penetration estimate, height above the plate, local overhang angle and curvature, and whether it serves a new island or local minimum (found by slicing the model mesh into layers).
  - **Record structure:** base/raft type and trunk and branch counts.
  - **Outputs** (under `local-samples/supports/out/`, gitignored and never committed): `contacts.json`, plus a colored debug mesh for a quick check by eye.

  Done when: a synthetic test shape with procedurally placed supports, then rotated, moved and remeshed, gives back the known transform and every known contact within tolerance.
- [x] **M7.3a** Synthetic test shape + registration: build a synthetic model (sphere-on-cube style) with procedurally placed supports from scratch, apply a known rigid transform (rotation + translation), remesh it, and write the registration routine (coarse principal-axes alignment with flip disambiguation, then point-to-plane ICP on the model surface ignoring supports) plus the residual report (RMS and 95th-percentile surface distance) and recovered print tilt. Verify the recovered transform matches the known one and residuals stay within tolerance. Code in `tools/support-research/` (Python, local venv). · M · needs M7.2
  Result: 12 tests pass (8 end-to-end + 4 geometry unit). Measured on synthetic asymmetric L-block: rotation error 0.010°, translation error 0.0008 mm, inlier fraction 0.983, RMS 0.145 mm, p95 0.246 mm. All within tolerance (rot < 1°, trans < 0.2 mm, inlier ≥ 0.9, RMS < 0.15, p95 < 0.3). Mirrored/wrong alignments correctly rejected.
- [x] **M7.3b** Support separation: after registration, classify supported-mesh faces as model vs support by distance to the transformed model surface (ε threshold), robust to remeshing. Validate on the synthetic shape where the split is known. · M · needs M7.3a
  Result: batched exact centroid-to-surface distances in `tools/support-research/separate.py`, with face-order-preserving masks and rigid-transform validation. Synthetic original, transformed and remeshed splits verified; registered/remeshed split correctly labels all 816 model and 156 support faces at ε = 0.3 mm. 21 new tests pass (slowest 3.65 s); full Python suite: 33 pass in 54.44 s (existing registration test 44.39 s). No real dataset access or C++ builds. Centroid labels do not resolve mixed/contact-boundary faces; real-data accuracy and large-mesh memory remain unverified. No Python lint/typecheck command is configured.
- [x] **M7.3b2** Scale support separation to full-size meshes (1-2 million faces): KD-tree pre-filter over dense area-weighted samples of the transformed model surface; faces clearly beyond epsilon are support, clearly within are model, and only the ambiguous band gets exact closest-point distances. Done when: results identical to the exact method on the existing synthetic tests, plus a benchmark on a synthetic mesh of at least 300k faces showing time and peak memory. · M · needs M7.3b
  Result: `separate_supports_scaled()` in `tools/support-research/separate.py`. Nearest-sample distance bounds the true distance from above, so a face is model as soon as that bound is below epsilon (no margin), while the support side needs a safety margin derived from the achieved sample spacing; only the band between is measured exactly. Sample count follows surface area with a cap, and scene faces are processed in chunks. Benchmarks (synthetic, in memory): 348k faces 1.3 s / 70 MB peak, 1.39M faces 3.7 s / 279 MB peak (process RSS 354 MB), 100% agreement with the known split and 400/400 faces identical to the exact method. 50 separation tests pass in 9.8 s, including sparse-sampling and chunking equivalence.
- [x] **M7.3c** Contact description + outputs: for each contact record position, normal, contact diameter, penetration, height above plate, local overhang angle and curvature, island/local-minimum serving (via slicing the model mesh), base/raft and trunk/branch structure. Outputs `contacts.json` and a colored debug mesh under `local-samples/supports/out/` (gitignored). Done when the synthetic shape gives back every known contact within tolerance and the M7.3 "Done when" is fully satisfied. · M · needs M7.3b2
  Result: 13 tests pass in test_m73c.py; full support-research suite 75 tests pass in ~68 s. Synthetic cone tips (radius 0.6 mm, height 1.2 mm) yield measured diameter 1.2 mm (2×TIP_RADIUS), vertex_count ≥ 17 (all tip vertices in width_band), penetration ~0, overhang ~0°, serves_local_minimum=True. Contacts.json and debug mesh outputs verified.
- [ ] **M7.4** Rule mining across the dataset. Measure things like:
  - Island and minimum coverage
  - Contact spacing against overhang angle
  - Tip diameter against the area or volume it carries
  - Density on flat undersides compared with edges and points
  - Surfaces that are never touched
  - Orientation and tilt angles

  Write the result to `doc/sla-fork/supports/derived-rules.md`: each rule with aggregate evidence and a confidence level, and no per-model data. · M · needs M7.3
- [ ] **M7.5** `[human]` Rulebook review. Merge the interview rules (M7.1) with the mined rules (M7.4) into `doc/sla-fork/supports/rulebook.md`. Mark each rule accepted, modified or rejected, with concrete thresholds. · M · needs M7.1, M7.4
- [ ] **M7.6** Support quality scorecard. The benchmark harness writes the generator's support points and tip sizes to JSON, and a research script compares them with the expert contacts. Metrics:
  - **Island recall**
  - **Contact coverage** within *r* mm
  - **Excess contacts**
  - **Density error per region**
  - **Tip size distribution**
  - **Contacts on cosmetic surfaces**
  - **Support volume**
  - **Unsupported-point count**

  · M · needs M7.3, M0.13
- [ ] **M7.7** Baseline scorecard for the current generator (default and branching tree). Commit only the aggregate numbers to `doc/sla-fork/supports/baseline.md`. · S · needs M7.6
- [ ] **M7.8** Implement the rulebook in the generator, one sub-todo per rule (`M7.8.<n>`). Each must raise the scorecard without regressing island recall, and add a synthetic regression shape to `tests/sla_print/`. · L → split · needs M7.5, M7.7
- [ ] **M7.9** Cosmetic surface awareness. Detect likely cosmetic surfaces (faces, fine detail, high-curvature upward regions) using the thresholds in the rulebook. Let users paint "avoid supports here" regions that the generator respects. Connect this to the orientation objective (M4.11). · L → split · needs M7.5, M2.3
- [ ] **M7.10** Size tips for each region, following the rulebook: light tips on small detail, heavier tips where the load above is high (estimated from cross-section area and resin weight above the point), within printer and resin limits. · M · needs M7.5, M7.7
- [ ] **M7.11** Automated tuning. Search the generator parameters against the dataset scorecard, and promote the best settings to the default print presets, noting the trade-offs. · M · needs M7.8, M7.10
- [ ] **M7.12** `[human]` Print validation. Print a set of models using auto supports only. Record the results in `doc/sla-fork/supports/print-log.md`: success, detached supports, failed islands, scars. Add failures as new M7 todos. · M · needs M7.11
- [ ] **M7.13** *(optional research)* A learned contact predictor trained on the extracted contacts, used only as a scoring hint for the rule-based generator. Only pursue it if M7.11 levels off. Inference must stay in C++ without heavy new dependencies. · L → split · needs M7.11
