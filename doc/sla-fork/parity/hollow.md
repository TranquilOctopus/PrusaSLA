# SLA Hollow Tool - Parity Checklist

Comparison between legacy `src/slic3r/GUI/Gizmos/GLGizmoHollow.cpp` and new `SlaHollowGizmo` / `SlaDrainHolesEditing`.

| Legacy Feature | Status | New Implementation (file:line) | Notes |
|----------------|--------|-------------------------------|-------|
| **Hollowing Parameters** | | | |
| Enable/disable hollowing checkbox | Done | `SlaHollowGizmo.cpp:276` `enable_changed` callback | Connected to `hollowing_enable` config |
| Min thickness (offset) slider | Done | `SlaHollowGizmo.cpp:294` `min_thickness_changed` | Updates `hollowing_min_thickness` |
| Quality slider | Done | `SlaHollowGizmo.cpp:309` `quality_changed` | Updates `hollowing_quality` |
| Closing distance slider | Done | `SlaHollowGizmo.cpp:324` `closing_distance_changed` | Updates `hollowing_closing_distance` |
| **Preview** | | | |
| Preview button | Done | `SlaHollowDialog.cpp` preview callback; `SlaHollowGizmo.cpp:275` `start_preview()` | Triggers slicing until `slaposHollowing` |
| Preview mesh rendering | Done | `SlaHollowGizmo.cpp:602` `on_preview_completed()`, `show_preview_mesh()` | Semi-transparent mesh at `slaposHollowing` step |
| Preview status text | Done | `SlaHollowGizmo.cpp:593` | Shows "Generating preview..." / "Preview generated." |
| **Drain Hole Editing** | | | |
| Add hole (left-click on surface) | Done | `SlaHollowGizmo.cpp:1149` `add_hole_at_mesh_pos()` | Uses `SlaDrainHolesEditing::add_hole` |
| Remove hole (Ctrl+left or right-click) | Done | `SlaHollowGizmo.cpp:1114` Ctrl+click; `cpp:1164` right-click | Removal radius = 2× hole radius |
| Drag hole (left-click+drag on hole) | Done | `SlaHollowGizmo.cpp:1138` select+drag; `cpp:1177` move | Raycasts on mouse move |
| Multi-select (Shift+click toggle) | Done | `SlaHollowGizmo.cpp:1127` `toggle_hole()` | Shift+click on hole toggles selection |
| Select single (click on hole) | Done | `SlaHollowGizmo.cpp:1141` `select_hole()` | Click on hole selects it; clears others |
| Select all (Ctrl+A) | Done | `SlaHollowDialog.cpp` select all callback; `SlaDrainHolesEditing.cpp:93` `select_all_holes()` | Via dialog button / shortcut |
| **Hole Radius & Height** | | | |
| Hole diameter slider | Done | `SlaHollowDialog.cpp` hole_radius_changed; `SlaHollowGizmo.cpp:339` | Updates `hole_radius_mm` in edit state |
| Hole depth slider | Done | `SlaHollowDialog.cpp` hole_height_changed; `SlaHollowGizmo.cpp:346` | Updates `hole_height_mm` in edit state |
| Selected holes follow sliders | Done | `SlaHollowGizmo.cpp:932` `apply_radius_to_selected()`, `cpp:941` `apply_height_to_selected()` | Live update on slider change |
| New holes use current slider values | Done | `SlaDrainHolesEditing.cpp:30` `add_hole()` uses `hole_radius_mm`/`hole_height_mm` | Radius/height from dialog state |
| **Selection Visuals** | | | |
| Hover highlight | Done | `SlaHollowGizmo.cpp:1103` `m_hovered_hole_idx` | Cyan highlight in legacy; brightened color in new |
| Selected holes highlighted | Done | `SlaHollowGizmo.cpp:1260` `is_selected` check | Brightened color for selected |
| Failed holes (red) | Done | `SlaHollowGizmo.cpp:1309` `get_hole_color()` | Uses `Platform::Color::Error` for `hole.failed` |
| Hole cylinder oriented to normal | Done | `SlaHollowGizmo.cpp:1274` quaternion from normal | Cylinder aligns with surface normal |
| Cylinder radius = hole radius, height = hole height | Done | `SlaHollowGizmo.cpp:1282` scale transform | Unit cylinder scaled per hole |
| **Clipping Plane** | | | |
| Clipping plane slider (0-100%) | Done | `SlaHollowDialog.cpp` clipping slider; `SlaHollowGizmo.cpp:1205` uses scene clipper | Via `Scene::Clipper` like PaintOnGizmoBase |
| Clipping affects hole visibility | Partial | `SlaHollowGizmo.cpp:1099` raycast only | Preview mesh not clipped; holes clipped via raycast |
| Reset clipping button | Missing | — | Legacy had "Reset direction" button |
| Ctrl+wheel adjusts clipping | Missing | — | Legacy handled in `gizmo_event` |
| **Undo** | | | |
| Undo snapshots on hole edits | Done | `SlaHollowGizmo.cpp:875` `take_hole_undo_snapshot()` | Called on add/remove/move/radius/height |
| Undo on hollowing param change | Done | `SlaHollowGizmo.cpp:282` `take_snapshot` | In enable/thickness/quality/closing callbacks |
| Undo on apply/discard | Done | `SlaHollowGizmo.cpp:807` apply; `cpp:819` discard | `UndoSnapshotType::SlaDrainHolesApply/Edit` |
| **Shortcuts** | | | |
| Shortcut key (H) | Done | `SlaHollowGizmo.cpp:266` `set_shortcut("H")` | Legacy used Ctrl+H |
| Delete/Backspace removes selected | Missing | — | Legacy handled Delete key in `gizmo_event` |
| Ctrl+A select all | Done | Via dialog / `SlaDrainHolesEditing::select_all_holes()` | Not directly in gizmo keyboard handler |
| **Apply / Discard** | | | |
| Apply changes button | Done | `SlaHollowDialog.cpp` apply callback; `SlaHollowGizmo.cpp:795` `apply_edited_holes()` | Commits edited holes to model object |
| Discard changes button | Done | `SlaHollowDialog.cpp` discard callback; `SlaHollowGizmo.cpp:819` `discard_edited_holes()` | Restores original holes from model object |
| Generate holes button (auto) | Missing | — | Legacy had no auto-generate for holes; only for supports |
| Remove selected holes button | Done | `SlaHollowDialog.cpp` remove_selected; `SlaHollowGizmo.cpp:353` `delete_selected_holes()` | In dialog |
| Remove all holes button | Done | `SlaHollowDialog.cpp` remove_all; `SlaHollowGizmo.cpp:357` `select_all` + `delete` | In dialog |
| **Show Supports Toggle** | | | |
| Show/hide supports checkbox | Missing | — | Legacy had "Show supports" toggle in dialog |

## Summary Counts
- **Done**: 27
- **Partial**: 1
- **Missing**: 5

## Key Missing Features Requiring Follow-up
1. **Clipping plane reset button** – Legacy had "Reset direction" button; not yet exposed in new dialog.
2. **Ctrl+wheel for clipping** – Legacy allowed Ctrl+mouse wheel to adjust clipping plane; not implemented.
3. **Delete key handling** – Legacy Delete/Backspace key removed selected holes; not yet in new gizmo keyboard handling.
4. **Show supports toggle** – Legacy had checkbox to show/hide SLA supports; not ported (supports now separate tool).
5. **Full preview mesh clipping** – Preview mesh not clipped by clipping plane; only hole raycasting respects clipping.