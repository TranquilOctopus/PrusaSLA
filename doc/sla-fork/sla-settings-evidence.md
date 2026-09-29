# SLA settings evidence (M1.13d2)

Evidence-based redo of `sla-settings-audit-a.md`, `-b.md` and `-c.md`, so that the follow-up
job (M1.13d) can hide settings without guessing. **No code was changed**; this document only
records what each setting is read by.

165 settings are covered: 100 **used**, 46 **unused for SLA**, 18 **ui-only**, 1
**superseded** (`pad_around_object`). 47 settings are therefore safe to hide.

## How the settings dialog picks keys

The App does not keep a hand-written list of SLA settings. The categoriser shows **every**
definition of the config container whose category is not `Hidden`:

- `src/slic3r-shared/src/Slic3r/App/Config/ObservableCategorizer.cpp:9` -
  `def().category != ConfigItemDef::Category::Hidden`
- `src/slic3r-shared/src/Slic3r/App/Config/CategoryUtils.cpp:101` - `Category::Hidden` skips a row.

So "everything the app shows when an SLA printer is selected" is exactly the non-`Hidden`
definitions of the SLA printer / material / print configs, which is the whole of
`src/slic3r-domain/src/Slic3r/Domain/ConfigDefsSLA.cpp` plus the SLA instance of
`init_common_fdm_sla_config_items` in `src/slic3r-domain/src/Slic3r/Domain/ConfigCommon.cpp`.
`Biz/Preset/AbstractConfigManipulation.cpp:366-432` (`toggle_print_sla_options`) additionally
disables individual fields, but it never hides a whole group by itself.

## Verdicts

| verdict | meaning |
|---------|---------|
| **used** | the SLA engine or an SLA exporter reads the key by name. Evidence is a `file:line` of a read. The `SLAPrint.cpp` invalidation table (`src/libslic3r/src/libslic3r/SLAPrint.cpp:615-784`) is **not** counted as a read - it only says "when this key changes, re-run these steps", so it is reported as *declared*. |
| **ui-only** | read only by the App / Biz UI layer, for display, gizmos or field visibility. Nothing on the print path reads it. |
| **unused for SLA** | no read on any SLA path. |
| **superseded** | replaced by another key on the current code path. |

Short paths in the evidence column: `SLAPrint*.cpp`, `SLALayerImage.cpp`, `SLASupportTool.cpp`,
`Format/*` and `SLA/*` live in `src/libslic3r/src/libslic3r/`; `Biz/...` in
`src/slic3r-shared/src/Slic3r/`; `App/...` in `src/slic3r-shared/src/Slic3r/`.

