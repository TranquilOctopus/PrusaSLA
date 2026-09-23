# SLA Settings Audit: ConfigCommon.cpp

| key | label | category | verdict | reason |
|-----|-------|----------|---------|--------|
| bed_shape | Bed shape | Printer_Bed | keep | Build plate size for SLA |
| bed_custom_texture | Bed custom texture | Printer_Bed | hide | FFF cosmetic only |
| bed_custom_model | Bed custom model | Printer_Bed | hide | FFF cosmetic only |
| elefant_foot_compensation | Elephant foot compensation | Print_PrecisionSlicing | hide | FFF first-layer squish |
| thumbnails | Thumbnails | Printer_General | keep | Output file thumbnails |
| layer_height | Layer height | Print_LayersSurfaces | keep | Core SLA exposure setting |
| max_print_height | Max print height | Printer_General | keep | Build volume Z limit |
| output_filename_format | Output filename format | Print_OutputOptions | keep | Output naming template |
| slice_closing_radius | Slice gap closing radius | Print_PrecisionSlicing | keep | Mesh slicing precision |
| slicing_mode | Slicing Mode | Print_PrecisionSlicing | keep | Hole closing strategy |
| printer_notes | Printer notes | Printer_Notes | keep | Generic printer notes |

Hidden: printer_technology, thumbnails_format, printer_model, printer_variant, default_print, default_material