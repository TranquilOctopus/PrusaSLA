# SLA user journeys and provisional source audit

## Scope and evidence

This is the M1.1 partial deliverable: proposed user journeys and a source-based inventory of the active UI. It is not a runtime screen audit or an approved UX specification. Progress remains tracked only in [../ROADMAP.md](../ROADMAP.md).

Source baseline: `a2efe912a5`, inspected on 2026-09-17. References below are repository-relative `file:line` locations at that baseline. Inspection covered the active shared App/Biz layers, desktop drop target, and SLA viewer; legacy `src/slic3r/GUI` was not used as evidence of current functionality.

No application was launched, screenshot captured, dependency built, or test binary run. No runnable workspace build or attributable current SLA-screen captures were available. Static wiring does not establish that a screen is reachable, visually correct, or functional end to end. Missing-tool findings are limited to the enum, registrations, and targeted active-App searches examined.

## 1. Proposed user journeys

These are intended experiences, not claims about existing behavior. Wireframes and detailed interactions belong to M1.2; review belongs to M1.3.

### J1. First SLA print

1. Select an SL1/SL1S printer, compatible print settings, and resin. Keep the active printer and material visible during preparation.
2. Import an STL or model project. Confirm units, size, placement, and build-volume fit before generating supports.
3. Orient with rotation and Place on Face. Offer undo and preserve the original geometry. Future automatic orientation must remain an explicit action.
4. Generate supports, inspect their contact points, and apply or discard manual corrections. Do not imply that FFF support painting edits SLA points.
5. Optionally hollow the object and place drain holes. After changing orientation or hollowing, reassess supports rather than silently presenting the previous result as current.
6. Slice the intended bed. Show validation errors, progress, cancellation, and the distinction between modified and finished results.
7. Inspect model, supports, and pad, then scrub layers. Future island/cavity warnings must identify the affected object or layer; a lack of warnings is not proof of printability.
8. Export a printer-compatible archive, or explicitly choose a configured destination. Confirm file location and success; use SLA terminology instead of G-code terminology.

**Desired outcome:** a saved project and exported archive for the selected printer, with no hidden assumption that manual point/hole tools or additional archive formats already exist.

### J2. Recover from a preparation change

1. Return from Preview to Plater and change orientation, resin, supports, or hollowing parameters.
2. Clearly identify the previous slice as stale. Allow undo/redo without losing authored support points or drain holes.
3. Re-slice only the intended beds, cancel if necessary, and inspect the replacement result.
4. Export only a current finished result; failed or cancelled slicing must not masquerade as a new successful export.

**Runtime questions:** which edits actually invalidate SLA results, whether authored points/holes survive, and how cancelled jobs affect the previously finished result.

### J3. Resume an existing SLA project

1. Open an SLA 3MF as a project, distinguishing that operation from importing its geometry into another project.
2. Confirm printer/material compatibility and inspect existing object-specific settings, support points, and holes.
3. Save a copy, edit, undo/redo, reopen, and compare authored data before slicing and exporting.

**Boundary:** a sliced archive is not interchangeable with a 3MF project. Archive import is future M5.1 work; the inspected desktop drop path does not accept SL1/SL1S.

### J4. Recover from invalid input or export failure

1. Explain unsupported import types or incompatible settings without discarding the current project.
2. Identify the setting or object requiring correction; preserve user edits while navigating to it.
3. On export failure, retain the finished slice and allow a different path or destination. Do not display success before completion.

**Runtime questions:** exact error messages, recoverability after an unavailable removable drive or host, and whether the selected destination supports SLA output.

## 2. Source-based screen and behavior inventory

### A. Import and Plater entry (J1, J3, J4)

