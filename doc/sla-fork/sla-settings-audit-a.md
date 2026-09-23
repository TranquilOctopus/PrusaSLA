# SLA Settings Audit (part a) — ConfigDefsSLA.cpp:1-760

| key | label | category | verdict | reason |
|-----|-------|----------|---------|--------|
| display_width | Display width | Printer_General | keep | Display size for resin printer |
| display_height | Display height | Printer_General | keep | Display size for resin printer |
| display_pixels_x | X | Printer_General | keep | Display resolution X |
| display_pixels_y | Y | Printer_General | keep | Display resolution Y |
| display_mirror_x | Mirror horizontally | Printer_General | keep | Display mirroring |
| display_mirror_y | Mirror vertically | Printer_General | keep | Display mirroring |
| display_orientation | Display orientation | Printer_General | keep | Display orientation |
| fast_tilt_time | Fast | Printer_General | keep | Tilt time for separation |
| slow_tilt_time | Slow | Printer_General | keep | Tilt time for separation |
| high_viscosity_tilt_time | High viscosity | Printer_General | keep | Tilt time for high viscosity |
| area_fill | Area fill threshold | Filament_MaterialPrintingProfile | keep | Layer separation threshold |
| relative_correction_x | Printer scaling correction in X axis | Printer_General | keep | Printer scaling correction |
| relative_correction_y | Printer scaling correction in Y axis | Printer_General | keep | Printer scaling correction |
| relative_correction_z | Printer scaling correction in Z axis | Printer_General | keep | Printer scaling correction |
| absolute_correction | Printer absolute correction | Printer_General | keep | Polygon inflation correction |
| elefant_foot_min_width | Elephant foot minimum width | Printer_General | keep | Elephant foot compensation |
| zcorrection_layers | Z compensation | Filament_MaterialTemperatures | keep | Cross-layer bleed correction |
| gamma_correction | Printer gamma correction | Printer_General | keep | Display gamma correction |
| material_colour | Color | Filament_MaterialTemperatures | keep | Material visual color |
| initial_layer_height | Initial layer height | Filament_MaterialTemperatures | keep | First layer height |
| bottle_volume | Bottle volume | Filament_MaterialTemperatures | keep | Material cost tracking |
| bottle_weight | Bottle weight | Filament_MaterialTemperatures | keep | Material cost tracking |
| material_density | Density | Filament_MaterialTemperatures | keep | Material cost tracking |
| bottle_cost | Cost | Filament_MaterialTemperatures | keep | Material cost tracking |
| faded_layers | Faded layers | Print_LayersSurfaces | keep | Exposure fade layers |
| min_exposure_time | Minimum exposure time | Printer_General | keep | Exposure bounds |
| max_exposure_time | Maximum exposure time | Printer_General | keep | Exposure bounds |
| exposure_time | Exposure time | Filament_MaterialTemperatures | keep | Layer exposure time |
| min_initial_exposure_time | Minimum initial exposure time | Printer_General | keep | Initial exposure bounds |
| max_initial_exposure_time | Maximum initial exposure time | Printer_General | keep | Initial exposure bounds |
| initial_exposure_time | Initial exposure time | Filament_MaterialTemperatures | keep | First layer exposure |
| material_correction_x | X | Filament_MaterialTemperatures | keep | Material expansion correction |
| material_correction_y | Y | Filament_MaterialTemperatures | keep | Material expansion correction |
| material_correction_z | Z | Filament_MaterialTemperatures | keep | Material expansion correction |
| material_notes | SLA print material notes | Filament_Notes | keep | Material notes |
| supports_enable | Generate supports | Print_Supports | keep | Support generation |
| support_tree_type | Support tree type | Print_Supports | keep | Support strategy |
| support_enforcers_only | Support only in enforced regions | Print_Supports | keep | Support enforcer logic |
| support_points_density_relative | Support points density | Print_Supports | keep | Support density |
| pad_enable | Use raft | Print_Pad | keep | Raft enable |
| pad_wall_thickness | Raft wall thickness | Print_Pad | keep | Raft wall thickness |
| pad_wall_height | Raft height | Print_Pad | keep | Raft cavity height |
| pad_brim_size | Raft expansion | Print_Pad | keep | Raft expansion |
| pad_max_merge_distance | Max merge distance | Print_Pad | keep | Pad merging logic |
| pad_wall_slope | Raft slope | Print_Pad | keep | Raft wall angle |
| pad_around_object | Raft around object | Print_Pad | keep | Raft around object |
| pad_around_object_everywhere | Raft around object everywhere | Print_Pad | keep | Force raft everywhere |
| pad_object_gap | Raft gap to object | Print_Pad | keep | Object-raft gap |
| pad_object_connector_stride | Pad object connector stride | Print_Pad | keep | Connector spacing |
| pad_object_connector_width | Pad object connector width | Print_Pad | keep | Connector width |
| pad_object_connector_penetration | Pad object connector penetration | Print_Pad | keep | Connector penetration |
| raft_type | Raft type | Print_Pad | keep | Raft type selection |
| hollowing_enable | Enable hollowing | Print_Hollowing | keep | Hollowing enable |
| hollowing_min_thickness | Wall thickness | Print_Hollowing | keep | Hollow wall thickness |
| hollowing_quality | Accuracy | Print_Hollowing | keep | Hollowing accuracy |

Hidden: relative_correction, material_type, material_correction, material_vendor

**Counts:** keep=55, hide=0, unsure=0