| key | label | location | verdict | evidence |
|-----|-------|----------|---------|----------|
| `display_width` | Display width | Printer | **used** | engine `SLALayerImage.cpp:18`, `SLAPrintSteps.cpp:1420` (display area vs `area_fill`), `Format/SL1.cpp:34`, `Format/SL1_SVG.cpp:220`, `Format/AnycubicSLA.cpp:79`, `Format/GooSLA.cpp:124`; exporter `Biz/ResultExport/SLA/AnycubicSLA.cpp:500,537`, `GooSLA.cpp:284` |
| `display_height` | Display height | Printer | **used** | engine `SLALayerImage.cpp:19`, `SLAPrintSteps.cpp:1421`, `Format/SL1.cpp:35`, `Format/SL1_SVG.cpp:221`, `Format/AnycubicSLA.cpp:80`, `Format/GooSLA.cpp:125`; exporter `AnycubicSLA.cpp:538`, `GooSLA.cpp:285` |
| `display_pixels_x` | X | Printer | **used** | engine `SLALayerImage.cpp:20`, `Format/SL1.cpp:36`, `Format/AnycubicSLA.cpp:81`, `Format/GooSLA.cpp:126`; exporter `AnycubicSLA.cpp:292,501,517`, `GooSLA.cpp:278` |
| `display_pixels_y` | Y | Printer | **used** | engine `SLALayerImage.cpp:21`, `Format/SL1.cpp:37`, `Format/AnycubicSLA.cpp:82`, `Format/GooSLA.cpp:127`; exporter `AnycubicSLA.cpp:293,518`, `GooSLA.cpp:279` |
| `display_mirror_x` | Mirror horizontally | Printer | **used** | engine `SLALayerImage.cpp:24`, `Format/SL1.cpp:40`, `Format/SL1_SVG.cpp:229`, `Format/AnycubicSLA.cpp:85`, `Format/GooSLA.cpp:130`; exporter `GooSLA.cpp:281` |
| `display_mirror_y` | Mirror vertically | Printer | **used** | engine `SLALayerImage.cpp:25`, `Format/SL1.cpp:41`, `Format/SL1_SVG.cpp:230`, `Format/AnycubicSLA.cpp:86`, `Format/GooSLA.cpp:131`; exporter `GooSLA.cpp:282` |
| `display_orientation` | Display orientation | Printer | **used** | engine `SLALayerImage.cpp:27`, `Format/SL1.cpp:43`, `Format/SL1_SVG.cpp:232`, `Format/AnycubicSLA.cpp:88`, `Format/GooSLA.cpp:133` |
| `fast_tilt_time` | Fast | Printer | **used** | engine `SLAPrintSteps.cpp:1405` (peel time, non-Prusa branch `:1528`) |
| `slow_tilt_time` | Slow | Printer | **used** | engine `SLAPrintSteps.cpp:1406,1528` |
| `high_viscosity_tilt_time` | High viscosity | Printer | **used** | engine `SLAPrintSteps.cpp:1407,1527` |
| `area_fill` | Area fill threshold | Material | **used** | engine `SLAPrintSteps.cpp:1404,1513,1525`; exporter `Biz/ResultExport/SLA/SL1.cpp:114` |
| `relative_correction` | Printer scaling correction | Printer | **used** | engine `SLAPrint.cpp:1490` (size gate for the scalars), consumed at `SLAPrint.cpp:286-287,1132` |
| `relative_correction_x` | Printer scaling correction in X axis | Printer | **used** | engine `SLAPrint.cpp:1491` -> model transform `SLAPrint.cpp:287` |
| `relative_correction_y` | Printer scaling correction in Y axis | Printer | **used** | engine `SLAPrint.cpp:1492` -> model transform `SLAPrint.cpp:287` |
| `relative_correction_z` | Printer scaling correction in Z axis | Printer | **used** | engine `SLAPrint.cpp:1493` -> model offset `SLAPrint.cpp:286` |
| `absolute_correction` | Printer absolute correction | Printer | **used** | engine `SLAPrintSteps.cpp:367` applied to every slice at `:381-385` |
| `elefant_foot_min_width` | Elephant foot minimum width | Printer | **used** | engine `SLAPrintSteps.cpp:364` (min feature width for the EFC shrink at `:390`) |
| `zcorrection_layers` | Z compensation | Material | **used** | engine `SLAPrintSteps.cpp:394` -> `SLA/ZCorrection.cpp` |
| `gamma_correction` | Printer gamma correction | Printer | **used** | engine `SLALayerImage.cpp:37`, `Format/SL1.cpp:55`, `Format/AnycubicSLA.cpp:100`, `Format/GooSLA.cpp:145` |
| `material_colour` | Color | Material | **ui-only** | no read anywhere; only the generic colour control renders/edits it (`App/Config/ConfigItemControl.cpp:222`, `ConfigItemPreview.cpp:97`). The comment at `App/Theme.cpp:162` claims a runtime override that no code performs |
| `material_type` | SLA material type | Material | **ui-only** | App `App/MaterialSelectionDialog.cpp:196` filters the material picker by this key; `Category::Hidden`, so it is not shown in the settings dialog |
| `initial_layer_height` | Initial layer height | Material | **used** | engine `SLAPrintSteps.cpp:350,352`; exporter `AnycubicSLA.cpp:285,510` |
| `bottle_volume` | Bottle volume | Material | **used** | Biz `Biz/ResinEconomics.cpp:82` (weight/price shown in the App sidebar); exporter `AnycubicSLA.cpp:280,521`, `GooSLA.cpp:342` |
| `bottle_weight` | Bottle weight | Material | **used** | Biz `Biz/ResinEconomics.cpp:83`; exporter `AnycubicSLA.cpp:279,520`, `GooSLA.cpp:341` |
| `material_density` | Density | Material | **used** | Biz `Biz/ResinEconomics.cpp:86` |
| `bottle_cost` | Cost | Material | **used** | Biz `Biz/ResinEconomics.cpp:84`; exporter `AnycubicSLA.cpp:281,522`, `GooSLA.cpp:343` |
| `faded_layers` | Faded layers | Print | **used** | engine `SLAPrintSteps.cpp:363,1412,1577`; exporter `AnycubicSLA.cpp:288,513`, `GooSLA.cpp:302` |
| `min_exposure_time` | Minimum exposure time | Printer | **unused for SLA** | no read; only the legacy key list `Biz/Config/ConfigLegacy.cpp:176` and the legacy def `Biz/Config/Legacy/PrintConfig.cpp:4592`; set but never consumed in `resources/presets/community-sla/CommunitySLA/preset-sla-printer.yaml:19` |
| `max_exposure_time` | Maximum exposure time | Printer | **unused for SLA** | no read; only `ConfigLegacy.cpp:175` + legacy def `PrintConfig.cpp:4600` |
| `exposure_time` | Exposure time | Material | **used** | engine `SLAPrintSteps.cpp:1410,1552`; exporter `AnycubicSLA.cpp:286,511`, `GooSLA.cpp:291`, `SL1.cpp:150` |
| `min_initial_exposure_time` | Minimum initial exposure time | Printer | **unused for SLA** | no read; only `ConfigLegacy.cpp:176` + legacy def `PrintConfig.cpp:4615` |
| `max_initial_exposure_time` | Maximum initial exposure time | Printer | **unused for SLA** | no read; only `ConfigLegacy.cpp:176` + legacy def `PrintConfig.cpp:4623`; set but never consumed in `preset-sla-printer.yaml:22` |
| `initial_exposure_time` | Initial exposure time | Material | **used** | engine `SLAPrintSteps.cpp:1409,1552`; exporter `AnycubicSLA.cpp:287,512`, `GooSLA.cpp:301`, `SL1.cpp:151` |
| `material_correction` | (none) | Material | **used** | engine `SLAPrint.cpp:1496` (size gate for the scalars) |
| `material_correction_x` | X | Material | **used** | engine `SLAPrint.cpp:1497` -> model transform `SLAPrint.cpp:287` |
| `material_correction_y` | Y | Material | **used** | engine `SLAPrint.cpp:1498` -> model transform `SLAPrint.cpp:287` |
| `material_correction_z` | Z | Material | **used** | engine `SLAPrint.cpp:1499` -> model transform `SLAPrint.cpp:287` |
| `material_notes` | SLA print material notes | Material | **unused for SLA** | no read; free-text field shown in the material panel. Kept visible on purpose, see the caveat below |
| `material_vendor` | (none) | Material | **unused for SLA** | no read; `Category::Hidden`; only the legacy key list `ConfigLegacy.cpp:172` and the legacy def `PrintConfig.cpp` |
| `supports_enable` | Generate supports | Print | **used** | engine `SLAPrint.cpp:98,1465,1535`, `SLAPrintSteps.cpp:734,814,958,1037`, `SLASupportTool.cpp:78,237` |
| `support_tree_type` | Support tree type | Print | **used** | engine `SLAPrint.cpp:99` (support cfg), `SLAPrintSteps.cpp:849`, `SLASupportTool.cpp:185`; also drives field visibility `Biz/Preset/AbstractConfigManipulation.cpp:369` |
| `support_enforcers_only` | Support only in enforced regions | Print | **used** | engine `SLAPrintSteps.cpp:935` |
| `support_points_density_relative` | Support points density | Print | **used** | engine `SLAPrintSteps.cpp:847`, `SLASupportTool.cpp:183`; App `App/Plater/SlaSupportPointsGizmo.cpp:95,431` |
| `pad_enable` | Use raft | Print | **used** | engine `SLAPrintSteps.cpp:996` (`generate_pad` gate), `:1038`, `SLAPrint.cpp:1451,1545`, `SLASupportTool.cpp:119,240`. Still read independently of `raft_type` - see the note on `raft_type` |
| `pad_wall_thickness` | Raft wall thickness | Print | **used** | engine `SLAPrint.cpp:81,175,222,245` (`raft_preset_to_pad_values` base value + legacy branch) |
| `pad_wall_height` | Raft height | Print | **used** | engine `SLAPrint.cpp:80,174,221,249` |
| `pad_brim_size` | Raft expansion | Print | **used** | engine `SLAPrint.cpp:82,176,223,250`; halved for the Skate raft in `Domain/SLA/RaftPreset.cpp:43` |
| `pad_max_merge_distance` | Max merge distance | Print | **used** | engine `SLAPrint.cpp:230,248` |
| `pad_wall_slope` | Raft slope | Print | **used** | engine `SLAPrint.cpp:83,177,224,246`; forced to 70 deg for Skate in `Domain/SLA/RaftPreset.cpp:44` |
| `pad_around_object` | Raft around object | Print | **superseded** | by `raft_type`: the only geometry read is the legacy branch `SLAPrint.cpp:90` (`is_zero_elevation`), which is skipped when `raft_type` is in the config (`SLAPrint.cpp:75`); what is left is a Biz field-visibility toggle `Biz/Preset/AbstractConfigManipulation.cpp:420,423` |
| `pad_around_object_everywhere` | Raft around object everywhere | Print | **used** | engine `SLAPrint.cpp:184,198` (raft placement) |
| `pad_object_gap` | Raft gap to object | Print | **used** | engine `SLAPrint.cpp:84,178,199,225` |
| `pad_object_connector_stride` | Pad object connector stride | Print | **used** | engine `SLAPrint.cpp:187,201` |
| `pad_object_connector_width` | Pad object connector width | Print | **used** | engine `SLAPrint.cpp:186,200` |
| `pad_object_connector_penetration` | Pad object connector penetration | Print | **used** | engine `SLAPrint.cpp:188,202` |
| `raft_type` | Raft type | Print | **used** | engine `SLAPrint.cpp:75,77` (zero elevation), `:169,171` (embedded pad), `:216,218` (pad cfg); the mapping lives in `Domain/SLA/RaftPreset.cpp:9-49` |
| `hollowing_enable` | Enable hollowing | Print | **used** | engine `SLAPrintSteps.cpp:551` (hollowing step), `:1583` (statistics) |
| `hollowing_min_thickness` | Wall thickness | Print | **used** | engine `SLAPrintSteps.cpp:558` |
| `hollowing_quality` | Accuracy | Print | **used** | engine `SLAPrintSteps.cpp:559` |
| `hollowing_closing_distance` | Closing distance | Print | **used** | engine `SLAPrintSteps.cpp:560` |
| `material_print_speed` | Print speed | Material | **used** | engine `SLAPrintSteps.cpp:1526-1527,1537,1539`; exporter `SL1.cpp:152` |
| `sla_archive_format` | Format of the output SLA archive | Printer | **used** | engine `SLAPrintSteps.cpp:1649` (picks the file type); App `App/ResultExport/ExportPathSelect.cpp:79`; exporter `AnycubicSLA.cpp:327` |
| `sla_output_precision` | SLA output precision | Printer | **used** | engine `Format/SL1_SVG.cpp:223` (SVG raster grid) |
| `delay_before_exposure` | Delay before exposure | Material | **used** | engine `SLAPrintSteps.cpp:1257` (`ExposureProfile`, used by `layer_peel_move_time` `:1277`); exporter `Biz/ResultExport/SLA/SL1.cpp:43-46,79-84` (key list + read loop) |
| `delay_after_exposure` | Delay after exposure | Material | **used** | engine `SLAPrintSteps.cpp:1258` (`ExposureProfile`, used by `layer_peel_move_time` `:1277`); exporter `Biz/ResultExport/SLA/SL1.cpp:44-47,79-84` (key list + read loop) |
| `tower_hop_height` | Tower hop height | Material | **used** | engine `SLAPrintSteps.cpp:1263` (`ExposureProfile`, used by `layer_peel_move_time` `:1277`); exporter `Biz/ResultExport/SLA/SL1.cpp:52-54,87-92` (key list + read loop) |
| `tower_speed` | Tower speed | Material | **used** | engine `SLAPrintSteps.cpp:1269` (`ExposureProfile`, used by `layer_peel_move_time` `:1277`); exporter `Biz/ResultExport/SLA/SL1.cpp:56,94-99` (key list + read loop) |
| `tilt_down_initial_speed` | Tilt down initial speed | Material | **used** | engine `SLAPrintSteps.cpp:1270` (`ExposureProfile`, used by `layer_peel_move_time` `:1277`); exporter `Biz/ResultExport/SLA/SL1.cpp:57,94-99` (key list + read loop) |
| `tilt_down_finish_speed` | Tilt down finish speed | Material | **used** | engine `SLAPrintSteps.cpp:1271` (`ExposureProfile`, used by `layer_peel_move_time` `:1277`); exporter `Biz/ResultExport/SLA/SL1.cpp:58,94-99` (key list + read loop) |
| `tilt_up_initial_speed` | Tilt up initial speed | Material | **used** | engine `SLAPrintSteps.cpp:1272` (`ExposureProfile`, used by `layer_peel_move_time` `:1277`); exporter `Biz/ResultExport/SLA/SL1.cpp:59,94-99` (key list + read loop) |
| `tilt_up_finish_speed` | Tilt up finish speed | Material | **used** | engine `SLAPrintSteps.cpp:1273` (`ExposureProfile`, used by `layer_peel_move_time` `:1277`); exporter `Biz/ResultExport/SLA/SL1.cpp:60,94-99` (key list + read loop) |
| `use_tilt` | Use tilt | Material | **used** | engine `SLAPrintSteps.cpp:1268` (`ExposureProfile`, used by `layer_peel_move_time` `:1277`); exporter `Biz/ResultExport/SLA/SL1.cpp:64,101-105` (key list + read loop) |
| `tilt_down_offset_steps` | Tilt down offset steps | Material | **used** | engine `SLAPrintSteps.cpp:1264` (`ExposureProfile`, used by `layer_peel_move_time` `:1277`); exporter `Biz/ResultExport/SLA/SL1.cpp:66-67,107-111` (key list + read loop) |
| `tilt_down_offset_delay` | Tilt down offset delay | Material | **used** | engine `SLAPrintSteps.cpp:1259` (`ExposureProfile`, used by `layer_peel_move_time` `:1277`); exporter `Biz/ResultExport/SLA/SL1.cpp:45,79-84` (key list + read loop) |
| `tilt_down_cycles` | Tilt down cycles | Material | **used** | engine `SLAPrintSteps.cpp:1265` (`ExposureProfile`, used by `layer_peel_move_time` `:1277`); exporter `Biz/ResultExport/SLA/SL1.cpp:66-67,107-111` (key list + read loop) |
| `tilt_down_delay` | Tilt down delay | Material | **used** | engine `SLAPrintSteps.cpp:1260` (`ExposureProfile`, used by `layer_peel_move_time` `:1277`); exporter `Biz/ResultExport/SLA/SL1.cpp:45,79-84` (key list + read loop) |
| `tilt_up_offset_steps` | Tilt up offset steps | Material | **used** | engine `SLAPrintSteps.cpp:1266` (`ExposureProfile`, used by `layer_peel_move_time` `:1277`); exporter `Biz/ResultExport/SLA/SL1.cpp:67-68,107-111` (key list + read loop) |
| `tilt_up_offset_delay` | Tilt up offset delay | Material | **used** | engine `SLAPrintSteps.cpp:1261` (`ExposureProfile`, used by `layer_peel_move_time` `:1277`); exporter `Biz/ResultExport/SLA/SL1.cpp:46,79-84` (key list + read loop) |
| `tilt_up_cycles` | Tilt up cycles | Material | **used** | engine `SLAPrintSteps.cpp:1267` (`ExposureProfile`, used by `layer_peel_move_time` `:1277`); exporter `Biz/ResultExport/SLA/SL1.cpp:70,107-111` (key list + read loop) |
| `tilt_up_delay` | Tilt up delay | Material | **used** | engine `SLAPrintSteps.cpp:1262` (`ExposureProfile`, used by `layer_peel_move_time` `:1277`); exporter `Biz/ResultExport/SLA/SL1.cpp:47,79-84` (key list + read loop) |
| `lift_height` | Lift height | Material | **unused for SLA** | no read; the non-Prusa branch of `SLAPrintSteps.cpp:1524-1549` uses `fast_tilt_time`/`slow_tilt_time`/`high_viscosity_tilt_time` instead. Grep for `"lift_height"` over `src/` returns only the def, the invalidation entry `SLAPrint.cpp:679` and `test/.../ConfigLoadTests.cpp:503` |
| `lift_height_2` | Lift height (above area fill) | Material | **unused for SLA** | no read; only the def and the invalidation entry `SLAPrint.cpp:680` |
| `lift_speed` | Lift speed | Material | **used** | exporter `Biz/ResultExport/SLA/GooSLA.cpp:298,310` (`get_cfg_value_f(cfg, "lift_speed", 2.0f)`); the SLA def default is 0, so the fallback is what normally applies. Never read by the engine |
| `lift_speed_2` | Lift speed (above area fill) | Material | **unused for SLA** | no read; only the def and the invalidation entry `SLAPrint.cpp:682` |
| `retract_speed` | Retract speed | Material | **used** | exporter `Biz/ResultExport/SLA/GooSLA.cpp:314` (`get_cfg_value_f(cfg, "retract_speed", 3.0f)`). Note the key is also an FFF extruder key (`ConfigDefsFDM.cpp:3375`); the other reads (`GCode/WipeTower*.cpp`, `GCode.cpp:1283`) are FFF only |
| `retract_speed_2` | Retract speed (above area fill) | Material | **unused for SLA** | no read; only the def and the invalidation entry `SLAPrint.cpp:685` |
| `wait_before_lift` | Wait before lift | Material | **unused for SLA** | no read; only the def and the invalidation entry `SLAPrint.cpp:688` |
| `wait_after_lift` | Wait after lift | Material | **unused for SLA** | no read; only the def and the invalidation entry `SLAPrint.cpp:686` |
| `wait_after_retract` | Wait after retract | Material | **unused for SLA** | no read; only the def and the invalidation entry `SLAPrint.cpp:687` |
| `light_pwm` | Light PWM | Material | **unused for SLA** | no read; only the def, the invalidation entry `SLAPrint.cpp:683` and `test/.../ConfigLoadTests.cpp:505`. The key is not even set in `resources/presets/prusa-research-sla` |
| `bottom_lift_height` | Bottom lift height | Material | **unused for SLA** | no read; only the def, the invalidation entry `SLAPrint.cpp:669` (`steps({propagate(slapsMergeSlicesAndEval)})`, i.e. declared) and `test/.../ConfigLoadTests.cpp:506-508` |
| `bottom_lift_height_2` | Bottom lift height (above area fill) | Material | **unused for SLA** | no read; only the def, the invalidation entry `SLAPrint.cpp:670` (`steps({propagate(slapsMergeSlicesAndEval)})`, i.e. declared) and `test/.../ConfigLoadTests.cpp:506-508` |
| `bottom_lift_speed` | Bottom lift speed | Material | **unused for SLA** | no read; only the def, the invalidation entry `SLAPrint.cpp:671` (`steps({propagate(slapsMergeSlicesAndEval)})`, i.e. declared) and `test/.../ConfigLoadTests.cpp:506-508` |
| `bottom_lift_speed_2` | Bottom lift speed (above area fill) | Material | **unused for SLA** | no read; only the def, the invalidation entry `SLAPrint.cpp:672` (`steps({propagate(slapsMergeSlicesAndEval)})`, i.e. declared) and `test/.../ConfigLoadTests.cpp:506-508` |
| `bottom_retract_speed` | Bottom retract speed | Material | **unused for SLA** | no read; only the def, the invalidation entry `SLAPrint.cpp:674` (`steps({propagate(slapsMergeSlicesAndEval)})`, i.e. declared) and `test/.../ConfigLoadTests.cpp:506-508` |
| `bottom_retract_speed_2` | Bottom retract speed (above area fill) | Material | **unused for SLA** | no read; only the def, the invalidation entry `SLAPrint.cpp:675` (`steps({propagate(slapsMergeSlicesAndEval)})`, i.e. declared) and `test/.../ConfigLoadTests.cpp:506-508` |
| `bottom_wait_before_lift` | Bottom wait before lift | Material | **unused for SLA** | no read; only the def, the invalidation entry `SLAPrint.cpp:678` (`steps({propagate(slapsMergeSlicesAndEval)})`, i.e. declared) and `test/.../ConfigLoadTests.cpp:506-508` |
| `bottom_wait_after_lift` | Bottom wait after lift | Material | **unused for SLA** | no read; only the def, the invalidation entry `SLAPrint.cpp:676` (`steps({propagate(slapsMergeSlicesAndEval)})`, i.e. declared) and `test/.../ConfigLoadTests.cpp:506-508` |
| `bottom_wait_after_retract` | Bottom wait after retract | Material | **unused for SLA** | no read; only the def, the invalidation entry `SLAPrint.cpp:677` (`steps({propagate(slapsMergeSlicesAndEval)})`, i.e. declared) and `test/.../ConfigLoadTests.cpp:506-508` |
| `bottom_light_pwm` | Bottom light PWM | Material | **unused for SLA** | no read; only the def, `SLAPrint.cpp:673` and `ConfigLoadTests.cpp:507`. `GooSLA.cpp:325-326` hard-codes 255 instead |
| `bottom_layer_count` | Bottom layer count | Material | **unused for SLA** | no read; only the def, `SLAPrint.cpp:668` and `ConfigLoadTests.cpp:508`. `AnycubicSLA.cpp:288,513` derives its own `bottom_layer_count` local from `faded_layers` |
| `material_source_note` | Material source note | Material | **unused for SLA** | no read and nothing writes it: only the def, the invalidation entry `SLAPrint.cpp:690` (`steps({})`) and `test/.../ConfigLoadTests.cpp:509` which asserts it is empty. The foreign-resin importer keeps `source_format`/`source_path` in `Biz/ResinProfile/ChituboxCfgReader.cpp:185-186` and never maps them to this key |
| `support_head_front_diameter` | Pinhead front diameter | Print | **used** | engine `SLAPrint.cpp:103 (also 1233), SLAPrintSteps.cpp:852, SLASupportTool.cpp:188` - only for `support_tree_type=default` (`make_support_cfg`, `SLAPrint.cpp:94`) |
| `support_head_penetration` | Head penetration | Print | **used** | engine `SLAPrint.cpp:108 (also 1224)` - only for `support_tree_type=default` (`make_support_cfg`, `SLAPrint.cpp:94`) |
| `support_head_width` | Pinhead width | Print | **used** | engine `SLAPrint.cpp:109 (also 1225)` - only for `support_tree_type=default` (`make_support_cfg`, `SLAPrint.cpp:94`) |
| `support_pillar_diameter` | Pillar diameter | Print | **used** | engine `SLAPrint.cpp:104 (also 1234)` - only for `support_tree_type=default` (`make_support_cfg`, `SLAPrint.cpp:94`) |
| `support_small_pillar_diameter_percent` | Small pillar diameter percent | Print | **used** | engine `SLAPrint.cpp:107` - only for `support_tree_type=default` (`make_support_cfg`, `SLAPrint.cpp:94`) |
| `support_max_bridges_on_pillar` | Max bridges on a pillar | Print | **used** | engine `SLAPrint.cpp:124` - only for `support_tree_type=default` (`make_support_cfg`, `SLAPrint.cpp:94`) |
| `support_max_weight_on_model` | Max weight on model | Print | **used** | engine `SLAPrint.cpp:125` - only for `support_tree_type=default` (`make_support_cfg`, `SLAPrint.cpp:94`) |
| `support_pillar_connection_mode` | Pillar connection mode | Print | **used** | engine `SLAPrint.cpp:115` - only for `support_tree_type=default` (`make_support_cfg`, `SLAPrint.cpp:94`) |
| `support_buildplate_only` | Support on build plate only | Print | **used** | engine `SLAPrint.cpp:116` - only for `support_tree_type=default` (`make_support_cfg`, `SLAPrint.cpp:94`) |
| `support_pillar_widening_factor` | Pillar widening factor | Print | **used** | engine `SLAPrint.cpp:117` - only for `support_tree_type=default` (`make_support_cfg`, `SLAPrint.cpp:94`) |
| `support_base_diameter` | Support base diameter | Print | **used** | engine `SLAPrint.cpp:118` - only for `support_tree_type=default` (`make_support_cfg`, `SLAPrint.cpp:94`) |
| `support_base_height` | Support base height | Print | **used** | engine `SLAPrint.cpp:119` - only for `support_tree_type=default` (`make_support_cfg`, `SLAPrint.cpp:94`) |
| `support_base_safety_distance` | Support base safety distance | Print | **used** | engine `SLAPrint.cpp:121-122` - only for `support_tree_type=default` (`make_support_cfg`, `SLAPrint.cpp:94`) |
| `support_critical_angle` | Critical angle | Print | **used** | engine `SLAPrint.cpp:112` - only for `support_tree_type=default` (`make_support_cfg`, `SLAPrint.cpp:94`) |
| `support_max_bridge_length` | Max bridge length | Print | **used** | engine `SLAPrint.cpp:113` - only for `support_tree_type=default` (`make_support_cfg`, `SLAPrint.cpp:94`) |
| `support_max_pillar_link_distance` | Max pillar linking distance | Print | **used** | engine `SLAPrint.cpp:114` - only for `support_tree_type=default` (`make_support_cfg`, `SLAPrint.cpp:94`) |
| `support_object_elevation` | Object elevation | Print | **used** | engine `SLAPrint.cpp:111 (also 1449, 1480), SLASupportTool.cpp:238` - only for `support_tree_type=default` (`make_support_cfg`, `SLAPrint.cpp:94`) |
| `branching_support_head_front_diameter` | Branching | Print | **unused for SLA** | the engine reads `branchingsupport_*` (no underscore) at `SLAPrint.cpp:131-153`; the defs created in `ConfigDefsSLA.cpp:1282-1534` are `branching_support_*`, which nothing reads. Already `Category::Hidden` |
| `branching_support_head_penetration` | Branching | Print | **unused for SLA** | the engine reads `branchingsupport_*` (no underscore) at `SLAPrint.cpp:131-153`; the defs created in `ConfigDefsSLA.cpp:1282-1534` are `branching_support_*`, which nothing reads. Already `Category::Hidden` |
| `branching_support_head_width` | Branching | Print | **unused for SLA** | the engine reads `branchingsupport_*` (no underscore) at `SLAPrint.cpp:131-153`; the defs created in `ConfigDefsSLA.cpp:1282-1534` are `branching_support_*`, which nothing reads. Already `Category::Hidden` |
| `branching_support_pillar_diameter` | Branching | Print | **unused for SLA** | the engine reads `branchingsupport_*` (no underscore) at `SLAPrint.cpp:131-153`; the defs created in `ConfigDefsSLA.cpp:1282-1534` are `branching_support_*`, which nothing reads. Already `Category::Hidden` |
| `branching_support_small_pillar_diameter_percent` | Branching | Print | **unused for SLA** | the engine reads `branchingsupport_*` (no underscore) at `SLAPrint.cpp:131-153`; the defs created in `ConfigDefsSLA.cpp:1282-1534` are `branching_support_*`, which nothing reads. Already `Category::Hidden` |
| `branching_support_max_bridges_on_pillar` | Branching | Print | **unused for SLA** | the engine reads `branchingsupport_*` (no underscore) at `SLAPrint.cpp:131-153`; the defs created in `ConfigDefsSLA.cpp:1282-1534` are `branching_support_*`, which nothing reads. Already `Category::Hidden` |
| `branching_support_max_weight_on_model` | Branching | Print | **unused for SLA** | the engine reads `branchingsupport_*` (no underscore) at `SLAPrint.cpp:131-153`; the defs created in `ConfigDefsSLA.cpp:1282-1534` are `branching_support_*`, which nothing reads. Already `Category::Hidden` |
| `branching_support_pillar_connection_mode` | Branching | Print | **unused for SLA** | the engine reads `branchingsupport_*` (no underscore) at `SLAPrint.cpp:131-153`; the defs created in `ConfigDefsSLA.cpp:1282-1534` are `branching_support_*`, which nothing reads. Already `Category::Hidden` |
| `branching_support_buildplate_only` | Branching | Print | **unused for SLA** | the engine reads `branchingsupport_*` (no underscore) at `SLAPrint.cpp:131-153`; the defs created in `ConfigDefsSLA.cpp:1282-1534` are `branching_support_*`, which nothing reads. Already `Category::Hidden` |
| `branching_support_pillar_widening_factor` | Branching | Print | **unused for SLA** | the engine reads `branchingsupport_*` (no underscore) at `SLAPrint.cpp:131-153`; the defs created in `ConfigDefsSLA.cpp:1282-1534` are `branching_support_*`, which nothing reads. Already `Category::Hidden` |
| `branching_support_base_diameter` | Branching | Print | **unused for SLA** | the engine reads `branchingsupport_*` (no underscore) at `SLAPrint.cpp:131-153`; the defs created in `ConfigDefsSLA.cpp:1282-1534` are `branching_support_*`, which nothing reads. Already `Category::Hidden` |
| `branching_support_base_height` | Branching | Print | **unused for SLA** | the engine reads `branchingsupport_*` (no underscore) at `SLAPrint.cpp:131-153`; the defs created in `ConfigDefsSLA.cpp:1282-1534` are `branching_support_*`, which nothing reads. Already `Category::Hidden` |
| `branching_support_base_safety_distance` | Branching | Print | **unused for SLA** | the engine reads `branchingsupport_*` (no underscore) at `SLAPrint.cpp:131-153`; the defs created in `ConfigDefsSLA.cpp:1282-1534` are `branching_support_*`, which nothing reads. Already `Category::Hidden` |
| `branching_support_critical_angle` | Branching | Print | **unused for SLA** | the engine reads `branchingsupport_*` (no underscore) at `SLAPrint.cpp:131-153`; the defs created in `ConfigDefsSLA.cpp:1282-1534` are `branching_support_*`, which nothing reads. Already `Category::Hidden` |
| `branching_support_max_bridge_length` | Branching | Print | **unused for SLA** | the engine reads `branchingsupport_*` (no underscore) at `SLAPrint.cpp:131-153`; the defs created in `ConfigDefsSLA.cpp:1282-1534` are `branching_support_*`, which nothing reads. Already `Category::Hidden` |
| `branching_support_max_pillar_link_distance` | Branching | Print | **unused for SLA** | the engine reads `branchingsupport_*` (no underscore) at `SLAPrint.cpp:131-153`; the defs created in `ConfigDefsSLA.cpp:1282-1534` are `branching_support_*`, which nothing reads. Already `Category::Hidden` |
| `branching_support_object_elevation` | Branching | Print | **unused for SLA** | the engine reads `branchingsupport_*` (no underscore) at `SLAPrint.cpp:131-153`; the defs created in `ConfigDefsSLA.cpp:1282-1534` are `branching_support_*`, which nothing reads. Already `Category::Hidden` |
| `support_preset_light_head_diameter` | light head diameter | Print | **ui-only** | App only: `App/Plater/SlaSupportPointsGizmo.cpp:1764` builds the key as `"support_preset_" + preset_name + "_"` and reads it at `:1767-1778` to seed the support-points dialog; no engine or exporter read |
| `support_preset_light_pillar_diameter` | light pillar diameter | Print | **ui-only** | App only: `App/Plater/SlaSupportPointsGizmo.cpp:1764` builds the key as `"support_preset_" + preset_name + "_"` and reads it at `:1767-1778` to seed the support-points dialog; no engine or exporter read |
| `support_preset_light_base_diameter` | light base diameter | Print | **ui-only** | App only: `App/Plater/SlaSupportPointsGizmo.cpp:1764` builds the key as `"support_preset_" + preset_name + "_"` and reads it at `:1767-1778` to seed the support-points dialog; no engine or exporter read |
| `support_preset_light_base_height` | light base height | Print | **ui-only** | App only: `App/Plater/SlaSupportPointsGizmo.cpp:1764` builds the key as `"support_preset_" + preset_name + "_"` and reads it at `:1767-1778` to seed the support-points dialog; no engine or exporter read |
| `support_preset_medium_head_diameter` | medium head diameter | Print | **ui-only** | App only: `App/Plater/SlaSupportPointsGizmo.cpp:1764` builds the key as `"support_preset_" + preset_name + "_"` and reads it at `:1767-1778` to seed the support-points dialog; no engine or exporter read |
| `support_preset_medium_pillar_diameter` | medium pillar diameter | Print | **ui-only** | App only: `App/Plater/SlaSupportPointsGizmo.cpp:1764` builds the key as `"support_preset_" + preset_name + "_"` and reads it at `:1767-1778` to seed the support-points dialog; no engine or exporter read |
| `support_preset_medium_base_diameter` | medium base diameter | Print | **ui-only** | App only: `App/Plater/SlaSupportPointsGizmo.cpp:1764` builds the key as `"support_preset_" + preset_name + "_"` and reads it at `:1767-1778` to seed the support-points dialog; no engine or exporter read |
| `support_preset_medium_base_height` | medium base height | Print | **ui-only** | App only: `App/Plater/SlaSupportPointsGizmo.cpp:1764` builds the key as `"support_preset_" + preset_name + "_"` and reads it at `:1767-1778` to seed the support-points dialog; no engine or exporter read |
| `support_preset_heavy_head_diameter` | heavy head diameter | Print | **ui-only** | App only: `App/Plater/SlaSupportPointsGizmo.cpp:1764` builds the key as `"support_preset_" + preset_name + "_"` and reads it at `:1767-1778` to seed the support-points dialog; no engine or exporter read |
| `support_preset_heavy_pillar_diameter` | heavy pillar diameter | Print | **ui-only** | App only: `App/Plater/SlaSupportPointsGizmo.cpp:1764` builds the key as `"support_preset_" + preset_name + "_"` and reads it at `:1767-1778` to seed the support-points dialog; no engine or exporter read |
| `support_preset_heavy_base_diameter` | heavy base diameter | Print | **ui-only** | App only: `App/Plater/SlaSupportPointsGizmo.cpp:1764` builds the key as `"support_preset_" + preset_name + "_"` and reads it at `:1767-1778` to seed the support-points dialog; no engine or exporter read |
| `support_preset_heavy_base_height` | heavy base height | Print | **ui-only** | App only: `App/Plater/SlaSupportPointsGizmo.cpp:1764` builds the key as `"support_preset_" + preset_name + "_"` and reads it at `:1767-1778` to seed the support-points dialog; no engine or exporter read |
| `printer_technology` | Printer technology | Printer | **used** | Biz `Biz/Config/ConfigLoad.cpp:360-366` (reads the value out of the stored config) and `ConfigLegacy.cpp:652-707` (pick the FDM or the SLA `ConfigPack` on it). `Category::Hidden` |
| `bed_shape` | Bed shape | Printer | **used** | engine `SLAPrintSteps.cpp:1586` (build-volume check of every object); Biz `Biz/Scene/BedFactory.cpp:19`, `FileLoadingLogic.cpp:360` |
| `bed_custom_texture` | Bed custom texture | Printer | **ui-only** | Biz `Biz/Scene/SceneInteractor.cpp:2664` rebuilds the 3D bed mesh when it changes; App `App/Config/ConfigItemFilePicker.cpp:23` (generic file-picker control). Never read by the SLA engine or an exporter |
| `bed_custom_model` | Bed custom model | Printer | **ui-only** | Biz `Biz/Scene/SceneInteractor.cpp:2663`; App `App/Config/ConfigItemFilePicker.cpp:30`. Never read by the SLA engine or an exporter |
| `elefant_foot_compensation` | Elephant foot compensation | Printer | **used** | engine `SLAPrintSteps.cpp:365`, applied to the first `faded_layers` layers at `:387-391` (the FDD reads in `PrintObjectSlice.cpp:981`, `Layer.cpp:564` are the FFF ones) |
| `thumbnails` | Thumbnails | Printer | **unused for SLA** | no read on an SLA path: `SLAPrint.cpp:766` declares it with `steps({})` and only the FDD GCode path consumes it (`Print.cpp:1581` -> `GCode.cpp:1053`). The exporters *would* write thumbnails (`SL1.cpp:237`, `GooSLA.cpp:271-274`, `AnycubicSLA.cpp:372`) but `SLAResultData::thumbnails` (`SLAResult.hpp:110`) is never filled from this key |
| `thumbnails_format` | Format of G-code thumbnails | Printer | **unused for SLA** | only read by the legacy thumbnail-string parser `Biz/Config/Legacy/PrintConfig.cpp:5278`; `Category::Hidden` |
| `layer_height` | Layer height | Print | **used** | engine `SLAPrintSteps.cpp:353,405,677`, `SLASupportTool.cpp:157`; exporter `AnycubicSLA.cpp:284,509`, `GooSLA.cpp:290`, `SL1.cpp:149` |
| `max_print_height` | Max print height | Printer | **used** | engine `SLAPrintSteps.cpp:1587` (build volume); exporter `AnycubicSLA.cpp:539` |
| `output_filename_format` | Output filename format | Print | **used** | Biz `Biz/ResultExport/ExportNameParser.cpp:213,238` (SLA branch -> `parse_sla_export_name`) |
| `slice_closing_radius` | Slice gap closing radius | Print | **used** | engine `SLAPrintSteps.cpp:712,912,1050`, `SLASupportTool.cpp:163` |
| `slicing_mode` | Slicing Mode | Print | **used** | engine `SLAPrintSteps.cpp:713`, `SLASupportTool.cpp:164` |
| `printer_model` | Printer type | Printer | **used** | engine `SLAPrintSteps.cpp:1418` (`is_prusa_print` picks a different print-time model); exporter `SL1.cpp:157`. `Category::Hidden` |
| `printer_variant` | Printer variant | Printer | **used** | exporter `Biz/ResultExport/SLA/SL1.cpp:158` (`printerVariant` in the printer ini). `Category::Hidden` |
| `printer_notes` | Printer notes | Printer | **unused for SLA** | no read on an SLA path: the only reads are FDD (`ArrangeHelper.cpp:70` gantry geometry, `Legacy/3mf_legacy.cpp:454` ramming keywords). Free-text field, see the caveat below |
| `default_print` | Default Print Preset | Printer | **ui-only** | App preset selection only: `Biz/Preset/PresetInteractor.cpp:1403,1674` picks the default print preset when a printer is selected. `Category::Hidden` |
| `default_material` | Default Material Preset | Print | **ui-only** | App preset selection only: `Biz/Preset/PresetInteractor.cpp:1436,1779`. `Category::Hidden` |

