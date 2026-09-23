# SLA Settings Audit — ConfigDefsSLA.cpp:761-1538

| key | label | category | verdict | reason |
|-----|-------|----------|---------|--------|
| hollowing_closing_distance | Closing distance | Print_Hollowing | keep | Hollowing interior rounding control |
| material_print_speed | Print speed | Filament_MaterialPrintingProfile | keep | Resin viscosity print profile selector |
| sla_archive_format | Format of the output SLA archive | Printer_General | keep | Output file format setting |
| sla_output_precision | SLA output precision | Printer_General | keep | Output resolution in nanometers |
| delay_before_exposure | Delay before exposure | Filament_MaterialPrintingProfile | keep | Layer separation delay before exposure |
| delay_after_exposure | Delay after exposure | Filament_MaterialPrintingProfile | keep | Delay after exposure before separation |
| tower_hop_height | Tower hop height | Filament_MaterialPrintingProfile | keep | Tower raise height for wiping |
| tower_speed | Tower speed | Filament_MaterialPrintingProfile | keep | Tower movement speed selector |
| tilt_down_initial_speed | Tilt down initial speed | Filament_MaterialPrintingProfile | keep | Initial tilt-down speed for separation |
| tilt_down_finish_speed | Tilt down finish speed | Filament_MaterialPrintingProfile | keep | Final tilt-down speed for separation |
| tilt_up_initial_speed | Tilt up initial speed | Filament_MaterialPrintingProfile | keep | Initial tilt-up speed for separation |
| tilt_up_finish_speed | Tilt up finish speed | Filament_MaterialPrintingProfile | keep | Final tilt-up speed for separation |
| use_tilt | Use tilt | Filament_MaterialPrintingProfile | keep | Enable tilt separation mechanism |
| tilt_down_offset_steps | Tilt down offset steps | Filament_MaterialPrintingProfile | keep | Steps for initial tilt-down portion |
| tilt_down_offset_delay | Tilt down offset delay | Filament_MaterialPrintingProfile | keep | Delay at tilt-down offset position |
| tilt_down_cycles | Tilt down cycles | Filament_MaterialPrintingProfile | keep | Number of tilt-down cycles |
| tilt_down_delay | Tilt down delay | Filament_MaterialPrintingProfile | keep | Delay between tilt-down cycles |
| tilt_up_offset_steps | Tilt up offset steps | Filament_MaterialPrintingProfile | keep | Steps for initial tilt-up portion |
| tilt_up_offset_delay | Tilt up offset delay | Filament_MaterialPrintingProfile | keep | Delay at tilt-up offset position |
| tilt_up_cycles | Tilt up cycles | Filament_MaterialPrintingProfile | keep | Number of tilt-up cycles |
| tilt_up_delay | Tilt up delay | Filament_MaterialPrintingProfile | keep | Delay between tilt-up cycles |
| lift_height | Lift height | Filament_MaterialPrintingProfile | keep | Build plate lift height for separation |
| lift_height_2 | Lift height (above area fill) | Filament_MaterialPrintingProfile | keep | Lift height for large cross-sections |
| lift_speed | Lift speed | Filament_MaterialPrintingProfile | keep | Build plate lift speed |
| lift_speed_2 | Lift speed (above area fill) | Filament_MaterialPrintingProfile | keep | Lift speed for large cross-sections |
| retract_speed | Retract speed | Filament_MaterialPrintingProfile | keep | Build plate retract speed after lift |
| retract_speed_2 | Retract speed (above area fill) | Filament_MaterialPrintingProfile | keep | Retract speed for large cross-sections |
| wait_before_lift | Wait before lift | Filament_MaterialPrintingProfile | keep | Delay before lift movement |
| wait_after_lift | Wait after lift | Filament_MaterialPrintingProfile | keep | Delay after lift before retract |
| wait_after_retract | Wait after retract | Filament_MaterialPrintingProfile | keep | Delay after retract before exposure |
| light_pwm | Light PWM | Filament_MaterialPrintingProfile | keep | UV light intensity (0-255) |
| bottom_lift_height | Bottom lift height | Filament_MaterialPrintingProfile | keep | Bottom layer lift height |
| bottom_lift_height_2 | Bottom lift height (above area fill) | Filament_MaterialPrintingProfile | keep | Bottom layer lift for large areas |
| bottom_lift_speed | Bottom lift speed | Filament_MaterialPrintingProfile | keep | Bottom layer lift speed |
| bottom_lift_speed_2 | Bottom lift speed (above area fill) | Filament_MaterialPrintingProfile | keep | Bottom lift speed for large areas |
| bottom_retract_speed | Bottom retract speed | Filament_MaterialPrintingProfile | keep | Bottom layer retract speed |
| bottom_retract_speed_2 | Bottom retract speed (above area fill) | Filament_MaterialPrintingProfile | keep | Bottom retract for large areas |
| bottom_wait_before_lift | Bottom wait before lift | Filament_MaterialPrintingProfile | keep | Bottom layer delay before lift |
| bottom_wait_after_lift | Bottom wait after lift | Filament_MaterialPrintingProfile | keep | Bottom layer delay after lift |
| bottom_wait_after_retract | Bottom wait after retract | Filament_MaterialPrintingProfile | keep | Bottom layer delay after retract |
| bottom_light_pwm | Bottom light PWM | Filament_MaterialPrintingProfile | keep | Bottom layer UV intensity (0-255) |
| bottom_layer_count | Bottom layer count | Filament_MaterialPrintingProfile | keep | Number of bottom layers |
| material_source_note | Material source note | Filament_MaterialTemperatures | unsure | Metadata for imported resin profiles |
| support_head_front_diameter | Default | Print_Supports | keep | Pinhead front diameter for supports |
| support_head_penetration | Default | Print_Supports | keep | Pinhead penetration into model |
| support_head_width | Default | Print_Supports | keep | Pinhead width (sphere center distance) |
| support_pillar_diameter | Default | Print_Supports | keep | Support pillar diameter |
| support_small_pillar_diameter_percent | Default | Print_Supports | keep | Small pillar size percentage |
| support_max_bridges_on_pillar | Default | Print_Supports | keep | Max bridges per pillar |
| support_max_weight_on_model | Default | Print_Supports | keep | Max weight of model-attached subtrees |
| support_pillar_connection_mode | Default | Print_Supports | keep | Pillar bridge connection type |
| support_buildplate_only | Default | Print_Supports | keep | Restrict supports to build plate only |
| support_pillar_widening_factor | Default | Print_Supports | keep | Pillar widening on merge factor |
| support_base_diameter | Default | Print_Supports | keep | Support base diameter |
| support_base_height | Default | Print_Supports | keep | Support base cone height |
| support_base_safety_distance | Default | Print_Supports | keep | Base-to-model minimum distance |
| support_critical_angle | Default | Print_Supports | keep | Default support connection angle |
| support_max_bridge_length | Default | Print_Supports | keep | Maximum bridge length |
| support_max_pillar_link_distance | Default | Print_Supports | keep | Max pillar linking distance |
| support_object_elevation | Default | Print_Supports | keep | Object elevation above supports |

Hidden: branching_support_head_front_diameter, branching_support_head_penetration, branching_support_head_width, branching_support_pillar_diameter, branching_support_small_pillar_diameter_percent, branching_support_max_bridges_on_pillar, branching_support_max_weight_on_model, branching_support_pillar_connection_mode, branching_support_buildplate_only, branching_support_pillar_widening_factor, branching_support_base_diameter, branching_support_base_height, branching_support_base_safety_distance, branching_support_critical_angle, branching_support_max_bridge_length, branching_support_max_pillar_link_distance, branching_support_object_elevation

Summary: keep=65, hide=0, unsure=1