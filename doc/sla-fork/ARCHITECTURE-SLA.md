# PrusaSLA: where the SLA code is

A map of this fork's SLA code, for new contributors and for coding agents, so nobody has to
re-explore the tree to find the right file. It is written against the code as it is now: every path
below exists, and the type and function names are the ones in the files. Line numbers are
deliberately left out, because they go stale. For *what* the work should be, see
[PLAN.md](PLAN.md); for *what* still has to be done, see [ROADMAP.md](ROADMAP.md); for how to build
and run things, see [BUILD.md](BUILD.md).

Everything else in this file is a fact about the tree, not a rule. The rules are in
[AGENTS.md](../../AGENTS.md) and in section 8.

---

## 1. Layers

### The three layers

`src/slic3r-shared/README.md` is the authority: dependencies go **App → Biz → Domain**, never the
other way, and `App` stays platform-agnostic.

| Layer | Namespace | Where it lives |
|---|---|---|
| Domain | `Slic3r::Domain` | `src/slic3r-domain/` and, for most of the model, `libslic3r` (`src/libslic3r/`) |
| Biz | `Slic3r::Biz` | `src/slic3r-shared/src/Slic3r/Biz/`, plus the algorithm libs `src/slic3r-biz-*` |
| App | `Slic3r::App` | `src/slic3r-shared/src/Slic3r/App/`, plus `src/slic3r-app-cli/`, `src/slic3r-render/`, `src/slic3r-platform/` |

Library targets depend on each other in roughly this order (the authoritative statement is the
arrow chain in `src/slic3r-shared/README.md`): `src/slic3r-base` → `src/slic3r-render` →
`src/slic3r-platform` → `src/slic3r-domain` and the `src/slic3r-biz-*` algorithms → `libslic3r` →
`src/slic3r-shared` → the shells (`src/slic3r-app-desktop`, `src/slic3r-shared-wx`,
`src/slic3r-app-cli`).

### Where the SLA code sits in each layer

**Domain** — `src/slic3r-domain/`

- `include/Slic3r/Domain/SLA/SupportPoint.hpp` + `src/Slic3r/Domain/SLA/SupportPoint.cpp` —
  `Slic3r::Domain::SLA::SupportPoint`, `SupportPoints`, `PointsStatus`, `SupportPointType`
- `include/Slic3r/Domain/SLA/DrainHole.hpp` + `.cpp` — `DrainHole`, `DrainHoles`
- `include/Slic3r/Domain/SLA/PrintStatistics.hpp` — `PrintStatistics`
- `include/Slic3r/Domain/SLA/RaftPreset.hpp` + `.cpp` — `RaftInfill`, `RaftPadValues`
- `include/Slic3r/Domain/SlaLayerHeight.hpp` + `.cpp` — the `sla_effective_*` helpers and the raft interface
- `include/Slic3r/Domain/ConfigDefsSLA.hpp` + `src/Slic3r/Domain/ConfigDefsSLA.cpp` — every SLA
  option definition and the `sla::` enums (see section 2)
- `include/Slic3r/Domain/ConfigBoxesSLA.hpp` + `.cpp` — `SLAPrintSettings`, `SLAMaterialSettings`,
  `SLAPrinterSettings`, `SLAObjectSettings`
- `include/Slic3r/Domain/FullConfigSLA.hpp` + `.cpp` — `FullConfigSLA`, `PartialObjectConfigSLA`
- `include/Slic3r/Domain/Config.hpp` — `ConfigView`, `FullConfig`, `PartialConfig`, the key-based config reads
- `include/Slic3r/Domain/ModelObject.hpp` — carries `sla_support_points`, `sla_points_status`, `sla_drain_holes`
- `include/Slic3r/Domain/Preset/Types.hpp` — `PresetKind` with `SlaPrinter` / `SlaPrint` / `SlaToolPrint` / `SlaMaterial`

**Engine** — `src/libslic3r/`. Public headers live in `src/libslic3r/include/libslic3r/`,
implementation in `src/libslic3r/src/libslic3r/`. Almost all the SLA engine is **private**, so
including a `src/libslic3r/src/...` path from another library is a layering mistake.

- Public SLA headers: `PrintSteps.hpp` (`SLAPrintStep`, `SLAPrintObjectStep`), `IPrint.hpp`,
  `SLAResult.hpp` (hotspot), `SLASupportTool.hpp`, `SLA/LayerStats.hpp`, `SLA/RasterMemory.hpp`,
  `SLALayerImage.hpp`, `SLALayersToMesh.hpp`, `SlicingStatus.hpp`, `GeneratedSupportPoints.hpp`
- Private: `SLAPrint.hpp` / `.cpp`, `SLAPrintSteps.hpp` / `.cpp`, `SLASupportTool.cpp`,
  `ConfigViews.hpp` (`SLAPrintConfigView`, `SLAPrintObjectConfigView`), and everything under
  `src/libslic3r/src/libslic3r/SLA/` and `src/libslic3r/src/libslic3r/Format/`

**Biz** — `src/slic3r-shared/src/Slic3r/Biz/` (headers in `include/Slic3r/Biz/`)

- `Config/Legacy/PrintConfig.hpp` + `.cpp` — the legacy `PRINT_CONFIG_CLASS_DEFINE` SLA config
  classes (`SLAFullPrintConfig`, `SLAPrintConfig`, `SLAPrintObjectConfig`, `SLAMaterialConfig`,
  `SLAPrinterConfig`) and `handle_legacy_sla()`
- `Config/ConfigLoad.cpp`, `Config/3mf_legacy.hpp`, `Config/Legacy/3mf_legacy.cpp` — loading
  legacy SLA config and the old 3MF `Metadata/Slic3r_PE_sla_support_points.txt`
- `Preset/PresetInteractor.cpp`, `Preset/PresetSelectionCheck.cpp` — the `PresetKind::Sla*` switch
  onto the four config boxes
- `Preset/IO/BundleLoader.cpp`, `Preset/IO/BundlePaths.hpp` — bundle discovery (see section 2)
- `Slicing/SlicingInteractor.{hpp,cpp}`, `Slicing/BackgroundProcess.{hpp,cpp}`,
  `Slicing/AutoSlicing.hpp` — the slice driver
- `SLAObjectCache.{hpp,cpp}`, `SLAResultCache.{hpp,cpp}` — the two per-bed caches
- `Sla/DrainHoleSuggestion.{hpp,cpp}` — drain hole suggestion from a cavity issue
- `ResinProfile/` — the whole import subsystem (see section 6)
- `ResultExport/SLA/` — the archive writers and the format registry (see section 5)
- `PrintHost/PrintHostJobData.{hpp,cpp}`, `PrintHost/PrintHostFormats.{hpp,cpp}` — SLA upload rules
- `Scene/SceneInteractor.{hpp,cpp}` — `modify_sla_support_points`, `modify_sla_drain_holes`
- `ResinEconomics.{hpp,cpp}`, `ResinEconomicsInteractor.{hpp,cpp}` — ml, cost, bottles

**App** — `src/slic3r-shared/src/Slic3r/App/` (headers in `include/Slic3r/App/`). The fork's own
SLA files, all new and all small:

- `App/Plater/` — `SlaSupportPointsGizmo`, `SlaSupportPointsDialog`, `SlaSupportPointsEditing`,
  `SlaSupportGeometry`, `SlaSupportOnModel`, `SlaSupportPreviewService`, `SlaHollowGizmo`,
  `SlaHollowDialog`, `SlaDrainHolesEditing`, `SlaHeightBand`, `SlaHeightBandMeshes`,
  `SlaIssueAnalysis`, `SlaIssueNotification`, `SlaUnsupportedNotification`,
  `SlaUnsupportedObjects`, `SlaFirstModelNotification`
- `App/Preview/` — `SlaLayerImageWindow`, `SlaSupportsPanel`, `SidebarSlaSupports`,
  `SlaViewerWrapper`, `SlaViewerWrapperInputData`
- `App/ResultExport/SlaPreExportCheck.{hpp,cpp}`
- `App/` root — `SlaPrintSettingsDialog`, `SidebarSlaPrintSettings`, `SidebarSlaSummary`,
  `SlaIssueRows`, `SlaLayerJump`, `IsSlaActive.hpp` (header-only), `SlaRaftSettings.hpp`
  (header-only), `SlaFirstPreference`, `ResinImportDialog`, `ResinImportReport`,
  `ResinDatasheetDialog`, `FixtureRender.cpp`