## Greps that were run

One pass over the whole of `src/` for every key in the table, in quoted form
(`"key"`), so dynamic reads via a built key name are listed separately below:

```
grep -rnE -o --include=*.cpp --include=*.hpp --include=*.h --include=*.ini --include=*.json \
     "\"(<all 165 keys joined with |>)\"" src
```

That produced 1288 hits, which were then bucketed by file into: engine SLA files, engine
non-SLA files, `Biz/ResultExport/SLA/*`, `App/*`, `Biz/*` (other), `slic3r-domain` (the
definitions), `src/slic3r/GUI` (not built) and tests. Targeted follow-ups:

- `"branching_support"` vs `"branchingsupport"` over `src/` - the def file and the engine
  disagree on the prefix (see contradictions).
- `"support_preset_"` over `src/` - no literal hit in the App either, because the key is
  built at runtime.
- `light_pwm`, `bottom_`, `wait_`, `lift_`, `retract_` over `src/` and
  `resources/presets/**` - hits only in the defs, the invalidation table, a load test and
  the legacy `PrintConfig.cpp`.
- `min_exposure_time`, `max_initial_exposure_time` over the whole repo - the only hits
  outside `src/` are `resources/presets/community-sla/CommunitySLA/preset-sla-printer.yaml`.
- `"thumbnails"` over `src/` - the only consumer is the FFF GCode path.

