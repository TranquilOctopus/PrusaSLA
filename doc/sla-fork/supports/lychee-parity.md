# Lychee support options: parity table

Generated from `lychee-features.md` (21 rows in 7 groups). Each row maps a Lychee option to our closest equivalent.

| # | Lychee option | Our equivalent (config key or UI place) | Status | Evidence | Note |
|---|---|---|---|---|---|
| 1 | **Presets: Light / Medium / Heavy (plus custom)** — named bundle of tip, stem, base dims | — | missing | — | No support-specific preset bundles. We have `support_tree_type` (Default/Branching) but no named dimension presets. |
| 2 | **Presets: Mini / tiny supports** — very small presets for fine detail | `support_preset_mini_head_diameter` / `_pillar_diameter` / `_base_diameter` / `_base_height`; "Mini" button in the support tool | covered | ConfigDefsSLA.cpp:1678,1692-1733; SlaSupportPointsDialog.cpp:140-144; SlaSupportPointsGizmo.cpp:1562-1566 | Four settings like the other three presets (M2.18b), so Mini is 0.2 mm tip, 0.5 mm pillar, 1.4 mm base of 0.4 mm, all smaller than Light. Placed by the button next to Light / Medium / Heavy. |
| 3 | **Tip: Contact (tip) diameter** — diameter where tip touches model | `support_head_front_diameter` (global/per-object); per-point `head_front_radius` in `SupportPoint`; UI head-diameter slider + **Apply to selected** | partial | ConfigDefsSLA.cpp:1274; SupportPoint.hpp:37; SlaSupportPointsDialog.hpp:25,43; SlaSupportPointsEditing.cpp:131-138 | Config is global/per-object only. Per-point editing exists in UI (select points → set head diameter → Apply). Lychee allows per-point in preset; we require manual selection. |
| 4 | **Tip: Tip length** — length of tapered tip section | `support_head_width` (pinhead width, back-sphere to front-sphere center) | partial | ConfigDefsSLA.cpp:1300 | `support_head_width` controls pinhead length, not a pure tip taper length. Geometry differs (double-sphere pinhead vs. simple cone). |
| 5 | **Tip: Tip depth / penetration** — how far tip sinks into model surface | `support_head_penetration` | partial | ConfigDefsSLA.cpp:1287 | Global/per-object config only; no per-point override in config. |
| 6 | **Tip: Tip shape** — cone or sphere/ball contact | — | missing | — | Our pinhead is always a double-sphere (front + back). No cone-only or sphere-only choice. |
| 7 | **Tip: Knot / joint** — optional ball between tip and stem, and its size | — | missing | — | No knot/joint parameter. Pinhead connects directly to pillar. |
| 8 | **Stem: Stem (body) diameter** — main column diameter | `support_pillar_diameter` | partial | ConfigDefsSLA.cpp:1314 | Global/per-object config only; no per-point override in config. |
| 9 | **Stem: Stem geometry** — cross-section (round, square, polygon side count) | — | missing | — | Pillars are always round. No polygon/cross-section choice. |
| 10 | **Stem: Stem taper** — diameter changes along stem | — | missing | — | Pillars are constant-diameter cylinders. No taper parameter. |
| 11 | **Stem: Support on model** — stems that start on model instead of raft | `support_max_weight_on_model`; `support_buildplate_only` | partial | ConfigDefsSLA.cpp:1361,1395 | `support_max_weight_on_model` limits total branch length ending on model; `support_buildplate_only` disables model supports entirely. No per-point "start on model" toggle. |
| 12 | **Stem: Support on support / branching** — stems that join into other stems | `support_tree_type = Branching`; `support_max_bridges_on_pillar`; `support_max_pillar_link_distance`; `support_pillar_connection_mode` | covered | ConfigDefsSLA.cpp:522-538,1343,1497,1376 | Branching tree type enables pillar-to-pillar links. Bridging/linking params control density/length. |
| 13 | **Base: Base (foot) diameter** — diameter where stem meets raft/plate | `support_base_diameter` | partial | ConfigDefsSLA.cpp:1423 | Global/per-object config only; no per-point override in config. |
| 14 | **Base: Base height and shape** — cone or cylinder foot, and its height | `support_base_height` | partial | ConfigDefsSLA.cpp:1437 | Height covered. Shape is always a cone; no cylinder/flat option. |
| 15 | **Bracing: Braces / cross-braces** — automatic links between stems | `support_pillar_connection_mode` (zigzag/cross/dynamic) | covered | ConfigDefsSLA.cpp:1376 | Cross/zigzag modes create automatic cross-bracing between nearby pillars. |
| 16 | **Bracing: Brace diameter, spacing, angle, pattern** — brace size and layout | Pattern: `support_pillar_connection_mode`; Angle: `support_critical_angle`; Spacing: `support_max_pillar_link_distance`; Diameter: `support_brace_diameter` (0 = as thick as the pillar) | covered | ConfigDefsSLA.cpp:1432,1462,1561,1591; DefaultSupportTree.cpp:206,256 | All four covered. Since M2.15 the brace diameter is a setting of its own, next to `support_brace_enable` and `support_brace_start_height`. |
| 17 | **Raft: Raft type** — standard, skate, grid/honeycomb, none | Pad system (`pad_enable`, `pad_wall_thickness`, `pad_wall_height`, `pad_brim_size`, `pad_wall_slope`) | partial | ConfigDefsSLA.cpp:562-652 | Pad is a solid base with optional cavity walls. No "skate", "grid", or "honeycomb" type selection. `pad_enable=false` ≈ "none". |
| 18 | **Raft: Raft thickness, margin/offset, chamfer/slope** | Thickness: `pad_wall_thickness` (conflated with wall); Margin: `pad_brim_size`; Chamfer/slope: `pad_wall_slope` | partial | ConfigDefsSLA.cpp:572,602,640 | Margin and slope covered. "Thickness" is the wall thickness; no separate raft-floor thickness parameter. |
| 19 | **Placement: Model elevation (Z lift)** | `support_object_elevation`; `pad_around_object` / `pad_around_object_everywhere` | covered | ConfigDefsSLA.cpp:1511,654,664 | Elevation config exists. Pad-around-object modes modify behavior. |
| 20 | **Placement: Auto-support density and minimum distance** | Density: `support_points_density_relative`; Minimum distance: `support_points_minimal_distance` | covered | ConfigDefsSLA.cpp:575,587; SupportPointGenerator.hpp:39-45; SupportPointGenerator.cpp:257-312 | Density (%) still covered. M2.22 adds the minimal distance: the generator refuses an overhang point nearer to another one than the value, measured in the layer. Island points are never filtered out, and zero (the default) leaves every point where it was. |
| 21 | **Placement: Overhang angle threshold and island detection** | Overhang angle: `support_points_overhang_angle`; Island detection: `SupportPointType::island` in SupportPoint | covered | ConfigDefsSLA.cpp:601; SupportPointGenerator.cpp:565-598; SupportIslands/SampleConfig.hpp:38-41; SupportPoint.hpp:20,42; ConfigDefsSLA.cpp:1607 (`support_critical_angle` is for stick junctions, not overhang) | Island points are tagged and can be locked in UI (`lock_island_supports`). Since M2.22 the overhang angle is a setting: surfaces steeper than it from horizontal get no overhang point, islands keep theirs. The default of 90 degrees keeps every overhang, `support_critical_angle` stays the stick junction angle. |