- `App/Config/SlaSettingsRows.{hpp,cpp}` — the shared row list for the SLA settings dialogs
- `App/Hints/SlaHints.{hpp,cpp}` — the SLA hint list

**Platform and desktop shells**

- `src/slic3r-app-desktop/` — the wx desktop app. SLA-relevant: `DesktopApp.{hpp,cpp}`
  (`--sla-fixture`, `--render-to`), `MainFrameDropTarget.{hpp,cpp}` (resin profile drops),
  `SplashScreen.cpp`
- `src/slic3r-platform-wx/` — only `WXMainThreadDispatcher` and `WXRenderCanvas`; no SLA code
- `src/slic3r-platform/` — `App/Platform/CommandName.hpp` holds
  `CommandName::SlaSupportPointsGizmo` and `::SlaHollowGizmo`
- `src/slic3r-app-cli/` — `App/CLI/ProcessActions.cpp` has the `--export-sla`,
  `--import-resin-profile` and `--export-resin-profile` entry points;
  `App/CLI/LoadPrintData.cpp` resolves SLA preset slots

**The unbuilt legacy GUI** — `src/slic3r/` has no `CMakeLists.txt` anywhere and is not in
`src/CMakeLists.txt`, so none of it compiles. It is a porting reference only:
`src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.{hpp,cpp}`, `GLGizmoHollow.{hpp,cpp}`,
`GLGizmoSlaBase.{hpp,cpp}`, `Jobs/SLAImportJob.{hpp,cpp}`, `Jobs/SLAImportDialog.hpp`,
`Jobs/RotoptimizeJob.{hpp,cpp}`. Never add to it, and do not go looking for `ConfigDefsSLA.cpp`
there: it was moved out into `slic3r-domain`.

---

## 2. Config

### Definitions — `src/slic3r-domain/src/Slic3r/Domain/ConfigDefsSLA.cpp`

`get_defs_sla()` returns a function-local `ConfigDefinitions` whose init function is
`sla_config_init_fn`, which first calls `init_common_fdm_sla_config_items(defs, PrinterTechnology::SLA)`
and then defines about 185 SLA keys itself. There is no macro: keys are added with
`defs.add("name", typeid(T))` and then given a `location` (`SLAConfigLocation::{Printer, Material,
Print, Object}`), a `label`, an `option_group`, a `category`, a `tooltip` and a `gui_type`. Two
loops add families of keys: one walks `{"" , "branching"}` and adds `branchingsupport_*` and
`support_*` twice, once visible and once `Category::Hidden`; the other walks a four-entry support
preset table and adds `support_preset_{mini,light,medium,heavy}_{head_diameter,pillar_diameter,base_diameter,base_height}`.
The def types are `ConfigItemDef` and `ConfigDefinitions` in
`src/slic3r-domain/include/Slic3r/Domain/ConfigDef.hpp`.

Naming, in the spelling the code actually uses:

- `support_`, `support_preset_*`, and **`branchingsupport_*`** (no separator between the two words;
  a test pins that spelling, so do not "fix" it)
- `pad_`, `raft_` (`raft_type`, `raft_floor_thickness`, `raft_edge_taper`,
  `raft_infill`/`_spacing`/`_wall`/`_skin`, `raft_interface_thickness`, `raft_interface_exposure`)
- **`hollowing_`** (four keys: `_enable`, `_min_thickness`, `_quality`, `_closing_distance`)
- `bottom_` (eleven, the `bottom_*` twins of lift, retract, wait and `light_pwm`)
- `lift_height`, `lift_height_2`, `lift_speed`, `lift_speed_2`, `retract_speed`, `retract_speed_2`,
  `wait_before_lift`, `wait_after_lift`, `wait_after_retract`, `light_pwm`
- `resin_layer_height`, `resin_faded_layers`, `bottom_layer_count`, `faded_layers`, `layer_height`
- `sla_archive_format`, `sla_output_precision`, `elefant_foot_compensation`, `peel_*`, `vat_film_type`

Categories are `ConfigItemDef::Category`: the SLA-only ones are `Print_Pad` and `Print_Hollowing`,
next to `Print_Supports`, `Print_LayersSurfaces`, `Printer_General`,
`Filament_MaterialPrintingProfile`, `Filament_MaterialTemperatures`, `Filament_Notes`. There is no
`Category::SLA`. `Category::Hidden` is how a setting is kept but not shown; about 38 keys in
`get_defs_sla()` carry it. `ObservableCategorizer.cpp` and `CategoryUtils.cpp` are what filter on
it, and `App/Config/SlaSettingsRows.cpp` uses it for the SLA dialogs' own row lists.

`SlaRaftSettingsTests.cpp` is the test that keeps the raft group, the option groups and the
`Hidden` markers honest; `sla_invalidation_tests.cpp` keeps the `branchingsupport_*` spelling
honest.

### Reading config — key based, not accessors

There is no accessor-rich `SLAPrintConfig` in this fork. Values are read by string key through
`Slic3r::Domain::ConfigView::get<T>("key")`, which asserts on an unknown key, so tolerant readers
look the key up in `cfg.values()` first. The views the engine uses are
`SLAPrintConfigView` and `SLAPrintObjectConfigView` in
`src/libslic3r/src/libslic3r/ConfigViews.hpp`, built from `FullConfigSLA` and
`PartialObjectConfigSLA`. The legacy `SLAPrintConfig` / `SLAPrintObjectConfig` / `SLAMaterialConfig`
/ `SLAPrinterConfig` classes in `src/slic3r-shared/src/Slic3r/Biz/Config/Legacy/PrintConfig.hpp`
still exist for old presets and 3MF files; they are not what new code should use.

### Effective values

`src/slic3r-domain/include/Slic3r/Domain/SlaLayerHeight.hpp` +
`src/slic3r-domain/src/Slic3r/Domain/SlaLayerHeight.cpp`, in `Slic3r::Domain`:

- `sla_effective_layer_height(cfg)` — `resin_layer_height` when > 0, else `layer_height`
- `sla_effective_faded_layers(cfg)` — `resin_faded_layers` when >= 0, else `faded_layers`
- `sla_bottom_layer_count(cfg)` — `bottom_layer_count` when > 0, else the effective faded layers + 1
- `struct RaftInterface` with `first_layer`, `last_layer`, `exposure_s`, `count()`, `empty()`,
  `contains()`, `exposure_for()`, `layer_exposure_s()`, `print_time_delta_s()`
- `raft_interface_band(raft_height_mm, interface_thickness_mm, layer_height_mm)` and
  `sla_raft_interface(cfg, layer_count = -1)`

**Every** SLA layer-height read goes through `sla_effective_layer_height`, and every
`faded_layers` read through the effective faded layers. The fall-backs are what keep presets and
3MF files written before the resin preset owned those keys working. Callers: `SLAPrintSteps.cpp`,
`SLASupportTool.cpp`, the Anycubic / Goo / Ctb / SL1 writers,
`App/SlaPrintSettingsDialog.cpp`, `App/SidebarSlaPrintSettings.cpp`. Tests:
`src/slic3r-domain/test/Slic3r/Domain/SlaLayerHeightTests.cpp`.

### Raft

`src/slic3r-domain/include/Slic3r/Domain/SLA/RaftPreset.hpp` + `.cpp` is a value mapper, not a set
of named presets. `raft_preset_to_pad_values(sla::RaftType, ...)` maps the type onto
`RaftPadValues`; the types are `None`, `Full`, `AroundObject` and `Skate`, and the separate
`RaftInfillType` is `None`, `Grid`, `Honeycomb`. The same file owns which settings each type shows:
`raft_type_visible_settings`, `raft_type_uses_setting`, `raft_infill_visible_settings`,
`raft_infill_uses_setting`, `raft_visible_settings`, `raft_uses_setting`, `is_raft_setting`. The App
side is the header-only `App/SlaRaftSettings.hpp` (`raft_enum_of`, `selected_raft_type`,
`raft_setting_visible`).

The engine turns those values into a pad in `src/libslic3r/src/libslic3r/SLAPrint.cpp`:
`raft_values`, `raft_infill`, `raft_knob_mm`, `is_pad_enabled`, `is_pad_around_object`,
`is_zero_elevation`, `builtin_pad_cfg`, `make_pad_cfg`, `validate_pad`, `make_support_cfg`. The pad
mesher itself is `src/libslic3r/src/libslic3r/SLA/Pad.{hpp,cpp}`.

