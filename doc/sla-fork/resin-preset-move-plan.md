# Resin Preset Move Plan (M1.13c)

## Summary

This document lists the configuration keys that would need to move from their current locations (Print or Printer) to **Material** location to create a unified "Resin" preset holding everything about one resin on one printer.

**Keys found: 52**  
**Already in Material: 47**  
**Need to move: 5** (2 from Print, 3 min/max bounds from Printer)

---

## Table: Resin-Related Keys

| Key | Label | Today's Location | Target Location | Files That Use It |
|-----|-------|------------------|-----------------|-------------------|
| `layer_height` | Layer height | Print (ConfigCommon.cpp:137) | Material | `src/libslic3r/SLAPrint.cpp:662`, `src/slic3r-shared/Biz/Config/ConfigLegacy.cpp:171`, `resources/presets/*/preset-sla-print.yaml` |
| `initial_layer_height` | Initial layer height | **Material** (ConfigDefsSLA.cpp:295) | Material | `src/libslic3r/SLAPrint.cpp:662`, `src/libslic3r/SLAPrintSteps.cpp:346`, `src/slic3r-shared/Biz/ResultExport/SLA/AnycubicSLA.cpp:285,510`, `src/slic3r-shared/Biz/ResultExport/SLA/GooSLA.cpp:295`, `resources/presets/*/preset-sla-material.yaml` |
| `faded_layers` | Faded layers | **Print** (ConfigDefsSLA.cpp:350, overrides_in=Object) | Material | `src/libslic3r/SLAPrint.cpp:653`, `src/libslic3r/SLAPrintSteps.cpp:356,1395,1560`, `src/slic3r-domain/SLA/PrintStatistics.hpp:23`, `src/slic3r-shared/Biz/ResultExport/SLA/AnycubicSLA.cpp:288,513`, `src/slic3r-shared/Biz/ResultExport/SLA/GooSLA.cpp:302`, `src/slic3r-shared/Biz/ResultExport/SLA/SL1.cpp:168`, `src/slic3r-shared/Biz/Config/ConfigLegacy.cpp:170`, `resources/presets/*/preset-sla-print.yaml` (not present, uses default) |
| `exposure_time` | Exposure time | **Material** (ConfigDefsSLA.cpp:384) | Material | `src/libslic3r/SLAPrint.cpp:652`, `src/libslic3r/SLAPrintSteps.cpp:1393`, `src/slic3r-shared/Biz/ResultExport/SLA/AnycubicSLA.cpp:56,286,511`, `src/slic3r-shared/Biz/ResultExport/SLA/GooSLA.cpp:291`, `src/slic3r-shared/Biz/ResultExport/SLA/SL1.cpp:150`, `src/slic3r-shared/Biz/Config/ConfigLegacy.cpp:170`, `resources/presets/*/preset-sla-material.yaml` |
| `initial_exposure_time` | Initial exposure time | **Material** (ConfigDefsSLA.cpp:417) | Material | `src/libslic3r/SLAPrint.cpp:661`, `src/libslic3r/SLAPrintSteps.cpp:1392`, `src/slic3r-shared/Biz/ResultExport/SLA/AnycubicSLA.cpp:287,512`, `src/slic3r-shared/Biz/ResultExport/SLA/GooSLA.cpp:301`, `src/slic3r-shared/Biz/ResultExport/SLA/SL1.cpp:151`, `src/slic3r-shared/Biz/Config/ConfigLegacy.cpp:171`, `resources/presets/*/preset-sla-material.yaml` |
| `bottom_layer_count` | Bottom layer count | **Material** (ConfigDefsSLA.cpp:1264) | Material | `src/libslic3r/SLAPrint.cpp:668`, `src/slic3r-shared/Biz/ResultExport/SLA/AnycubicSLA.cpp:59,154,288,513`, `src/slic3r-shared/test/Biz/Config/ConfigLoadTests.cpp:508` |
| `min_exposure_time` | Minimum exposure time | **Printer** (ConfigDefsSLA.cpp:362) | Material (or stay Printer as bounds) | `src/libslic3r/SLAPrint.cpp:704,1209`, `src/slic3r-shared/Biz/Config/ConfigLegacy.cpp:176`, `src/slic3r-shared/Biz/Config/Legacy/PrintConfig.cpp:4592`, `resources/presets/*/preset-sla-printer.yaml` |
| `max_exposure_time` | Maximum exposure time | **Printer** (ConfigDefsSLA.cpp:373) | Material (or stay Printer as bounds) | `src/libslic3r/SLAPrint.cpp:701,1208`, `src/slic3r-shared/Biz/Config/ConfigLegacy.cpp:175`, `src/slic3r-shared/Biz/Config/Legacy/PrintConfig.cpp:4600`, `resources/presets/*/preset-sla-printer.yaml` |
| `min_initial_exposure_time` | Minimum initial exposure time | **Printer** (ConfigDefsSLA.cpp:395) | Material (or stay Printer as bounds) | `src/libslic3r/SLAPrint.cpp:705,1216`, `src/slic3r-shared/Biz/Config/ConfigLegacy.cpp:176`, `src/slic3r-shared/Biz/Config/Legacy/PrintConfig.cpp:4615`, `resources/presets/*/preset-sla-printer.yaml` |
| `max_initial_exposure_time` | Maximum initial exposure time | **Printer** (ConfigDefsSLA.cpp:406) | Material (or stay Printer as bounds) | `src/libslic3r/SLAPrint.cpp:702,1215`, `src/slic3r-shared/Biz/Config/ConfigLegacy.cpp:175`, `src/slic3r-shared/Biz/Config/Legacy/PrintConfig.cpp:4623`, `resources/presets/*/preset-sla-printer.yaml` |
| `delay_before_exposure` | Delay before exposure | **Material** (ConfigDefsSLA.cpp:822) | Material | `src/libslic3r/SLAPrint.cpp:642`, `src/libslic3r/SLAPrintSteps.cpp:1218,1240`, `src/slic3r-shared/Biz/ResultExport/SLA/AnycubicSLA.cpp:301`, `src/slic3r-shared/Biz/ResultExport/SLA/SL1.cpp:44` |
| `delay_after_exposure` | Delay after exposure | **Material** (ConfigDefsSLA.cpp:834) | Material | `src/libslic3r/SLAPrint.cpp:641`, `src/libslic3r/SLAPrintSteps.cpp:1219,1241`, `src/slic3r-shared/Biz/ResultExport/SLA/SL1.cpp:45` |
| `tower_hop_height` | Tower hop height | **Material** (ConfigDefsSLA.cpp:846) | Material | `src/libslic3r/SLAPrint.cpp:780`, `src/libslic3r/SLAPrintSteps.cpp:1224,1246` |
| `tower_speed` | Tower speed | **Material** (ConfigDefsSLA.cpp:858) | Material | `src/libslic3r/SLAPrint.cpp:781`, `src/libslic3r/SLAPrintSteps.cpp:1230,1252` |
| `tilt_down_initial_speed` | Tilt down initial speed | **Material** (ConfigDefsSLA.cpp:901) | Material | `src/libslic3r/SLAPrint.cpp:771`, `src/libslic3r/SLAPrintSteps.cpp:1231,1253` |
| `tilt_down_finish_speed` | Tilt down finish speed | **Material** (ConfigDefsSLA.cpp:911) | Material | `src/libslic3r/SLAPrint.cpp:770`, `src/libslic3r/SLAPrintSteps.cpp:1232,1254` |
| `tilt_up_initial_speed` | Tilt up initial speed | **Material** (ConfigDefsSLA.cpp:921) | Material | `src/libslic3r/SLAPrint.cpp:777`, `src/libslic3r/SLAPrintSteps.cpp:1233,1255` |
| `tilt_up_finish_speed` | Tilt up finish speed | **Material** (ConfigDefsSLA.cpp:931) | Material | `src/libslic3r/SLAPrint.cpp:776`, `src/libslic3r/SLAPrintSteps.cpp:1234,1256` |
| `use_tilt` | Use tilt | **Material** (ConfigDefsSLA.cpp:941) | Material | `src/libslic3r/SLAPrint.cpp:782,1243`, `src/libslic3r/SLAPrintSteps.cpp:1229,1251,1266` |
| `tilt_down_offset_steps` | Tilt down offset steps | **Material** (ConfigDefsSLA.cpp:950) | Material | `src/libslic3r/SLAPrint.cpp:773`, `src/libslic3r/SLAPrintSteps.cpp:1225,1247,1271` |
| `tilt_down_offset_delay` | Tilt down offset delay | **Material** (ConfigDefsSLA.cpp:962) | Material | `src/libslic3r/SLAPrint.cpp:772`, `src/libslic3r/SLAPrintSteps.cpp:1220,1242` |
| `tilt_down_cycles` | Tilt down cycles | **Material** (ConfigDefsSLA.cpp:974) | Material | `src/libslic3r/SLAPrint.cpp:768`, `src/libslic3r/SLAPrintSteps.cpp:1226,1248,1278` |
| `tilt_down_delay` | Tilt down delay | **Material** (ConfigDefsSLA.cpp:985) | Material | `src/libslic3r/SLAPrint.cpp:769`, `src/libslic3r/SLAPrintSteps.cpp:1221,1243,1283` |
| `tilt_up_offset_steps` | Tilt up offset steps | **Material** (ConfigDefsSLA.cpp:997) | Material | `src/libslic3r/SLAPrint.cpp:779`, `src/libslic3r/SLAPrintSteps.cpp:1227,1249,1290` |
| `tilt_up_offset_delay` | Tilt up offset delay | **Material** (ConfigDefsSLA.cpp:1009) | Material | `src/libslic3r/SLAPrint.cpp:778`, `src/libslic3r/SLAPrintSteps.cpp:1222,1244` |
| `tilt_up_cycles` | Tilt up cycles | **Material** (ConfigDefsSLA.cpp:1021) | Material | `src/libslic3r/SLAPrint.cpp:774`, `src/libslic3r/SLAPrintSteps.cpp:1228,1250` |
| `tilt_up_delay` | Tilt up delay | **Material** (ConfigDefsSLA.cpp:1032) | Material | `src/libslic3r/SLAPrint.cpp:775`, `src/libslic3r/SLAPrintSteps.cpp:1223,1245` |
| `lift_height` | Lift height | **Material** (ConfigDefsSLA.cpp:1044) | Material | `src/libslic3r/SLAPrint.cpp:679`, `src/slic3r-shared/Biz/ResultExport/SLA/AnycubicSLA.cpp:590`, `src/slic3r-shared/test/Biz/Config/ConfigLoadTests.cpp:503` |
| `lift_height_2` | Lift height (above area fill) | **Material** (ConfigDefsSLA.cpp:1055) | Material | `src/libslic3r/SLAPrint.cpp:680` |
| `lift_speed` | Lift speed | **Material** (ConfigDefsSLA.cpp:1066) | Material | `src/libslic3r/SLAPrint.cpp:681`, `src/slic3r-shared/Biz/ResultExport/SLA/AnycubicSLA.cpp:308,591`, `src/slic3r-shared/Biz/ResultExport/SLA/GooSLA.cpp:310` |
| `lift_speed_2` | Lift speed (above area fill) | **Material** (ConfigDefsSLA.cpp:1077) | Material | `src/libslic3r/SLAPrint.cpp:682` |
| `retract_speed` | Retract speed | **Material** (ConfigDefsSLA.cpp:1088) | Material | `src/libslic3r/SLAPrint.cpp:684`, `src/slic3r-shared/Biz/ResultExport/SLA/AnycubicSLA.cpp:314,592`, `src/slic3r-shared/Biz/ResultExport/SLA/GooSLA.cpp:314` |
| `retract_speed_2` | Retract speed (above area fill) | **Material** (ConfigDefsSLA.cpp:1099) | Material | `src/libslic3r/SLAPrint.cpp:685` |
| `wait_before_lift` | Wait before lift | **Material** (ConfigDefsSLA.cpp:1110) | Material | `src/libslic3r/SLAPrint.cpp:688` |
| `wait_after_lift` | Wait after lift | **Material** (ConfigDefsSLA.cpp:1121) | Material | `src/libslic3r/SLAPrint.cpp:686` |
| `wait_after_retract` | Wait after retract | **Material** (ConfigDefsSLA.cpp:1132) | Material | `src/libslic3r/SLAPrint.cpp:687` |
| `light_pwm` | Light PWM | **Material** (ConfigDefsSLA.cpp:1143) | Material | `src/libslic3r/SLAPrint.cpp:683`, `src/slic3r-shared/Biz/ResultExport/SLA/GooSLA.cpp:89,326,420,488`, `src/slic3r-shared/test/Biz/Config/ConfigLoadTests.cpp:505` |
| `bottom_lift_height` | Bottom lift height | **Material** (ConfigDefsSLA.cpp:1154) | Material | `src/libslic3r/SLAPrint.cpp:669`, `src/slic3r-shared/test/Biz/Config/ConfigLoadTests.cpp:506` |
| `bottom_lift_height_2` | Bottom lift height (above area fill) | **Material** (ConfigDefsSLA.cpp:1165) | Material | `src/libslic3r/SLAPrint.cpp:670` |
| `bottom_lift_speed` | Bottom lift speed | **Material** (ConfigDefsSLA.cpp:1176) | Material | `src/libslic3r/SLAPrint.cpp:671`, `src/slic3r-shared/Biz/ResultExport/SLA/AnycubicSLA.cpp:311`, `src/slic3r-shared/Biz/ResultExport/SLA/GooSLA.cpp:308` |
| `bottom_lift_speed_2` | Bottom lift speed (above area fill) | **Material** (ConfigDefsSLA.cpp:1187) | Material | `src/libslic3r/SLAPrint.cpp:672` |
| `bottom_retract_speed` | Bottom retract speed | **Material** (ConfigDefsSLA.cpp:1198) | Material | `src/libslic3r/SLAPrint.cpp:674`, `src/slic3r-shared/Biz/ResultExport/SLA/AnycubicSLA.cpp:311`, `src/slic3r-shared/Biz/ResultExport/SLA/GooSLA.cpp:312` |
| `bottom_retract_speed_2` | Bottom retract speed (above area fill) | **Material** (ConfigDefsSLA.cpp:1209) | Material | `src/libslic3r/SLAPrint.cpp:675` |
| `bottom_wait_before_lift` | Bottom wait before lift | **Material** (ConfigDefsSLA.cpp:1220) | Material | `src/libslic3r/SLAPrint.cpp:678` |
| `bottom_wait_after_lift` | Bottom wait after lift | **Material** (ConfigDefsSLA.cpp:1231) | Material | `src/libslic3r/SLAPrint.cpp:676` |
| `bottom_wait_after_retract` | Bottom wait after retract | **Material** (ConfigDefsSLA.cpp:1242) | Material | `src/libslic3r/SLAPrint.cpp:677` |
| `bottom_light_pwm` | Bottom light PWM | **Material** (ConfigDefsSLA.cpp:1253) | Material | `src/libslic3r/SLAPrint.cpp:673`, `src/slic3r-shared/Biz/ResultExport/SLA/GooSLA.cpp:88,325,419`, `src/slic3r-shared/test/Biz/Config/ConfigLoadTests.cpp:507` |
| `material_correction` / `_x` / `_y` / `_z` | Correction for expansion | **Material** (ConfigDefsSLA.cpp:427-467) | Material | (used in slicing, no direct exporter refs found) |
| `material_print_speed` | Print speed (Slow/Fast/HighViscosity) | **Material** (ConfigDefsSLA.cpp:788) | Material | (enum selector for tilt speeds) |
| `area_fill` | Area fill threshold | **Material** (ConfigDefsSLA.cpp:165) | Material | (used to select between normal/bottom params) |
| `zcorrection_layers` | Z compensation | **Material** (ConfigDefsSLA.cpp:246) | Material | (used in slicing) |