## Safe to hide for SLA

Only the `unused for SLA` and `superseded` keys. Hiding must not delete the key: old
presets and `.3mf` files still carry the values, and `ConfigLoad` / `ConfigLegacy` still
know the names.

**Superseded (1)**
- `pad_around_object` - `raft_type` sets it (`Domain/SLA/RaftPreset.cpp:24-46`).

**Unused for SLA - exposure bounds (4, Printer)**
- `min_exposure_time`, `max_exposure_time`, `min_initial_exposure_time`,
  `max_initial_exposure_time`

**Unused for SLA - layer separation mechanics, whole families (19, Material)**
- `lift_height`, `lift_height_2`, `lift_speed_2`, `retract_speed_2`
- `wait_before_lift`, `wait_after_lift`, `wait_after_retract`
- `light_pwm`
- `bottom_lift_height`, `bottom_lift_height_2`, `bottom_lift_speed`, `bottom_lift_speed_2`,
  `bottom_retract_speed`, `bottom_retract_speed_2`, `bottom_wait_before_lift`,
  `bottom_wait_after_lift`, `bottom_wait_after_retract`, `bottom_light_pwm`,
  `bottom_layer_count`

These are 19 settings that look like the SL1 layer-separation knobs and do nothing at all
in this fork: the engine computes peel time from `ExposureProfile`
(`SLAPrintSteps.cpp:1255-1274`, i.e. the tilt/tower/delay family) and the *tilt* times
(`SLAPrintSteps.cpp:1524-1530`). Do **not** hide `lift_speed` and `retract_speed` in the same
sweep: those two keys *are* read by the Elegoo exporter, and `retract_speed` is also an FFF
extruder key.