### Invalidation

The table is `const std::map<std::string, std::vector<Step>> invalidated_by`, an anonymous-namespace
constant in `src/libslic3r/src/libslic3r/SLAPrint.cpp`, with declarations in
`SLAPrint.hpp`. `Step` is `std::variant<SLAPrintStep, SLAPrintObjectStep>` from
`src/libslic3r/include/libslic3r/PrintSteps.hpp`. Read it through
`SLASlicingSync::get_invalidated_by_map()`; the propagation machinery next to it is `propagate()`,
`steps()`, `all_steps()` and `diff_to_invalidated_steps()`.

A key with **no** table entry makes `diff_to_invalidated_steps` fail, so a new config key needs an
entry in the same change that defines it. The test is
`tests/sla_print/sla_invalidation_tests.cpp`, which walks 193 keys, cross-checks the 18
`branchingsupport_*` keys against `get_defs_sla()`, and asserts a table of 39 representative keys.

### Presets and bundles

- `resources/presets/community-sla/` — the generic community bundle, version **1.0.4**.
  `CommunitySLA.idx` (change log), `CommunitySLA/vendor.yaml` (six printer templates, one tool,
  one sheet), `CommunitySLA/preset-sla-material.yaml` (two resins),
  `preset-sla-print.yaml` ("Standard supports" and "Fine supports"),
  `preset-sla-printer.yaml` (six `sla_printer` documents, each with `sla_archive_format`)
- `resources/presets/prusa-research-sla/` — the Prusa SL1/SL1S bundle: `PrusaResearchSLA.idx`,
  `PrusaResearchSLA/vendor.yaml`, one `preset-sla-{print,material,printer}.yaml`, about 141
  per-resin files, and an `assets/` folder with the printer models and thumbnails
- `resources/presets/RepositoryManifest.json` — the updater manifest; ids `prusa-research-fff`,
  `prusa-research-sla`, `community-sla`
- Bundles are discovered, not enumerated: `src/slic3r-shared/src/Slic3r/Biz/Preset/IO/BundleLoader.cpp`
  scans `resources/presets/<repo_id>/<vendor_id>/` and accepts a directory with a `vendor.yaml`.
  `BundlePaths.hpp` holds the three roots. The one hard-coded vendor list left is a temporary
  bootstrap in `PresetInteractor.cpp`.
- The yaml `kind` strings (`sla_printer`, `sla_print`, `sla_tool_print`, `sla_material`) are in
  `src/slic3r-shared/src/Slic3r/Biz/Preset/IO/PresetYamlDesc.hpp`
- The visible labels "Resin" and "Supports & raft" are runtime i18n strings, chosen by
  `is_sla_active()`: `App/SidebarPrint.cpp`, `App/PrintSettingsDialog.cpp`,
  `App/MaterialSettingsDialog.cpp`, `App/SlaPrintSettingsDialog.cpp`

---

## 3. Supports

### The point

`Slic3r::Domain::SLA::SupportPoint` in
`src/slic3r-domain/include/Slic3r/Domain/SLA/SupportPoint.hpp` is the one support point type in
the fork; the engine uses it directly, there is no `PrintPoint` copy. Fields: `pos`, `head_front_radius`,
`type`, `pillar_diameter`, `base_diameter`, `base_height`, `base_shape`, `tip_shape`, `tip_length`,
`contact_depth`, `stem_sides`, `stem_taper`, `knot_radius`, `on_model`. Nested enums
`BaseShape {Default, Cone, Cylinder, Flat}`, `TipShape {Default, Cone, Ball}` and
`OnModel {Inherit, Allow, Forbid}`. `SupportPoints` is a plain `std::vector<SupportPoint>`.
Points are stored per object in `ModelObject::sla_support_points` with
`ModelObject::sla_points_status`, and come back from a slice in
`Slic3r::Biz::Slicing::Sla::Object::support_points`, transformed by
`SLAPrintObject::transformed_support_points()`.

Do not confuse these with the FFF `SupportSpotsGenerator::SupportPoint` in
`src/libslic3r/src/libslic3r/SupportSpotsGenerator.hpp` or with the cache DTOs in
`src/libslic3r/include/libslic3r/GeneratedSupportPoints.hpp`.

### The tool

- `App/Plater/SlaSupportPointsGizmo.{hpp,cpp}` — `Slic3r::App::Plater::SlaSupportPointsGizmo`,
  an `Scene::IToolGizmo` plus `ISceneSelectionChangedListener` and
  `ISLAObjectCacheChangedListener`. It owns the dialog through `Yoga::Passthrough`, does the
  raycasting and glyphs, runs generation on a `Biz::JThread`, and holds the rectangle selection.
  Public auto-support API: `auto_support(object_ids)`, `auto_support_running()`
- `App/Plater/SlaSupportPointsDialog.{hpp,cpp}` — `SlaSupportPointsDialog : GizmoWindow` with a
  `Callbacks` struct (generate, apply, discard, the per-field change slots, the four preset slots,
  `auto_support_all`, the clipping and island-lock slots)
- `App/Plater/SlaSupportPointsEditing.{hpp,cpp}` — `SlaSupportPointsEditing`, the UI-free edit
  buffer. The tests test this, not a copy of it
- `App/Plater/SlaSupportGeometry.{hpp,cpp}` — `SlaSupportGeometry`, `SupportGeometryField`,
  `support_geometry_of`, `apply_support_geometry`, `selection_support_geometry`
- `App/Plater/SlaSupportOnModel.{hpp,cpp}` — `using SupportOnModel = Domain::SLA::SupportPoint::OnModel`
  and the mixed-value helper
- Registration: `App/Plater/ToolGizmosUiInfo.cpp` (name, shortcut `P`, icon, SLA-only visibility),
  `App/Plater/PlaterRenderModule.cpp` (hotspot: creates the gizmo, the dialog and the toolbar item),
  `App/Scene/IGizmo.hpp` (hotspot: `ToolType::SlaSupportPoints`)
- `App/Preview/SlaSupportsPanel.{hpp,cpp}` — `SlaSupportsStatus {NoModels, NeedsSupports,
  Generating, ReadyToSlice}`, `SlaSupportsPanelState`, the pure `sla_supports_panel_state()`
- `App/Preview/SidebarSlaSupports.{hpp,cpp}` — the Preview block with "Auto support selected",
  "Auto support all" and "Edit supports". It navigates to Prepare and asks
  `Navigator::run_sla_auto_support()`; it does not slice

### The engine call the tool makes, and why nothing slices

`src/libslic3r/include/libslic3r/SLASupportTool.hpp` +
`src/libslic3r/src/libslic3r/SLASupportTool.cpp`, namespace `Slic3r::sla`:

- `using SupportToolStop = std::function<bool()>`
- `struct SupportToolTree { tree; pad; }` (both nullable)
- `build_support_tree_for_tool(object, object_to_world, points, full, object_cfg, stop)` — builds a
  world-frame mesh, makes a `sla::SupportableMesh`, calls `sla::create_support_tree` and, when the
  raft asks, `sla::create_pad` + `validate_pad`
- `generate_support_points_for_tool(object, object_to_world, full, object_cfg, stop)` — slices the
  merged mesh at its own heights, runs `sla::prepare_generator_data`,
  `sla::generate_support_points`, `sla::move_on_mesh_surface`, applies the zero-elevation filter,
  and returns points in the object's mesh frame
- `support_tool_elevation(full, object_cfg)` — the same elevation rule as
  `SLAPrintObject::get_elevation()`

This path deliberately re-implements only part of the pipeline: it does **not** do
`slaposAssembly`, `slaposHollowing`, `slaposDrillHoles`, the CSG slice, permanent point carry-over,
blocker/enforcer filtering, `slaposSliceSupports` or any result reporting. That is why the support
tool and the tree preview work without slicing anything.

`App/Plater/SlaSupportPreviewService.{hpp,cpp}` is the always-on tree preview in the Prepare view:
it computes `sla::build_support_tree_for_tool` on its own worker, diffs work with
`SlaSupportPreviewKey` (`hash_support_points()` over the tree-relevant fields), draws
`AuxiliaryElementId{Type::SlaSupports, object_id}` and `{Type::SlaPad, ...}` nodes, and lifts the
model through `PlaterScenePresenter::set_sla_lift()` so the plate and the tool agree.