- `src/slic3r-app-desktop/src/Slic3r/App/Desktop/MainFrameDropTarget.cpp:22`: `OnDropFiles()` filters supported extensions, treats a single 3MF as a project, otherwise imports models and navigates to Plater.
- `src/slic3r-shared/src/Slic3r/Biz/FileLoadingLogic.cpp:1221`: `get_import_extensions()` lists 3MF, STL, OBJ, SVG, STEP, and STP.
- **Established by source:** desktop drop routing for projects versus models.
- **Gap/boundary:** this extension filter excludes SL1/SL1S archives. This does not prove every other import entry point rejects them.
- **Unverified screen behavior:** actual drop feedback, unit prompts, initial camera/placement, and differences between Open/Add and drag-and-drop.
- **Related work:** M5.1 archive import; M1.8 welcome flow.

### B. Printer, print settings, and resin selection (J1, J3)

- `src/slic3r-shared/src/Slic3r/App/LogicalPrinterSettingsDialog.cpp:273`: printer rows check `can_select_printer_preset()` before selecting the hardware configuration/preset and taking an undo snapshot.
- `src/slic3r-shared/src/Slic3r/App/SidebarPrint.cpp:65`: print-preset combo checks eligibility and selects a print preset.
- `src/slic3r-shared/src/Slic3r/App/MaterialSelectionRow.cpp:40`: material rows check eligibility before selection.
- `src/slic3r-shared/src/Slic3r/Biz/Preset/PresetInteractor.cpp:2114`: material selection derives preset kind from printer technology and updates material/config accessors; the wrapper signals slicing-input changes around `PresetInteractor.cpp:2175`.
- **Established by source:** selection routes and technology-aware material handling.
- **Unverified screen behavior:** whether a fresh profile offers SL1/SL1S, which resin presets are available, compatibility filtering, and switching FFF to SLA with loaded objects.
- **Related work:** M1.6 preset bundling; M1.7 SLA-first defaults; M3.10 resin import review.

### C. Orientation and toolbar (J1, J2)

- `src/slic3r-shared/src/Slic3r/App/Plater/PlaterRenderModule.cpp:988`: registration includes generic rotation and Place on Face tools.
- `src/slic3r-shared/src/Slic3r/App/Plater/RotationGizmo.cpp:556`: `enabled()` requires nonempty object selection.
- `src/slic3r-shared/src/Slic3r/App/Plater/PlaceOnFaceGizmo.cpp:113`: `enabled()` requires whole-instance selection.
- **Established by source:** generic manual orientation tools are registered; these enablement conditions do not exclude SLA.
- **Scoped gap:** no SLA automatic-orientation entry was identified in the inspected registration list and targeted active-App search.
- **Unverified screen behavior:** numerical rotation, handles, face picking, undo, multi-object behavior, and discoverability with an SLA printer selected.
- **Related work:** M4.11 algorithm; M4.12 auto-orient action.

### D. Support preparation (J1, J2, J3)

- `src/slic3r-shared/src/Slic3r/App/Plater/PlaterRenderModule.cpp:1027`: registers `PaintOnSupportsGizmo`.
- `src/slic3r-shared/src/Slic3r/App/Plater/PaintOnSupportsGizmo.cpp:358`: the painting tool uses the `FdmSupports` annotation kind.
- `src/slic3r-shared/src/Slic3r/App/Plater/PaintOnGizmoBase.cpp:1334`: inherited enablement requires FFF technology and whole-instance selection.
- `src/slic3r-shared/src/Slic3r/App/Scene/IGizmo.cpp:7`: default `supports_printer()` returns true.
- **Established by source:** registered support painting is FFF-specific, not an SLA point editor. Disabled is not equivalent to hidden.
- **Scoped gap:** no dedicated SLA point tool appears in the inspected `ToolType` enum and registrations (see E).
- **Unverified screen behavior:** whether automatic SLA supports can be enabled through generic settings and which FFF toolbar buttons remain visible or disabled.
- **Related work:** M0.7 tool slots; M0.8 FFF-tool hiding; M2.1–M2.3 SLA point editing.

### E. Hollowing, drain holes, and authored data (J1, J2, J3)