**Unused for SLA - metadata and output extras (6)**
- `material_notes`, `material_vendor`, `material_source_note`, `printer_notes`
  - see the caveat below before hiding the two notes fields.
- `thumbnails`, `thumbnails_format`

**Unused for SLA - broken "branching" definitions (17, already `Category::Hidden`)**
- `branching_support_head_front_diameter`, `branching_support_head_penetration`,
  `branching_support_head_width`, `branching_support_pillar_diameter`,
  `branching_support_small_pillar_diameter_percent`,
  `branching_support_max_bridges_on_pillar`, `branching_support_max_weight_on_model`,
  `branching_support_pillar_connection_mode`, `branching_support_buildplate_only`,
  `branching_support_pillar_widening_factor`, `branching_support_base_diameter`,
  `branching_support_base_height`, `branching_support_base_safety_distance`,
  `branching_support_critical_angle`, `branching_support_max_bridge_length`,
  `branching_support_max_pillar_link_distance`, `branching_support_object_elevation`

### Caveats for the hiding job

1. **`material_notes` and `printer_notes` are documentation fields, not print settings.**
   Nothing reads them on any path, so they are technically safe to hide, but
   `ConfigDefsSLA.cpp:479-480` states that notes are currently the only way to pass data for
   non-PrusaResearch printers, and M1.13d is about hiding options *that do nothing for the
   print*. Recommend keeping both visible.