### Trees, mesher, generator

Under `src/libslic3r/src/libslic3r/SLA/`:

- `SupportTree.{hpp,cpp}` — `SupportTreeConfig` (with `head_fullwidth()`, `safety_distance()`),
  `SupportableMesh`, `MeshType {Support, Pad}`, `create_support_tree()`, `create_pad()`,
  `slice()`, `ground_level()`, `brace_radius()`, `brace_start_z()`
- `DefaultSupportTree.{hpp,cpp}` — `class DefaultSupportTree` and `create_default_tree()`:
  `add_pinheads`, `classify`, `routing_to_ground`, `routing_to_model`, `interconnect_pillars`,
  `merge_result`, `static execute`
- `BranchingTreeSLA.{hpp,cpp}` — `create_branching_tree()`; the builder is
  `BranchingTreeBuilder`, defined in the `.cpp`, on top of
  `src/libslic3r/src/libslic3r/BranchingTree/BranchingTree.hpp` (`branchingtree::Builder`,
  `build_tree`, `sample_mesh`) and `PointCloud.hpp`
- `SupportTreeBuilder.{hpp,cpp}` — `SupportTreeNode`, `Head`, `Pillar`, `Pedestal`, `Junction`,
  `Bridge`, `DiffBridge`, `Anchor`, `StemGeometry`, and `class SupportTreeBuilder`
  (`add_head`, `add_pillar`, `add_pillar_base`, `add_bridge`, `merge_and_cleanup`,
  `retrieve_mesh(MeshType)`)
- `SupportTreeMesher.{hpp,cpp}` — the mesher free functions: `sphere`, `cylinder`, `pinhead`,
  `halfcone`, `polygon_cone`, `get_mesh(Head|Pillar|Pedestal|Junction|Bridge|DiffBridge, steps)`
- `SupportTreeStrategies.hpp` — `SupportTreeType {Default, Branching, Organic}`,
  `PillarConnectionMode {zigzag, cross, dynamic}`
- `SupportPointGenerator.{hpp,cpp}` — `prepare_generator_data`, `generate_support_points`,
  `move_on_mesh_surface`, `SupportPointGeneratorConfig`, `LayerSupportPoint`
- `SupportIslands/` — `SupportIslandPoint.hpp` and its subclasses, `UniformSupportIsland.{hpp,cpp}`,
  `SampleConfig.hpp`, `SampleConfigFactory.{hpp,cpp}`
- `SupportTreeUtils.hpp`, `SupportTreeUtilsLegacy.hpp` — the shared geometry helpers
  (`head_back_radius`, `head_penetration`, `base_shape_of`, `stem_geometry`, `may_rest_on_model`,
  `calculate_pinhead_placement`, `build_ground_connection`)

### Auto support and placement

`Generate` and `Auto support all` both go to `generate_support_points_for_tool`; `Auto support all`
goes through the whole bed one object at a time, and the result is staged until Apply. The
placement keys, defined in `ConfigDefsSLA.cpp` and read in `SLASupportTool.cpp` and
`SLAPrintSteps.cpp`, are `support_points_density_relative`, `support_points_minimal_distance`,
`support_points_overhang_angle` and `support_enforcers_only`. The density slider writes straight to
the object override `object_settings_sla`. The per-point shape keys (`support_tip_shape`,
`support_tip_length`, `support_knot_diameter`, `support_stem_sides`, `support_stem_taper`,
`support_base_shape`) invalidate nothing: they ride on the point, not on the config.

`App/Plater/SlaHeightBand.{hpp,cpp}` is the Chitubox-style height band (it owns a
`Scene::ClipperPresenter` and a `Scene::HeightBand`, and is suspended when a tool gizmo takes the
clipper); `App/Plater/SlaHeightBandMeshes.{hpp,cpp}` collects the meshes it cuts.
`App/Plater/SlaUnsupportedObjects.{hpp,cpp}` is the "sliced with no support structure" rule, shared
with the pre-export check; `SlaUnsupportedNotification.{hpp,cpp}` shows it.

### Support painting

The facets the user painted on the model (`ModelVolume::supported_facets`, the same field the FFF
`PaintOnSupportsGizmo` writes) are honoured by the auto support generator. The new
`SLA/SupportFacetPaint.hpp` and `.cpp` (namespace `Slic3r::sla`) hold `SupportFacetPaint` (the enforcer
and blocker regions of every layer) and `support_facet_paint(parts, object_to_world, heights,
cancel)`, which reads the painted facets of a `SupportToolModelMesh` (each part carries the
`TriangleSplittingData` of its volume) and projects them into the slabs of the layers with
`slice_mesh_slabs`, the way `PrintObject::project_and_append_custom_facets` does it for FFF.
`SupportPointGeneratorData` carries the paint: `prepare_generator_data` drops the overhang samples of
a blocked layer, and `generate_support_points` samples an enforced region with the island sampler and
the configured density. The paint is built by `generate_support_points_for_tool` and by the
`prepare_for_generate_supports` step, so the tool and the slice time agree. A facet that has no area
in a layer (a vertical wall) paints nothing in that layer, as in the FFF path.

Parity checklists against the legacy gizmos: `doc/sla-fork/parity/support-points.md` and
`doc/sla-fork/parity/hollow.md`.

---

## 4. The slicing pipeline

### `SLAPrint::Steps`

`src/libslic3r/src/libslic3r/SLAPrintSteps.hpp` + `.cpp` hold `class SLAPrint::Steps`.
`SLAPrintObject::process()` in `SLAPrint.cpp` runs the object steps in two levels, then the print
steps. The step enums are in `src/libslic3r/include/libslic3r/PrintSteps.hpp`.

Object steps, in order:

| Enum | Method | What it does |
|---|---|---|
| `slaposAssembly` | `mesh_assembly` | CSG-union the parts, make the preview mesh |
| `slaposHollowing` | `hollow_model` | `sla::generate_interior` and add the interior as a CSG difference |
| `slaposDrillHoles` | `drill_holes` | drill the drain holes into the CSG, then free the hollowing data |
| `slaposObjectSlice` | `slice_model` | build the layer grid, `slice_csgmesh_ex`, apply printer corrections, prepare blocker/enforcer volumes |
| `slaposSupportPoints` | `support_points` | island sampling, permanent points, modifier filtering |
| `slaposSupportTree` | `support_tree` | `sla::create_support_tree` |
| `slaposPad` | `generate_pad` | `sla::create_pad` + `validate_pad`; fails with `ErrorCode::NoPadGenerated` |
| `slaposSliceSupports` | `slice_supports` | `sla::slice` of the tree and pad onto the layer grid |

Print steps: `slapsMergeSlicesAndEval` (`merge_slices_and_eval_stats`, where the islands, the
cavities, the layer areas, the peel force and the statistics are computed) and `slapsRasterize`
(`rasterize`, windowed by the raster batch size). `label()` and `progressrange()` give the progress
text and the 0–70 / 70–100 split.

### Only the Slice button slices

The rule and its single enforcement point are
`src/slic3r-shared/include/Slic3r/Biz/Slicing/AutoSlicing.hpp`:
`is_auto_slicing_allowed(technology, requested)` is false for SLA. `SlicingInteractor::process_slicing_queue()`
checks it, so no caller can start an SLA slice by itself. The auto-reslice toggle is hidden for SLA
in `App/Preview/SidebarAutoReslice.cpp`, and `PreviewRenderModule::update_auto_slicing()` goes
through the same predicate.

The slice entry points that are allowed: the Prepare action bar
(`App/Plater/SidebarPlaterActionButtons.cpp`), the Preview action bar
(`App/Preview/SidebarPreviewActionButtons.cpp`), the object list's slice cell
(`App/ObjectList.cpp`), the CLI (`App/CLI/ProcessActions.cpp`), and `--sla-fixture`
(`DesktopApp.cpp`). The only *partial* slices are user-driven and requested with
`SliceUntilStep`: the hollow tool's Preview button (`SlaHollowGizmo.cpp`, up to `slaposHollowing`)
and the FFF paint-on-supports gizmo.

`SlicingInteractor` holds one `BackgroundProcess` per bed, each with a slicing thread and a helper
thread; results and status hop to the main thread through
`PlatformServices::instance().main_thread_dispatcher()`. `IPrint::stop_token` is the cancel handle
(`PrintBase.hpp` turns it into `canceled()` / `throw_if_canceled()`). A failure becomes a status
with a message, not a crash (M6.8).