- `src/slic3r-shared/include/Slic3r/App/Scene/IGizmo.hpp:136`: complete `ToolType` enum has no dedicated SLA support-point or hollow/drain entry.
- `src/slic3r-shared/src/Slic3r/App/Plater/PlaterRenderModule.cpp:982`: inspected registration block through line 1093 has no dedicated SLA hollow/drain editor.
- `src/slic3r-shared/src/Slic3r/App/Undo/ModelSerialize.cpp:343`: undo deserialization restores `object_settings_sla`, `sla_support_points`, and `sla_drain_holes`.
- **Established by source:** authored SLA fields are retained in the inspected serialization path. This alone establishes neither editing UI nor correct persistence/undo round trips.
- **Scoped gap:** dedicated hollow/drain editing was not found in these active tool paths. Generic settings access to hollowing remains unverified.
- **Related work:** M2.4–M2.5 hollow/drain tools; M2.6 undo; M2.7 3MF round-trip tests.

### F. Slice controls and progress (J1, J2, J4)

- `src/slic3r-shared/src/Slic3r/App/Plater/SidebarPlaterActionButtons.cpp:159`: `update_slice_button()` maps invalid settings to a dialog, running jobs to Cancel, empty beds to import, and otherwise slices modified selected beds before navigation.
- `src/slic3r-shared/src/Slic3r/Biz/Slicing/SlicingInteractor.cpp:125`: `slice_bed()` queues work; cancellation removes queued requests and stops the process.
- **Established by source:** state-dependent control and cancellation wiring.
- **Unverified screen behavior:** successful SLA execution, error wording, selected-bed versus all-bed scope, cancellation recovery, and stale-result presentation. No slicing failure was reproduced or asserted by this audit.
- **Related work:** M0.2 baseline; M4.10 invalidation tests.

### G. Preview and layer inspection (J1, J2)

- `src/slic3r-shared/src/Slic3r/App/Preview/PreviewRenderModule.cpp:1409`: `update_viewer()` chooses the SLA viewer for SLA configuration and clears/resets the FDM viewer.
- `src/slic3r-shared/src/Slic3r/App/Preview/SlaViewerWrapper.cpp:68`: result-loading methods feed SLA print/object results to the viewer; only the print-result method updates the slider.
- `src/slic3r-shared/src/Slic3r/App/Preview/SlaViewerWrapper.cpp:115`: the slider uses layer heights and, around `SlaViewerWrapper.cpp:160`, forwards its thumb range to `set_layers_range()`.
- `src/libvgcode/src/Slic3r/App/libvgcode/SlaViewer.cpp:499`: layer selection applies Z clipping and builds caps from slice polygons.
- `src/slic3r-shared/src/Slic3r/App/Preview/PreviewRenderModule.cpp:219`: SLA data controls SLA-slider visibility; G-code inspector and shared legend are FFF-gated.
- **Established by source:** SLA viewer routing and 3D layer clipping, not a verified pixel-level exposure inspector.
- **Scoped gap:** no dedicated 2D raster inspector was identified in these paths.
- **Unverified screen behavior:** model/support/pad distinction, slider endpoints, single-layer behavior, reslice refresh, performance, and both themes.
- **Related work:** M2.9 visuals; M5.6 2D inspector; M5.7 chart; M5.8 issue navigation.

### H. Export and destination selection (J1, J2, J4)