---

## Risks

### 1. `SLAConfigLocation::Print` Assumptions in Code

The following locations explicitly check for or construct `SLAConfigLocation::Print`:

- **Config Boxes**: `src/slic3r-domain/src/Slic3r/Domain/ConfigBoxesSLA.cpp:6` — `SLAPrintSettings` constructs with `SLAConfigLocation::Print`
- **CLI Loading**: `src/slic3r-app-cli/src/Slic3r/App/CLI/LoadPrintData.cpp:349-350` — validates location is Printer or Print
- **Config Definitions**: `src/slic3r-domain/src/Slic3r/Domain/ConfigDef.cpp:353` — switch case for `SLAConfigLocation::Print`
- **Config Model Dump**: `src/slic3r-shared/src/Slic3r/App/ConfigModelDump.cpp:318` — case for `SLAConfigLocation::Print`
- **Plater Render Module**: `src/slic3r-shared/src/Slic3r/App/Plater/PlaterRenderModule.cpp:276` — case for `SLAConfigLocation::Print`
- **Settings Dialogs**: 
  - `src/slic3r-shared/src/Slic3r/App/PrintSettingsDialog.cpp:356` — filters by `SLAConfigLocation::Print`
  - `src/slic3r-shared/src/Slic3r/App/SidebarPrint.cpp:238` — filters by `SLAConfigLocation::Print`