Two caches carry the results out, both keyed by `Domain::SlicingId`:
`Biz/SLAObjectCache.{hpp,cpp}` holds the per-object preview (`Sla::Object`: `mesh`,
`support_structure`, `pad`, `support_points`, `object_trafo`, `issues`), and
`Biz/SLAResultCache.{hpp,cpp}` holds the per-bed `SLAResult`. Its
`Slic3r::Biz::Slicing::Sla::ResultType` (`None`, `Slices`, `Files`) decides whether an entry is
replaced or only its `export_data->files` extended.

### Raft, hollowing, drain holes

- Raft geometry: `src/libslic3r/src/libslic3r/SLA/Pad.{hpp,cpp}` — `PadConfig` (`wall_thickness_mm`,
  `floor_thickness_mm`, `wall_height_mm`, `wall_slope`, `brim_size_mm`, `edge_taper_mm`, and the
  nested `Infill` and `EmbedObject`), `PadSkeleton`, `AroundPadSkeleton`, `BrimPadSkeleton`,
  `BelowPadSkeleton`, `PadInfill`, `pad_blueprint`, `create_pad`
- Hollowing: `src/libslic3r/src/libslic3r/SLA/Hollowing.{hpp,cpp}` — `HollowingConfig`, `Interior`,
  `generate_interior`, `hollow_mesh` (four overloads), `hollow_mesh_and_drill` returning
  `HollowMeshResult::{Ok, FaultyMesh, FaultyHoles, DrillingFailed}`, `cut_drainholes`
- Drain holes: `src/slic3r-domain/include/Slic3r/Domain/SLA/DrainHole.hpp` (`DrainHole` with `pos`,
  `normal`, `radius`, `height`, `failed`), stored in `ModelObject::sla_drain_holes`, transformed by
  `SLAPrintObject::transformed_drainhole_points()`
- UI: `App/Plater/SlaHollowGizmo.{hpp,cpp}` (also holds `Biz::SlaHollowRequest`, the
  `SliceUntilStep` state machine), `App/Plater/SlaHollowDialog.{hpp,cpp}`,
  `App/Plater/SlaDrainHolesEditing.{hpp,cpp}`
- Suggestion: `src/slic3r-shared/include/Slic3r/Biz/Sla/DrainHoleSuggestion.hpp` —
  `DrainHoleSuggestionOptions`, `DrainHoleCandidate`, `DrainHoleSuggestion`,
  `suggest_drain_hole`, `suggest_nearest_drain_hole`, `accepts_drain_hole_suggestion`. A cup is
  drilled up through its roof, trapped resin down through its floor. The UI entry is
  `App/SidebarSlaSummary.cpp` (`suggest_drain_hole`, `add_suggested_drain_hole`), which marks the
  bed modified and lets the user re-slice

### Rasterizing, and the RasterMemory batches

- `src/libslic3r/src/libslic3r/SLA/RasterBase.{hpp,cpp}` — `class ISlaRasterizer` (`create_file`,
  `raw_raster_bytes`), `EncodedRaster`, `Resolution`, `PixelDim`, `RasterEncoder`, `class RasterBase`
  (orientation, mirroring, `Trafo`, `draw`, `encode`), `PNGRasterEncoder`, `PPMRasterEncoder`,
  `create_raster_grayscale_aa`
- `src/libslic3r/src/libslic3r/SLA/AGGRaster.hpp` — `AGGRaster`, `RasterGrayscaleAA`,
  `RasterGrayscaleAAGammaPower`
- `src/libslic3r/src/libslic3r/SLA/RasterToPolygons.{hpp,cpp}` — `raster_to_polygons`
- The **batching** is `src/libslic3r/include/libslic3r/SLA/RasterMemory.hpp` +
  `src/libslic3r/src/libslic3r/SLA/RasterMemory.cpp` (public on purpose, so App and Biz tests can
  use it): `raw_raster_bytes_per_pixel()` (1), `raw_raster_byte_budget()` (512 MB, the one knob),
  `raw_raster_batch_size(bytes_per_layer, layer_count, max_parallel)`, `live_raw_rasters()`,
  `peak_live_raw_rasters()`, `reset_peak_live_raw_rasters()`, `class RawRasterGuard`
- The consumer is `SLAPrint::Steps::rasterize()`: it asks for a rasterizer from
  `get_output_type()`, then walks the print in windows of `raw_raster_batch_size`, encoding one
  file per layer. `AGGRaster` holds a `RawRasterGuard`, so the peak count is measured rather than
  assumed. Analysis: `doc/sla-fork/profiling/raster-memory.md`
- Layer images for the UI: `src/libslic3r/include/libslic3r/SLALayerImage.hpp` +
  `SLALayerImage.cpp` (`SlaLayerImage`, `render_sla_layer_image`,
  `render_sla_layer_image_region`) and `SLALayersToMesh.{hpp,cpp}` (`GrayLayerImage`,
  `LayersToMeshParams`, `layers_to_mesh`)

### Result data and issues

`src/libslic3r/include/libslic3r/SLAResult.hpp` (hotspot) is the whole result contract, in
`Slic3r::Biz::Slicing`:

- `Sla::FileData`, `Sla::FilesData`, `Sla::FileDataType {sl1_png, sl1_svg, anycubic, goo, pm5, ctb,
  other}`, `Sla::OutputFiles`
- `Sla::SlaIssue` with `Kind {Island, Cup, TrappedResin, HighPeelForce, Other}`, `layer`,
  `object_id`, `object_name`, `position`, `note`
- `Sla::ResultType {None, Slices, Files}`, `Sla::ObjectIssueType` and `is_critical()`
- `Sla::Object` (the per-object preview: `object_id`, `instance_trafos`, `mesh`, `object_trafo`,
  `support_structure`, `pad`, `support_points`, `issues`)
- `SLAResultData` (`serialized_config`, `config`, `print_statistics`, `files`, `thumbnails`,
  `layer_areas`, `layer_peel_force`, `issues`, `project_name`) and `SLAResult` (`export_data`,
  `slices`, `heights`, `type`, `contained_in_bed`)

The detectors, all under `src/libslic3r/src/libslic3r/SLA/`:

- `IslandDetection.{hpp,cpp}` — `detect_islands`, `attribute_island`, `IslandHit`, `IslandOwner`
- `CavityDetection.{hpp,cpp}` — `detect_cavities`, `CavityAnalysis`, `CavityDetectionOptions`,
  `CupHit`, `TrappedResinHit`
- `LayerStats.{hpp,cpp}` (header is public) — `layer_areas_mm2`, `peel_force_estimate`,
  `peel_force_coefficients`, `peel_force_warning_n`, `layers_over_peel_force`,
  `PeelForceSettings`. Analysis: `doc/sla-fork/profiling/peel-force.md`

Everything above is filled in `merge_slices_and_eval_stats()`, converted into `Sla::SlaIssue`
entries (the size goes in `note`, e.g. `"island, 4.20 mm2"`) and handed out as
`SLAResult{type = Slices}`.

Where the UI shows them:

- **Prepare sidebar**: `App/SlaIssueRows.{hpp,cpp}` (`build_sla_issue_rows`, `sla_issue_row_text`,
  `sla_issue_area_mm2`, `sla_issue_volume_mm3`, `sla_issue_peel_force_n`) feeding
  `App/SidebarSlaSummary.cpp`, next to the resin figures from
  `Biz/ResinEconomics.cpp`
- **Notifications**: `App/Plater/SlaIssueNotification.{hpp,cpp}` and
  `App/Plater/SlaUnsupportedNotification.{hpp,cpp}`, both driven by
  `App/Plater/SlaIssueAnalysis.{hpp,cpp}` (`analyze_sla_issues_for_notification`)
- **Preview layer inspector**: `App/Preview/SlaLayerImageWindow.{hpp,cpp}` — the layer image at
  fit and at 1:1, the area and peel-force charts (`render_plot`, `peel_warning_n`), the island
  list (`rebuild_island_list`, `island_row_text`, prev/next island), and `go_to_layer`. The
  sidebar and the Preview module do not know each other, so a click posts on the header-only
  `App/SlaLayerJump.hpp` channel and the layer window takes it
- **Pre-export**: `App/ResultExport/SlaPreExportCheck.{hpp,cpp}`

---