- `src/slic3r-shared/src/Slic3r/App/Preview/SidebarPreviewActionButtons.cpp:141`: Finished-state actions route to filesystem/removable export, Connect, or printer upload according to destination.
- `src/slic3r-shared/src/Slic3r/App/ResultExport/ExportActions.cpp:21`: `can_export()` requires a selected finished bed.
- `src/slic3r-shared/src/Slic3r/App/Preview/SidebarPreviewActionButtons.cpp:90`: tooltip says “Export gcode to a file” without a technology condition at this assignment.
- `src/slic3r-shared/src/Slic3r/App/ResultExport/ExportPathSelect.cpp:74`: SLA save filters offer SL1/SL1S.
- `src/slic3r-shared/src/Slic3r/Biz/ResultExport/ResultExportDataFinalizer.cpp:109`: SLA finalization calls `store_sl1()`.
- `src/slic3r-shared/src/Slic3r/Biz/ResultExport/SLA/SL1.cpp:195`: writer emits configuration, layers, and thumbnails, choosing PNG/SVG layer data by file data type.
- **Established by source:** finished-state action wiring and an SL1-family payload writer; a concrete FFF-wording issue exists at the tooltip assignment.
- **Unverified screen behavior:** archive acceptance, extension selection, upload/device compatibility, errors, and overwrite/removable-drive handling.
- **Related work:** M0.9–M0.10 registry; M5.5 upload; M5.9 export flow; M1.8 SLA wording.

## 3. Interaction and visual constraints for the next design pass

Use `Platform::Color` tokens only, following [../PLAN.md](../PLAN.md) section 2.1. No visual treatment is verified by this document. M1.2 should use `Text`, `AccentPrimary`, `AccentSecondary`, `Warning`, `Error`, and the planned SLA semantic tokens rather than introducing literal colors. Warnings need an icon and text; model, supports, and pad must also be distinguishable by lightness.

Keep desktop IO in the platform shell, presentation in App, and use-case behavior in Biz. Do not treat this inventory as authorization to alter shared hotspots or port legacy GUI code wholesale.

## 4. Runtime audit checklist and evidence handoff

The following is a verification protocol, not another progress tracker. Record outcomes with the screenshot filename, observed behavior, and any discrepancy against section 2; keep todo state in ROADMAP.md.

| Case | Procedure | Evidence needed |
|---|---|---|
| R1 Setup | Start a fresh profile; select SL1/SL1S, print preset, and resin. | Printer/settings/material screens and exact selected preset IDs; note any manual fixture setup needed. |
| R2 Import | Drop a simple synthetic STL, open an SLA 3MF, and try an unsupported archive extension. | Plater state, project/model prompts, units/size, and rejection feedback. |
| R3 Orient | Rotate and Place on Face; undo/redo with single and multiple selection. | Before/after transforms and toolbar visible/disabled states in SLA mode. |
| R4 Support | Locate automatic SLA support settings and any point-editing entry. | Actual reachable controls; explicitly record absent controls rather than substituting FFF painting. |
| R5 Hollow | Locate hollowing settings and any hole editor; reopen a project with authored holes. | Reachability, geometry preview, and persistence observations; no claim of printability from appearance. |
| R6 Slice | Slice a selected bed; change resin; re-slice; cancel; test invalid settings. | Progress/error states, cancellation outcome, and stale/current result distinction. |
| R7 Inspect | Scrub first/last and a single layer; switch beds and re-slice. | Model/support/pad views, slider values, refresh behavior, and any actual 2D view. |
| R8 Export | Export SL1/SL1S to a local path; test a failed path without discarding the project. | Dialog filters, resulting filename, completion/error states, and independent archive validation result. |
| R9 Resume | Save, reopen, and undo/redo authored SLA edits. | Compared settings, points, and holes before/after; distinguish observation from automated test coverage. |
| R10 Presentation | Repeat representative Plater, Preview, and export screens in both themes and at minimum supported window size. | Captures showing text, focus, clipping, misleading FFF labels, and state distinctions. |

For every evidence set include application commit/version, OS, theme, window size/display scale, printer/material/print preset IDs, input model provenance, and exact steps. Use synthetic or redistributable models; do not commit third-party profiles or private models. Captures alone cannot verify cancellation, persistence, or archive correctness: supply workflow observations or perform those checks in a runnable build.

A build owner can complete M0.1 and provide a current desktop executable, or supply attributable captures plus observations for this audit. Source findings must then be reconciled with the observed screens before M1.1 is marked complete. Screenshots, execution results, and final UX approval are not supplied by this partial result.