- **Config Loading**: `src/slic3r-shared/src/Slic3r/Biz/Config/ConfigLoad.cpp:326,341` — loads print settings from `SLAConfigLocation::Print`
- **Preset Interactor**: `src/slic3r-shared/src/Slic3r/Biz/Preset/PresetInteractor.cpp:2457,2549` — handles `SLAConfigLocation::Print` for preset operations

If keys move from Print to Material, the `SLAPrintSettings` config box would no longer contain them. Code that reads these keys via `SLAPrintSettings` (e.g., `print_config.get("faded_layers")`) would need to read from `SLAMaterialSettings` instead.

### 2. Preset YAML Files Put Keys Under Print Presets

- **`resources/presets/*/preset-sla-print.yaml`** contains `layer_height` and `faded_layers` (via default)
- **`resources/presets/*/preset-sla-material.yaml`** contains exposure times, lift speeds, etc. (already correct)
- **`resources/presets/*/preset-sla-printer.yaml`** contains min/max exposure time bounds

Moving keys would require:
- Moving `layer_height` and `faded_layers` from print preset YAMLs to material preset YAMLs
- Deciding whether min/max bounds move to material presets or stay in printer presets

### 3. 3MF Project Loading

- **`src/slic3r-shared/src/Slic3r/Biz/Config/Legacy/3mf_legacy.cpp`** — loads/saves SLA print settings and material settings separately. The 3MF format stores print and material configs in separate sections. Moving keys would change the 3MF schema (print section loses keys, material section gains them). Old 3MF files would need migration.