## 5. Export

### The registry

`src/slic3r-shared/include/Slic3r/Biz/ResultExport/SLA/SlaArchiveFormat.hpp` + `.cpp`, namespace
`Slic3r::Biz::PrintHost::Sla`. `class ISlaArchiveFormat` has `name()`, `description()`,
`extensions()`, `file_data_type()` and `store(path, SLAResultData)`.
`class SlaArchiveFormatRegistry` is a singleton with `register_format`, `get`, `names`,
`find_by_extension` and `find_by_file_data_type`. The six built-in format classes and the
`register_sla_archive_formats()` entry point (idempotent, `std::call_once`) are in
`.../SLA/SL1Format.cpp`.

| Name | Extensions | `FileDataType` | Writer |
|---|---|---|---|
| `SL1` | `sl1`, `sl1s`, `zip` | `sl1_png` | `store_sl1` |
| `SL1_SVG` | `sl1svg` | `sl1_svg` | `store_sl1` |
| `Anycubic` | `pwmo`, `pwmx`, `pwms` | `anycubic` | `store_anycubic` |
| `PM5` | `pm5` | `pm5` | `store_pm5` |
| `Goo` | `goo` | `goo` | `store_goo` |
| `CTB` | `ctb` | `ctb` | `store_ctb` |

`src/slic3r-shared/src/Slic3r/Biz/ResultExport/SLA/SlaExportFileTypes.{hpp,cpp}` turns the
registry into the file-dialog list: `sla_export_file_types`, `sla_default_export_extension`,
`sla_extension_matches_format`, `is_sla_export_extension`. The rule behind it: **the layers are
encoded in the printer's format at slice time**, so only the extensions sharing that
`FileDataType` can be written without re-slicing.

### The writers

- `.../SLA/SL1.{hpp,cpp}` — `store_sl1`, the only zip writer: `config.ini`, `config.json`,
  `prusaslicer.ini`, `prusaslicer.json`, numbered layer PNG or SVG files, thumbnails
- `.../SLA/Zipper.{hpp,cpp}` — the streaming zip writer behind it (`FAST_COMPRESSION` for PNG,
  `TIGHT_COMPRESSION` for SVG)
- `.../SLA/AnycubicSLA.{hpp,cpp}` — `store_anycubic` (raw binary, `ANYCUBIC` intro, per-layer
  exposure and lift, so the raft interface lands per layer) and `store_pm5`
  (`PM5_FORMAT_VERSION = 517`, pw0 RLE layers, 16-level colour table)
- `.../SLA/GooSLA.{hpp,cpp}` — `store_goo`, the Elegoo container, big-endian header plus previews
- `.../SLA/CtbSLA.{hpp,cpp}` — `store_ctb`, Chitubox CTB version 3, unencrypted only
- `.../SLA/SlaAntiAliasing.{hpp,cpp}` — `sla_raster_anti_aliased(cfg)`: one rule, driven by
  `gamma_correction`. Zero thresholds the raster, anything above zero anti-aliases it, and the
  header's AA field follows the same setting. Analysis: `doc/sla-fork/profiling/aa-and-z-correction.md`

The engine-side rasterizers that produce the data these writers wrap live in
`src/libslic3r/src/libslic3r/Format/`: `SL1.{hpp,cpp}`, `SL1_SVG.{hpp,cpp}`,
`AnycubicSLA.{hpp,cpp}`, `GooSLA.{hpp,cpp}`, `CtbSLA.{hpp,cpp}`, plus the now-superseded
`SLAArchiveWriter.hpp` (`sla::ISlaStore`). `SLAPrint::Steps::rasterize()` picks one from the
printer's `sla_archive_format`; `sla_output_precision` is read only by the SVG rasterizer.

Format notes: `doc/sla-fork/formats/ctb.md`, `pm5.md`, `orientation.md`, and
`doc/sla-fork/orientation-test.md` for the printable calibration piece
(`doc/sla-fork/tools/orientation_test_piece.py`).

### Reading a sliced archive back

`.../SLA/SL1Import.{hpp,cpp}` — `import_sl1_archive(path, stop)` returning
`tl::expected<Sl1ImportResult, std::string>` with a rebuilt `Domain::TriangleMesh` and the
embedded profile. It is called from `src/slic3r-shared/src/Slic3r/Biz/FileLoadingLogic.cpp`
(`is_sl1_archive`). Zip and ini helpers are in `src/slic3r-shared/include/Slic3r/Biz/ArchiveIni.hpp`
(`ZipReader`, `parse_archive_ini`, `get_ini_value`).

### Test helpers

`src/slic3r-shared/test/Slic3r/Biz/ResultExport/SLA/SlaLayerDecoders.hpp`, namespace
`Slic3r::Test::Sla`. Nothing in the product reads `.ctb`, `.goo`, `.pwmx` or `.pm5` back, so the
RLE schemes are re-implemented here for the tests: `decode_ctb_layer`, `decode_goo_layer`,
`decode_pw0_layer`, `decode_png_layer`, plus `distinct_greys`, `on_quantization_grid`,
`placement_of_pixels`, `placement_of_svg`, `read_svg_numbers`, and the structs `DecodedLayer` and
`LayerPlacement`. Used by `CtbExportTests`, `GooExportTests`, `AnycubicExportTests`,
`PM5ExportTests`, `SlaAntiAliasingTests`, `SlaArchiveOrientationTests`,
`SlaRasterOrientationTests`, `SlaPm5WorkshopOrientationTests`.

### Before and around the export

- Pre-export check: `src/slic3r-shared/include/Slic3r/App/ResultExport/SlaPreExportCheck.hpp` +
  `.cpp` — `Problems`, `format_problems`, `collect(project_interactor)`, `confirm`. It reports
  only two things: models sliced with no support structure, and islands. Other issue kinds have no
  pre-export meaning today
- Trigger: `src/slic3r-shared/src/Slic3r/App/ResultExport/ExportActions.cpp` — `can_export`,
  `export_gcode`, `export_gcode_to_flash`, `send_gcode_to_connect`, `upload_gcode_to_print_host`.
  The confirm dialog is inside the internal `make_export` helper, before the file dialog
- File name and extension: `src/slic3r-shared/src/Slic3r/Biz/ResultExport/ExportNameParser.{hpp,cpp}`
  (`parse_sla_export_name`, `ExportNameData`) and
  `App/ResultExport/ExportPathSelect.{hpp,cpp}` (`sla_archive_format`, `sla_wildcards`,
  `get_export_name_data`, `show_export_modal_dialog`, `show_upload_modal_dialog`,
  `warn_sla_format_mismatch`)
- Writing: `src/slic3r-shared/src/Slic3r/Biz/ResultExport/ResultExportDataFinalizer.{hpp,cpp}`
  (`process_sla_result` asks the registry for the writer by `FileDataType`) and
  `ResultExportInteractor.{hpp,cpp}`

### Destinations and print hosts

The destination is a variant, not a switch: `PhysicalPrinter::FileSystemExport` (a plain file or
a removable drive) versus `PhysicalPrinter::PrinterUpload` / `ConnectUpload`, resolved in
`src/slic3r-shared/src/Slic3r/Biz/PrintHost/PrintHostFactory.cpp`.

- `src/slic3r-shared/src/Slic3r/Biz/PrintHost/PrintHostJobData.{hpp,cpp}` —
  `PrintHostExportFormat {Undefined, GCode, BGCode, Sl1, Sl1s, SlaArchive}`,
  `DataPtrVariant` (which includes the `SLAResultData` branch),
  `get_export_format_from_extension`, `get_post_upload_actions` (an SLA host such as
  `PrintHostSL1Host` gets **no** "print now" action)
- `src/slic3r-shared/src/Slic3r/Biz/PrintHost/PrintHostFormats.{hpp,cpp}` —
  `print_host_accepts_extension`, `destination_accepts_extension`,
  `destination_rejects_extension_message`. PrusaLink and Prusa Connect take `sl1`/`sl1s`; the
  FFF hosts take only G-Code
- `src/slic3r-shared/src/Slic3r/Biz/RemovableDrive/` — the drive monitor and eject service; it only
  changes the dialog's default folder
- `src/slic3r-shared/src/Slic3r/Biz/ProjectInteractor.cpp` — `do_result_export`,
  `do_result_export_inner` (refuses an incomplete plate), `do_result_upload`,
  `do_result_upload_connect`, `output_dir`