2. **`retract_speed` and `lift_speed` must not be hidden.** `retract_speed` is defined twice
   (`ConfigDefsFDM.cpp:3375` and `ConfigDefsSLA.cpp:1087`); a global hide would break FFF.
3. **`thumbnails` is "unused" only because of a wiring gap.** The SL1/Goo/Anycubic exporters
   do write thumbnails (`Biz/ResultExport/SLA/SL1.cpp:237`, `GooSLA.cpp:271-274`,
   `AnycubicSLA.cpp:372`), but `SLAResultData::thumbnails` is never filled from the config
   key. If someone wires it up later this verdict flips back to *used*; a commented-out
   setting is better than a wrong one.
4. **Do not read "declared" as "used".** The invalidation table in `SLAPrint.cpp:615-784`
   lists ~20 of the 46 unused keys with `propagate(slapsMergeSlicesAndEval)`, which makes
   them look alive. They are not read.

## Contradictions with `sla-settings-audit-a/-b/-c.md`

The earlier audit gave no evidence and got several verdicts wrong. In order of importance:

1. **`elefant_foot_compensation`: audit c says "hide - FFF first-layer squish". Wrong.**
   The SLA engine reads it at `src/libslic3r/src/libslic3r/SLAPrintSteps.cpp:365` and applies
   `elephant_foot_compensation()` to the first `faded_layers` layers at
   `SLAPrintSteps.cpp:387-391` (faded in linearly by `efc()` at `:373-375`, width limited by
   `elefant_foot_min_width` read at `:364`). The `ConfigDefsSLA.cpp:43` comment even says it
   "has an override in sla_material_settings". Verdict: **used**. The `PrintObjectSlice.cpp`
   and `Layer.cpp` reads of the same key are the FFF ones - easy to mistake for the SLA path.
