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
| Multi-select (Shift+drag rect) | Missing | — | Legacy: `GLSelectionRectangle` (GLGizmoSlaSupports.cpp:370) |
| Select/deselect single (click) | Partial | `SlaSupportPointsGizmo.cpp:851` | Only selects for drag; no persistent selection |
| Select all | Missing | — | Legacy: `SLAGizmoEventType::SelectAll` (GLGizmoSlaSupports.cpp:504) |
| **Island Display** | | | |
| Island points shown | Done | `SlaSupportPointsGizmo.cpp:1120` `get_point_color()` | Uses `SlaIslandWarning` color for `SupportPointType::island` |
| Island points locked | Partial | `SlaSupportPointsGizmo.cpp:739` | No `lock_unique_islands` flag yet; islands removable |
| **Clipping** | | | |
| Clipping plane slider (0-100%) | Done | `SlaSupportPointsDialog.cpp:56-62` | Slider calls `clipping_plane_changed` callback |
| Clipping plane affects view | Done | `SlaSupportPointsGizmo.cpp:118` `m_clipping_plane_presenter` | `Scene::ClipperPresenter` like PaintOnGizmoBase |
| Raycasting ignores clipped side | Done | `SlaSupportPointsGizmo.cpp:825` `raycast_mouse()` | Passes clipping plane to `MeshRaycaster::unproject_on_mesh` |
| Reset clipping button | Missing | — | Legacy: "Reset direction" button (GLGizmoSlaSupports.cpp:830) |
| Mouse wheel + Ctrl adjusts clipping | Missing | — | Legacy: `MouseWheelUp/Down` + Ctrl (GLGizmoSlaSupports.cpp:522) |
| **Point Visuals** | | | |
| Auto points (slope) | Done | `SlaSupportPointsGizmo.cpp:1120` | `SlaSupportPointAuto` color |
| Manual points | Done | `SlaSupportPointsGizmo.cpp:1120` | `SlaSupportPointManual` color |
| Island points | Done | `SlaSupportPointsGizmo.cpp:1120` | `SlaIslandWarning` color |
| Highlight hovered/dragged | Done | `SlaSupportPointsGizmo.cpp:1120` | Brightened color for dragged point |
| Sphere radius = head_front_radius (min 0.2mm) | Done | `SlaSupportPointsGizmo.cpp:1083` | `std::max(point.head_front_radius, 0.2)` |
| Cone pointing to surface normal | Missing | — | Legacy renders cone in editing mode (GLGizmoSlaSupports.cpp:313) |
| **Head Diameter** | | | |
| Head diameter slider (0.1-5.0mm) | Done | `SlaSupportPointsDialog.cpp:44-49` | Updates `head_diameter_mm` in edit state |
| Selected points follow slider | Missing | — | Legacy updates selected points live (GLGizmoSlaSupports.cpp:676) |
| **Lock/Unlock** | | | |
| Lock supports under new islands | Missing | — | Legacy: `m_lock_unique_islands` checkbox (GLGizmoSlaSupports.cpp:696) |
| **Undo** | | | |
| Undo snapshots on edits | Done | `SlaSupportPointsGizmo.cpp:688` `take_undo_snapshot()` | Called on add/remove/move/drag-end |
| Snapshot on density change | Done | `SlaSupportPointsGizmo.cpp:274` | In `density_changed` callback |
| **Shortcuts** | | | |
| Shortcut key (P) | Done | `SlaSupportPointsGizmo.cpp:262` | `m_dialog->set_shortcut("P")` |
| Delete key removes selected | Missing | — | Legacy: `SLAGizmoEventType::Delete` (GLGizmoSlaSupports.cpp:477) |
| Apply changes | Done | `SlaSupportPointsGizmo.cpp:651` `apply_edited_points()` | Button in dialog |
| Discard changes | Done | `SlaSupportPointsGizmo.cpp:676` `discard_edited_points()` | Button in dialog |
| Auto-generate button | Done | `SlaSupportPointsDialog.cpp:53` | Button in dialog |

## Summary Counts
- **Done**: 15
- **Partial**: 3
- **Missing**: 9

## Key Missing Features Requiring Follow-up
1. Multi-select with selection rectangle (needs `Scene::SelectionRectangle` equivalent)
2. Persistent point selection (click to select, not just drag)
3. Select all / Delete key handling
4. Lock/unlock island points
5. Cone visual for surface normal in editing mode
6. Reset clipping button + mouse wheel Ctrl support
7. Selected points follow head diameter slider live