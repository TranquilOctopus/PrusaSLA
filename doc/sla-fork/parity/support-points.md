# SLA Support Points Tool - Parity Checklist

Comparison between legacy `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp` and new `SlaSupportPointsGizmo`.

| Legacy Feature | Status | New Implementation (file:line) | Notes |
|----------------|--------|-------------------------------|-------|
| **Generation** | | | |
| Auto-generate points | Done | `SlaSupportPointsGizmo.cpp:491` `start_generation()` | Uses `SlaSupportPointsRequest` to slice until support spots |
| Density slider (50-200%) | Done | `SlaSupportPointsDialog.cpp:32-36` | Connected to `support_points_density_relative` config |
| **Manual Edit** | | | |
| Add point (left-click) | Done | `SlaSupportPointsGizmo.cpp:721` `add_point_at_mesh_pos()` | Adds `SupportPointType::manual_add` |
| Remove point (Ctrl+left or right-click) | Done | `SlaSupportPointsGizmo.cpp:737` `remove_point_at_index()` | Removal radius = 2× head diameter |
| Drag point (left-click+drag) | Done | `SlaSupportPointsGizmo.cpp:876` `move_point_to_mesh_pos()` | Raycasts on mouse move |
| Multi-select (Shift+drag rect) | Done | `SlaSupportPointsGizmo.cpp:1430` `start_rectangle_selection()`, `finish_rectangle_selection()` | Projects points to screen, selects inside rect |
| Select/deselect single (click) | Done | `SlaSupportPointsGizmo.cpp:1010` `select_point()`, `deselect_point()` | Persistent selection set; Shift+click toggles |
| Select all | Done | `SlaSupportPointsGizmo.cpp:1285` `select_all_points()` | Ctrl+A shortcut via `on_keyboard()` |
| **Island Display** | | | |
| Island points shown | Done | `SlaSupportPointsGizmo.cpp:1211` `get_point_color()` | Uses `SlaIslandWarning` color for `SupportPointType::island` |
| Island points locked | Done | `SlaSupportPointsDialog.cpp:72` checkbox; `SlaSupportPointsGizmo.cpp:1290` `lock_island_supports` | Checkbox in dialog; prevents move/delete when on |
| **Clipping** | | | |
| Clipping plane slider (0-100%) | Done | `SlaSupportPointsDialog.cpp:56-62` | Slider calls `clipping_plane_changed` callback |
| Clipping plane affects view | Done | `SlaSupportPointsGizmo.cpp:118` `m_clipping_plane_presenter` | `Scene::ClipperPresenter` like PaintOnGizmoBase |
| Raycasting ignores clipped side | Done | `SlaSupportPointsGizmo.cpp:825` `raycast_mouse()` | Passes clipping plane to `MeshRaycaster::unproject_on_mesh` |
| Reset clipping button | Done | `SlaSupportPointsDialog.cpp:85` `m_clipping_plane_reset_button`; `SlaSupportPointsGizmo.cpp:1255` `reset_clipping_plane()` | Button in dialog resets clipper to initial state |
| Mouse wheel + Ctrl adjusts clipping | Done | `SlaSupportPointsGizmo.cpp:910` mouse wheel handling | Ctrl+wheel moves clipping plane (copied from PaintOnGizmoBase) |
| **Point Visuals** | | | |
| Auto points (slope) | Done | `SlaSupportPointsGizmo.cpp:1211` | `SlaSupportPointAuto` color |
| Manual points | Done | `SlaSupportPointsGizmo.cpp:1211` | `SlaSupportPointManual` color |
| Island points | Done | `SlaSupportPointsGizmo.cpp:1211` | `SlaIslandWarning` color |
| Highlight hovered/dragged | Done | `SlaSupportPointsGizmo.cpp:1118` | Brightened color for dragged/hovered/selected |
| Sphere radius = head_front_radius (min 0.2mm) | Done | `SlaSupportPointsGizmo.cpp:1125` | `std::max(point.head_front_radius, 0.2)` |
| Cone pointing to surface normal | Done | `SlaSupportPointsGizmo.cpp:1146` cone rendering | Uses `TriangleMesh::make_cone`; drawn for selected points |
| **Head Diameter** | | | |
| Head diameter slider (0.1-5.0mm) | Done | `SlaSupportPointsDialog.cpp:44-49` | Updates `head_diameter_mm` in edit state |
| Selected points follow slider | Done | `SlaSupportPointsGizmo.cpp:1325` `apply_head_diameter_to_selected()` | Live update on slider change; undo snapshot on release |
| **Lock/Unlock** | | | |
| Lock supports under new islands | Done | `SlaSupportPointsDialog.cpp:72` checkbox; `SlaSupportPointsGizmo.cpp:1290` | Checkbox "Lock island supports" in dialog |
| **Undo** | | | |
| Undo snapshots on edits | Done | `SlaSupportPointsGizmo.cpp:688` `take_undo_snapshot()` | Called on add/remove/move/drag-end |
| Snapshot on density change | Done | `SlaSupportPointsGizmo.cpp:274` | In `density_changed` callback |
| **Shortcuts** | | | |
| Shortcut key (P) | Done | `SlaSupportPointsGizmo.cpp:262` | `m_dialog->set_shortcut("P")` |
| Delete key removes selected | Done | `SlaSupportPointsGizmo.cpp:1300` `on_keyboard()` | Delete/Backspace calls `delete_selected_points()` |
| Apply changes | Done | `SlaSupportPointsGizmo.cpp:651` `apply_edited_points()` | Button in dialog |
| Discard changes | Done | `SlaSupportPointsGizmo.cpp:676` `discard_edited_points()` | Button in dialog |
| Auto-generate button | Done | `SlaSupportPointsDialog.cpp:53` | Button in dialog |

## Summary Counts
- **Done**: 24
- **Partial**: 0
- **Missing**: 0

## Key Missing Features Requiring Follow-up
None - all parity items implemented.