2. **`pad_enable` and `pad_around_object`: audit a says "keep" for both.** Half wrong.
   `pad_around_object` is only read in the legacy branch that is skipped when `raft_type` is
   present (`SLAPrint.cpp:75` vs `:90`), and `Domain/SLA/RaftPreset.cpp` derives it from
   `raft_type` - **superseded**. `pad_enable` on the other hand is still a live gate:
   `SLAPrintSteps.cpp:996` (`generate_pad`), `:1038`, `SLAPrint.cpp:1451,1545`,
   `SLASupportTool.cpp:119,240`. Verdict: **used**. Side effect worth reporting separately:
   with `raft_type=none` and `pad_enable=true`, `generate_pad` still runs and builds a
   full-plate raft, because only `make_pad_cfg` reacts to `raft_type` (`:237-239`).
3. **The 17 "branching" settings: audit b lists them as `Hidden` and counts 0 hides.** They
   are hidden, but for the wrong reason and under the wrong name. The def file creates
   `branching_support_*` (`ConfigDefsSLA.cpp:1282-1534`, prefix `std::make_pair("branching", ...)`)
   while the engine reads `branchingsupport_*` - no underscore -
   (`SLAPrint.cpp:131-153`, `SLAPrintSteps.cpp:852`, `SLASupportTool.cpp:188`,
   `Biz/Preset/AbstractConfigManipulation.cpp:393-408`). Nothing reads
   `branching_support_*` at all, so the branching support tree is driven by keys that the
   Domain defs never define. `branching_support_*` are **unused for SLA**; the real
   `branchingsupport_*` keys are the ones worth looking at, in a separate fix.