---

## 6. Resin import

All under `src/slic3r-shared/`, namespace `Slic3r::Biz::ResinProfile` for the logic and
`Slic3r::App` for the dialogs.

- Readers: `include/Slic3r/Biz/ResinProfile/IResinProfileReader.hpp` (`format_id`, `sniff`,
  `sniff_path`, `reads_container_lazily`, `read`),
  `include/Slic3r/Biz/ResinProfile/ResinProfileReaderRegistry.hpp` + `src/.../ResinProfileReaderRegistry.cpp`
  (`register_reader`, `read_file`, `MAX_FILE_SIZE`, `SNIFF_BYTES`, `register_resin_profile_readers`)
- Readers: `ChituboxCfgReader.{hpp,cpp}` (the `.cfg` text format, detected by content because the
  extension is generic) and `SlicedArchiveResinReader.{hpp,cpp}` (`.sl1` / `.sl1s`, reads only
  `config.ini` and `prusaslicer.ini`, never the layers)
- The neutral struct: `include/Slic3r/Biz/ResinProfile/ForeignResinProfile.hpp` —
  `ForeignResinProfile` (`source_format`, `source_path`, `printer_hint`, `material`, `raw_values`,
  `warnings`) and `ResinMaterialSettings`, whose fields are all `std::optional` and carry their unit
  in the name
- Mapper: `include/Slic3r/Biz/ResinProfile/ResinProfileMapper.hpp` + `.cpp` — `MappingStatus
  {Exact, Converted, Approximated, NotApplicable, Unknown}`, `MappedField`,
  `TargetPrinterClass {Tilt, GenericMsla}`, `MappingResult`, `map_resin_profile`, and
  `printer_class_from_config`. The rules are one ordered table (`mapping_table()`); a
  tilt-capable printer and a generic MSLA printer get different targets, so bottom layer count
  becomes `resin_faded_layers` on one and `bottom_layer_count` on the other, and only the generic
  side gets the layer-separation keys. Anything the table did not consume is reported as
  `Unknown` with the note "Not a PrusaSLA resin setting; kept in the report only."
- Interactor: `include/Slic3r/Biz/ResinProfile/ResinProfileImportInteractor.hpp` + `.cpp` —
  `ResinImportTarget`, `ResinImportResult`, `import_file`, `import_profile`, `import_folder`,
  `system_resin_presets`, `default_registry()`. The flow: check the target, copy the identity
  fields, pick a base resin, choose the mapping class, map, name uniquely, select the base,
  write the values onto the material, set `material_source_note`, then
  `save_selected_preset_as(PresetKind::SlaMaterial, ...)`. `dry_run` stops after the mapping
- Report: `include/Slic3r/Biz/ResinProfile/ResinImportReport.{hpp,cpp}` —
  `resin_import_report_json(results)`
- Datasheet: `include/Slic3r/Biz/ResinProfile/ResinDatasheetForm.{hpp,cpp}` — `ResinDatasheet`,
  `validate_datasheet`, `datasheet_to_profile`. It produces a `ForeignResinProfile` with
  `source_format = SOURCE_FORMAT_DATASHEET`, so a typed-in resin takes the same path as a file
- Reverse export: `include/Slic3r/Biz/ResinProfile/ChituboxCfgExport.{hpp,cpp}` —
  `ExportedResinKey`, `export_chitubox_cfg_report`, `export_chitubox_cfg`, `export_printer_class`
- Dialogs: `App/ResinImportDialog.{hpp,cpp}` (the review table with per-field badges),
  `App/ResinImportReport.{hpp,cpp}` (`MappingBadge`, `MappingRow`, `BadgeCounts`,
  `build_mapping_rows`, `summary_line`, `validate_preset_name`),
  `App/ResinDatasheetDialog.{hpp,cpp}`
- Entry points: `App/MaterialSelectionDialog.cpp` (the two buttons and the file dialog),
  `App/Navigator.{hpp,cpp}` (`open_resin_import`), `App/Plater/PlaterRenderModule.cpp` and
  `App/Preview/PreviewRenderModule.cpp` (the same route per module),
  `src/slic3r-app-desktop/src/Slic3r/App/Desktop/MainFrameDropTarget.cpp` (drag and drop), and
  `src/slic3r-shared/src/Slic3r/Biz/FileLoadingLogic.cpp` (`get_resin_profile_extensions`,
  `is_resin_profile_file`, `route_dropped_files`)
- CLI: declared in `src/slic3r-shared/include/Slic3r/App/Init.hpp` and
  `src/slic3r-app-launcher/src/Slic3r/App/Launcher/ReadCLI.cpp`
  (`--import-resin-profile`, `--dry-run`, `--report`, `--export-resin-profile`), handled in
  `src/slic3r-app-cli/src/Slic3r/App/CLI/ProcessActions.cpp`
  (`perform_resin_profile_import`, `perform_resin_profile_export`)

User-facing: `doc/sla-fork/user-guide/switching-from-chitubox-and-lychee.md`.

---

## 7. Tests and tools

### The test binaries

| Target | CMakeLists | What it covers |
|---|---|---|
| `sla_print_tests` | `tests/sla_print/CMakeLists.txt` | the SLA engine |
| `slic3r-shared-tests` | `src/slic3r-shared/CMakeLists.txt` | App and Biz |
| `slic3r-domain-tests` | `src/slic3r-domain/CMakeLists.txt` | Domain, incl. the `sla_effective_*` fall-backs |
| `slic3r-app-cli-tests` | `src/slic3r-app-cli/CMakeLists.txt` | the CLI, no SLA cases yet |

All four list their sources **explicitly**; there is no glob, so a new test file has to be added to
the `add_executable` list. Every `CMakeLists.txt` is a hotspot: add lines, keep the list sorted.
`tests/CMakeLists.txt` provides the `test_common` interface library, the `TEST_DATA_DIR` define and
the Catch2 setup; `tests/catch_main.hpp` has the shared main and the `verboseconsole` reporter.

Build only what you need (from `BUILD.md`):

```
cmake --build build-default --target sla_print_tests --config Release
cmake --build build-default --target slic3r-shared-tests --config Release
```

Binaries land at `build-default/tests/sla_print/Release/sla_print_tests.exe` and
`build-default/src/slic3r-shared/Release/slic3r-shared-tests.exe`. Compare against
`doc/sla-fork/baseline-tests.md` before blaming your change for a failure. Full build times and the
dependency prefix are in `BUILD.md`; do not guess them.

### Fixtures

- `src/slic3r-shared/test/Slic3r/Biz/SlaFixture.{hpp,cpp}` — `Slic3r::Test::SlaSlicingFixture`,
  with `slice_sla_model(model, config)`. It owns a `Domain::Workbench`, a
  `StdMainThreadDispatcher` and a real `Biz::ProjectInteractor`, pumps the dispatcher while
  waiting, and unregisters its listener. One live fixture per test: two trip the message-handler
  assertion
- `tests/sla_print/sla_test_utils.{hpp,cpp}` — `test_pad`, `test_supports`, `test_concave_hull`,
  `check_support_tree_integrity`, `check_validity`, and the raster helpers
- `tests/test_utils.hpp` — `load_model` against `TEST_DATA_DIR`
- `src/slic3r-shared/test/data/` — `sla_roundtrip{1,2}.3mf`, `test_sla.ini`,
  `presets/prusa-research-sla/`

### `[.local]` tests and `SLA_LOCAL_SAMPLES`

Two test cases are tagged `[.local]` and are skipped unless `SLA_LOCAL_SAMPLES` names the
gitignored `local-samples/` folder:

- `tests/sla_print/sla_local_island_coverage_tests.cpp` — reads
  `<folder>/benchmark models/manifest.yaml` (ids from `SLA_ISLAND_IDS`, default `bm01,bm13`)
- `src/slic3r-shared/test/Slic3r/Biz/ResultExport/SLA/SlaPm5WorkshopOrientationTests.cpp` — reads
  `<folder>/anycubic-photon-mono-m5/01_5.pm5` and the STL it was sliced from

```
set SLA_LOCAL_SAMPLES=local-samples
build-default\tests\sla_print\Release\sla_print_tests.exe "[.local]"
```

Two more tests read outside files through their own variable: `SLA_COVERAGE_MODEL` in
`sla_island_coverage_tests.cpp`, and `SLA_BENCH_DIR` for the benchmark. `local-samples/` is
gitignored; nothing from it is ever committed.