---

## Summary counts

| Status | Count |
|---|---|
| **covered** | 7 (rows 2, 12, 15, 16, 19, 20, 21) |
| **partial** | 9 (rows 3, 4, 5, 8, 11, 13, 14, 17, 18) |
| **missing** | 5 (rows 1, 6, 7, 9, 10) |

*Row 11 counted as partial because related configs exist.

---

## Numbered gap list (with roadmap todo mapping)

1. **Support preset bundles (Light/Medium/Heavy/Mini)** → **M2.18 presets** (closed: Light / Medium / Heavy, M2.22 adds Mini)
2. **Per-point tip diameter in config** (currently UI-only via selection) → **M2.12 per-point sizes**
3. **Tip length as distinct parameter** (separate from pinhead width) → **M2.13 tip geometry**
4. **Tip shape choice (cone vs. sphere)** → **M2.13 tip geometry**
5. **Knot/joint between tip and stem** → **M2.13 tip geometry**
6. **Stem cross-section geometry (round/square/polygon)** → **M2.16 stem geometry**
7. **Stem taper (variable diameter along length)** → **M2.16 stem geometry**
8. **Per-point "support on model" toggle** → **M2.12 per-point sizes** (or no todo yet)
9. **Base shape choice (cone vs. cylinder)** → **M2.14 rafts** (base is part of pillar, but raft todo may cover)
10. **Independent brace diameter** (separate from pillar diameter) → **M2.15 bracing** (closed: `support_brace_diameter`)
11. **Raft type selection (standard/skate/grid/honeycomb/none)** → **M2.14 rafts**
12. **Separate raft floor thickness** (distinct from wall thickness) → **M2.14 rafts**
13. **Minimum distance between auto-support points** → **M2.12 per-point sizes** (closed by M2.22: `support_points_minimal_distance`)
14. **Overhang angle threshold for auto-generation** → **M2.22 auto-support placement** (closed: `support_points_overhang_angle`)

---

## Uncertain items

- **Row 11 (Support on model)**: `support_max_weight_on_model` and `support_buildplate_only` exist but work differently from Lychee's per-point "start on model" concept. The branching algorithm decides algorithmically; user cannot force a specific pillar to start on model vs. raft.
- **Row 17/18 (Raft)**: Our "pad" is architecturally different from Lychee's "raft" (pad is a print-bed adhesion base with cavity; raft in Lychee is a disposable interface layer). The parity is approximate.
- **Row 21 (Overhang angle)**: the threshold is measured on the sampled overhang outline: the horizontal run of a sample is its distance to the layer below, the vertical run the layer height. A wall (no horizontal run) is 90 degrees, which the default keeps, so today's points are unchanged. `support_critical_angle` is a different knob, the stick junction angle.