4. **`min_exposure_time` / `max_exposure_time` / `min_initial_exposure_time` /
   `max_initial_exposure_time`: audit a says "keep - exposure bounds". Wrong.** No read
   anywhere; they are still set in
   `resources/presets/community-sla/CommunitySLA/preset-sla-printer.yaml:19,22`. They are the
   four most convincing candidates to hide, because users assume they clamp the exposure.
5. **`thumbnails`: audit c says "keep - output file thumbnails". Wrong for this fork.** The
   only consumer of the key is the FFF GCode path (`Print.cpp:1581` -> `GCode.cpp:1053`); the
   SLA result never fills `SLAResultData::thumbnails`. **unused for SLA** (see caveat 3).
6. **`material_source_note`: audit b says "unsure - metadata for imported resin profiles".**
   Resolved: nothing reads it and nothing writes it. The importer keeps
   `source_format`/`source_path` in `ForeignResinProfile`
   (`Biz/ResinProfile/ChituboxCfgReader.cpp:185-186`) and never maps them to the key;
   `ConfigLoadTests.cpp:509` only asserts it is empty. **unused for SLA**.
7. **`bed_custom_texture` / `bed_custom_model`: audit c says "hide - FFF cosmetic only".**
   They are cosmetic, but they are not FFF-only and they are not dead: they are read by
   `Biz/Scene/SceneInteractor.cpp:2663-2664` to rebuild the 3D bed mesh and by the generic
   file-picker control `App/Config/ConfigItemFilePicker.cpp:23,30`. Verdict: **ui-only**, so
   they are *not* in the safe-to-hide list - if they should go, that is a product decision,
   not a dead-setting decision.
8. **`material_colour`: audit a says "keep - material visual color".** Nothing reads it. The
   App shows a colour picker for it (`App/Config/ConfigItemControl.cpp:222`) but no code
   applies it; the comment at `App/Theme.cpp:162` ("overridden by material_colour at
   runtime") describes an override that does not exist, and
   `App/Scene/VolumeColor.cpp:107-110` hard-codes the theme colour for SLA models. Verdict:
   **ui-only** (edited, never consumed) - a small bug, not a hide candidate.
9. **The 19 lift/wait/PWM/bottom settings that audit b marks "keep".** They are the biggest
   block of wrong verdicts: `lift_height`, `lift_height_2`, `lift_speed_2`,
   `retract_speed_2`, `wait_before_lift`, `wait_after_lift`, `wait_after_retract`,
   `light_pwm` and the eleven `bottom_*` settings are never read. `SLAPrintSteps.cpp:1524-1549`
   (the non-Prusa separation branch) uses `fast_tilt_time`/`slow_tilt_time`/
   `high_viscosity_tilt_time` instead, which is why they look plausible.
10. **`sla_output_precision` and `printer_variant`: audit a/b say "keep" but for the wrong
    reason.** Both are real reads, yet both are `Category::Hidden` and therefore already
    invisible in the dialog; `printer_variant` only reaches the printer through
    `Biz/ResultExport/SLA/SL1.cpp:158` and `sla_output_precision` only through the SVG
    rasteriser `Format/SL1_SVG.cpp:223`.