### 4. Legacy Config Mapping

- **`src/slic3r-shared/src/Slic3r/Biz/Config/ConfigLegacy.cpp:170-171`** — maps legacy ini keys including `faded_layers`, `exposure_time`, `initial_exposure_time`, `initial_layer_height`, `layer_height`, min/max exposure times. The legacy importer expects these in specific sections.

### 5. Exporters Read from Material Config (Mostly Correct)

Most exporters (Anycubic, Goo, SL1) already read resin parameters from material config:
- `sla_material_settings.items.opt("exposure_time")`
- `sla_material_settings.items.opt("initial_layer_height")`
- `sla_material_settings.items.opt("initial_exposure_time")`
- `sla_material_settings.items.opt("lift_height")`, etc.

**Exception**: `faded_layers` is read from print config in exporters:
- `AnycubicSLA.cpp:288` — `get_cfg_value_i(cfg, "faded_layers")` where cfg is print config
- `AnycubicSLA.cpp:513` — same
- `GooSLA.cpp:302` — same
- `SL1.cpp:168` — uses `stats.count_faded_layers` from print statistics

These would need to read from material config after the move.

### 6. SLAPrintSteps.cpp Propagation Steps

`src/libslic3r/src/libslic3r/SLAPrint.cpp` lines 641-688, 701-705, 768-782 define propagation steps for each key. Keys moving to Material would need their propagation step updated from `slapsMergeSlicesAndEval` (print-level) to a material-level step, or the config would need to be merged into the print config at slice time.

---

## Keys Requiring Move (5 total)

| Key | From | To | Notes |
|-----|------|-----|-------|
| `layer_height` | Print | Material | Defined in ConfigCommon.cpp, used by both FFF and SLA. SLA-specific override needed. |
| `faded_layers` | Print | Material | Has `overrides_in = {Object}`. Used in exporters via print config. |
| `min_exposure_time` | Printer | Material* | Printer capability bounds. May stay in Printer. |
| `max_exposure_time` | Printer | Material* | Printer capability bounds. May stay in Printer. |
| `min_initial_exposure_time` | Printer | Material* | Printer capability bounds. May stay in Printer. |
| `max_initial_exposure_time` | Printer | Material* | Printer capability bounds. May stay in Printer. |

*Decision needed: min/max bounds are printer capabilities, not resin properties. They may logically stay in Printer location.

---

## Keys Already in Material (47)

All other resin-related keys (exposure times, lift/retract speeds, wait/delay timings, tilt parameters, bottom layer variants, light PWM, corrections, print speed enum, area fill, z-correction) are already correctly located in **Material**.