### Benchmark harness

`tests/sla_print/sla_benchmark_tests.cpp`, `TEST_CASE("SLA benchmark harness", "[.][SLA][benchmark]")`,
hidden so a normal run skips it. `SLA_BENCH_DIR` names the corpus folder (a `manifest.yaml` in it
decides the model list, otherwise the folder is scanned), `SLA_BENCH_FILTER` narrows it to
comma-separated ids and categories, `SLA_BENCH_OUT` names the JSON. It writes a fixed-key-order
document (`schema`, `layer_height_mm`, `island_min_area_mm2`, `models`, `summary`) and per model:
`id`, `category`, `triangles`, `support_points`, `support_tree_triangles`,
`support_tree_volume_mm3`, `pad_volume_mm3`, `layer_count`, `layer_hash`, `islands_detected`,
`islands_without_support_point`, `t_points_ms`, `t_tree_pad_ms`, `t_slice_ms`, `t_total_ms`, and
optionally `peak_working_set_bytes` and `error`. A model is named by `id` and `category` only;
`Bench::describe_error` scrubs file names and paths out of error text, so a
non-redistributable corpus is never named. `layer_hash` is an FNV-1a hash over the layer count and
each layer's contour points, sorted, fed as integers.

`tests/sla_print/sla_bench_manifest.hpp` is the shared manifest reader
(`Bench::Manifest`, `Bench::ModelSpec`, `read_manifest`, `load_model_file`, `centre_on_plate`,
`describe_error`).

`doc/sla-fork/tools/bench_diff.py BEFORE.json AFTER.json` keys the models on `id`, prints a table,
and **exits 1 when any `layer_hash` changed**, so a geometry change is visible. `doc/sla-fork/baseline.json`
is referenced by the roadmap but is not committed yet; M0.13 is still open.

### Visual regression

The app can render a fixture scene offscreen and quit:
`--sla-fixture <3mf>`, `--render-to <png>`, `--render-view prepare|preview`,
`--render-size WIDTHxHEIGHT`. Parsed in `src/slic3r-app-launcher/src/Slic3r/App/Launcher/ReadCLI.cpp`
and validated before the window opens; handled in
`src/slic3r-app-desktop/src/Slic3r/App/Desktop/DesktopApp.cpp` (`process_sla_fixture`,
`start_fixture_render`, `do_fixture_render`), through
`src/slic3r-shared/src/Slic3r/App/Plater/ThumbnailImageGenerator.cpp` and
`src/slic3r-shared/src/Slic3r/App/FixtureRender.cpp` (`fixture_view_from_string`,
`parse_render_size`, `cie_lightness`, `scene_lightness`, `write_fixture_render`).

Each render writes a sidecar JSON next to the PNG with the CIE L\* of the theme tokens it drew
with. `doc/sla-fork/tools/visual_diff.py` (standard library only) has `compare`, `lightness`,
`grayscale` and `check` subcommands; the lightness rule is that the model, the supports and the pad
must be told apart **by lightness**, at a threshold of 10 L\*, and the same rule is a unit test in
`src/slic3r-shared/test/Slic3r/App/FixtureRenderTests.cpp`, so a palette change fails
`slic3r-shared-tests`. Commands and how to make a reference:
`doc/sla-fork/visual-regression.md`. No reference images are committed yet.

`doc/sla-fork/tools/visual_ci.py` (standard library only) is the CI half: `plan` reads
`doc/sla-fork/visual-regression/manifest.json` and prints the renders or the reason there is
nothing to do, and `run` drives the app and `visual_diff.py check` over them into one exit code
(0 passed or nothing to compare, 1 a render changed, 2 the run could not be made).
`doc/sla-fork/tools/test_visual_ci.py` tests it with a fake app. The `visual-regression` job in
`.github/workflows/sla-ci.yml` is manual only, like the whole file, and skips everything that
costs minutes while the manifest names no reference.

### Other tools

- `doc/sla-fork/tools/` — `bench_diff.py`, `orientation_test_piece.py`, `pm5_layout.py`,
  `readme_progress.py` (regenerates the README progress block from the roadmap), `render_icons.py`,
  `visual_diff.py`
- `tools/support-research/` — the offline M7 research scripts, with their own venv and pytest
  suite, outside the CMake build
- `doc/sla-fork/profiling/` — `raster-memory.md`, `peel-force.md`, `aa-and-z-correction.md`
- `doc/sla-fork/formats/` — `ctb.md`, `pm5.md`, `orientation.md`
- `doc/sla-fork/parity/` — `support-points.md`, `hollow.md`
- `doc/sla-fork/ux/` — `journeys.md`, `wireframes.md`, `palette-sweep.md`

There is no CI for any of this yet (M0.14 is open); the workflows in `.github/workflows/` are
upstream's.

---

## 8. Rules that bite

These are the ones that cost time when they are ignored. The first four repeat AGENTS.md; the rest
are what this fork has actually taught us.

**Colours.** Use `Platform::Color` tokens, never an RGB literal. The palette hex values live in one
table in `App/Theme.cpp` and nowhere else. The allowed colours are `#CAD2C5`, `#84A98C`, `#52796F`,
`#354F52` and `#2F3E46`, plus one warning amber and one error red; derived colours are mixes of
palette colours with each other, with white or with black, and each is named in the palette table
with its formula. The resin model tint is the single exception and falls back to `Sage300`.

**Strings.** Mark user-visible strings with `L("…")` or `_u8L("…")`, and never put a non-ASCII
character *inside* the macro. Put a separator or ellipsis outside it, exactly as
`MaterialSelectionDialog` writes the ellipsis, and keep the translatable half as one phrase. Unit
symbols (`mm²`, `°`) are the known exception, left inside as upstream does. Every new file holding
a translatable string must be added to `resources/localization/list.txt` or its strings never reach
translators.

**Hotspot files.** Keep edits to `IGizmo.hpp`, `PlaterRenderModule.cpp` / `.hpp`,
`ToolGizmosUiInfo.cpp`, `ConfigDefsSLA.cpp`, `ConfigBoxesSLA.cpp`, `PrintSteps.hpp`, `SLAResult.hpp`,
`SlicingInteractor.hpp`, `Theme.cpp`, `ThemeTypes.hpp` and every `CMakeLists.txt` minimal, and put
new work in new files. `ConfigDefsSLA.cpp` is the only place a new config key goes, with a tooltip
and a category like its neighbours.

**Never slice from a tool.** Only the Slice button slices an SLA bed; `is_auto_slicing_allowed` in
`Biz/Slicing/AutoSlicing.hpp` is the single gate. The support tool, the tree preview, the drain
hole suggestion and the pre-export check all go through `SLASupportTool` and the caches instead. If
you find yourself adding a `slice_bed` call to a tool, you are about to break the rule.

**Line endings.** `AGENTS.md`, `README.md` and everything in `doc/sla-fork/` are CRLF. When you
patch a markdown file, keep it CRLF and change no other line. `git diff` should show your lines and
nothing else.

**MSVC traps, all of them hit in this fork**

- Qualify `Biz::` and `Domain::` names at the use site. An undeclared identifier in a header that
  only compiles because another translation unit got there first is a real trap, not a theory.
- Include every header you use. There is no PCH (`SLIC3R_PCH=OFF` is required on MSVC).
- `_fpx` needs `using Slic3r::App::Yoga::operator""_fpx;` in scope.
- A Yoga `Text` needs a text argument; leaving it out does not compile.
- A `ComboBox` needs an explicit `std::string` name, not a `const char*` that happens to convert.
- Eigen: do not mix `float` and `double`, and do not mix 4x4 and 3x3. A `Matrix4f` where a
  `Matrix3d` was meant is a silent wrong answer, not a compile error.
- `M0.6` records the cost: touching `SLAResult.hpp` rebuilds most of `slic3r-shared`. Batch
  hotspot-header changes.

**Catch2 traps**

- No `||`, no `&&` and no ternary inside `CHECK` / `REQUIRE`. A comma expression is parsed as one
  argument and the comparison quietly becomes something else. Compute the value into a local first.
- `Approx` needs `#include <catch_approx.hpp>` (or `catch2/catch_approx.hpp`); it is not in the
  umbrella header.
- Hidden tests use the `.` tag, so the default run skips them: run the binary with `"[.]"` to see
  them, and with `"[<tag>]"` to run one.
