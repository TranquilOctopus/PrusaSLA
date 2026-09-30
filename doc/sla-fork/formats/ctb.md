# Chitubox `.ctb` (unencrypted v2/v3)

Written from the published open-source descriptions of the unencrypted `.ctb` container
(Chitubox 2 and 3, the files the older Elegoo Mars/Saturn machines take). **No Chitubox-sliced
sample has been compared against this writer**: `doc/sla-fork/ROADMAP.md` M5.3.samples is where one
has to come from, and until then every field below is marked **unverified**. The writer is
`store_ctb()` in `src/slic3r-shared/src/Slic3r/Biz/ResultExport/SLA/CtbSLA.cpp`, the layer encoder
is `CtbSLARasterEncoder` in `src/libslic3r/src/libslic3r/Format/CtbSLA.cpp`, and
`CtbExportTests.cpp` parses a written file field by field with its own reader.

Confidence marks used here:

| Mark | Meaning |
|---|---|
| **unverified** | Taken from the community descriptions of the format. No sample has confirmed it. This is the mark on nearly every field. |
| **self round trip** | `CtbExportTests.cpp` writes a file, reads it back with a separate reader and compares the value, so we know we write what this document says we write. Says nothing about what a printer accepts. |

**Not implemented, on purpose: the encrypted v4/v5 container.** Decrypting, re-deriving or bypassing
the encryption of a foreign file format is out of bounds for this fork (AGENTS.md, "Third-party
content"), and the encrypted variants are not needed for the older Elegoo machines. This writer
only ever writes version 3 with the layer key 0.

All integers are 32-bit **little-endian**, all floats are IEEE 754 single precision, also
little-endian. Text fields are NUL padded to the size in the table.

## Fixed header (offset 0x0000)

| Offset | Type | Written value | Field | Mark |
|---|---|---|---|---|
| 0x0000 | u32 | 3 | format version. 2 and 3 are the unencrypted versions; 4 and 5 are the encrypted ones | unverified |
| 0x0004 | u32 | 0 | reserved | unverified |
| 0x0008 | u32 | 0 | reserved | unverified |
| 0x000C | char[25] | `SLIC3R_VERSION`, NUL padded | version of the software that wrote the file | unverified |
| 0x0025 | char[7] | NUL | padding after the software version | unverified |
| 0x002C | char[25] | `"PrusaSlicer"`, NUL padded | name of the software | unverified |
| 0x0045 | char[7] | NUL | padding after the software name | unverified |
| 0x004C | char[20] | `"YYYY-MM-DD HH:MM:SS"`, NUL padded | export time stamp | unverified |
| 0x0060 | char[4] | NUL | padding after the time stamp | unverified |
| 0x0064 | char[32] | `printer_name` if the config view has that key, otherwise empty | machine the file was sliced for. **This fork's config view has no such key, so the field is written empty**: a model name that nothing in the config states would be a guess | unverified |
| 0x0084 | u32 | 0 | padding after the printer name | unverified |
| 0x0088 | u32 | `display_pixels_x` | display resolution along x | unverified |
| 0x008C | u32 | `display_pixels_y` | display resolution along y | unverified |
| 0x0090 | f32 | 0 | x offset of the display inside the machine, mm | unverified |
| 0x0094 | f32 | 0 | y offset of the display inside the machine, mm | unverified |
| 0x0098 | u32 | `display_mirror_x` as 0/1 | mirror the layer image in x | unverified |
| 0x009C | u32 | `display_mirror_y` as 0/1 | mirror the layer image in y | unverified |
| 0x00A0 | u32 | 2 | number of previews that follow | unverified |

## Previews

Each preview is a 6-word sub-header followed by `width * height * bytes_per_pixel` raw bytes, in
the order of the pixels (top row first, left to right). The writer writes the two below; the
printer's screen shows the first one and a file browser the second.

| Offset in the preview | Type | Value | Field | Mark |
|---|---|---|---|---|
| +0 | u32 | 0 | reserved | unverified |
| +4 | u32 | 800 (preview 0) / 400 (preview 1) | width in pixels | unverified |
| +8 | u32 | 600 / 300 | height in pixels | unverified |
| +12 | u32 | 3 / 1 | bytes per pixel: RGB for the first, gray for the second | unverified |
| +16 | u32 | 0 | x offset in the display | unverified |
| +20 | u32 | 0 | y offset in the display | unverified |
| +24 | u32 | 0 / 1 | type: 0 color, 1 gray | unverified |
| +28 | u8[w*h*bpp] | the plate preview, resampled nearest neighbour from the slice thumbnail | pixels. Black when there is no thumbnail (the app renders one only in a running plater) | unverified |

## Print parameters

The previews are followed by these words, in this order. The units are mm, mm/s and seconds, the
units of the resin settings themselves, so the values go in unchanged. The exposures come from
`exposure_time` / `initial_exposure_time`, the layer height from `sla_effective_layer_height()`, the
bottom layer count from `sla_bottom_layer_count()` and the transition layers from
`sla_effective_faded_layers()`.

| Word | Type | Value written | Field | Mark |
|---|---|---|---|---|
| 0 | f32 | layer height, mm | the height every layer but the first is printed at | unverified |
| 1 | f32 | `exposure_time` | normal exposure, s | unverified |
| 2 | f32 | `initial_exposure_time` | bottom exposure, s | unverified |
| 3 | u32 | `sla_bottom_layer_count()`, clamped to the layer count | number of layers at the bottom exposure | unverified |
| 4 | f32 | 0.5 | light-off time, s. **This format has no setting for it**, so it is the same constant the other writers use | unverified |
| 5 | f32 | `wait_before_lift` | wait before the lift, s | unverified |
| 6 | f32 | `wait_after_lift` | wait after the lift, s | unverified |
| 7 | f32 | `wait_after_retract` | wait after the retract, s | unverified |
| 8 | f32 | `lift_height` | lift distance, mm | unverified |
| 9 | f32 | `lift_speed` | lift speed, mm/s | unverified |
| 10 | f32 | the lift distance | retract distance, mm. **No setting exists for it**, so the plate returns over the distance it was lifted | unverified |
| 11 | f32 | `retract_speed` | retract speed, mm/s | unverified |
| 12 | f32 | `bottom_wait_before_lift` | bottom wait before the lift, s | unverified |
| 13 | f32 | `bottom_wait_after_lift` | bottom wait after the lift, s | unverified |
| 14 | f32 | `bottom_wait_after_retract` | bottom wait after the retract, s | unverified |
| 15 | f32 | `bottom_lift_height` | bottom lift distance, mm | unverified |
| 16 | f32 | `bottom_lift_speed` | bottom lift speed, mm/s | unverified |
| 17 | f32 | the bottom lift distance | bottom retract distance, mm | unverified |
| 18 | f32 | `bottom_retract_speed` | bottom retract speed, mm/s | unverified |
| 19 | u32 | `sla_effective_faded_layers()` | transition layer count | unverified |
| 20 | u32 | `bottom_light_pwm`, 0-255 | bottom light PWM | unverified |
| 21 | u32 | `light_pwm`, 0-255 | light PWM | unverified |
| 22 | u32 | 0 | advance mode | unverified |
| 23 | u32 | the sum over the layers of the exposure and the three waits, rounded | print time, s. The lift and retract moves are not added | unverified |
| 24 | f32 | total material volume, ml | the object and support material of the print statistics | unverified |
| 25 | f32 | total weight, g | volume x the density `bottle_weight` / `bottle_volume` | unverified |
| 26 | f32 | total price | volume x `bottle_cost` / `bottle_volume` | unverified |
| 27-28 | char[8] | `"USD"` | price unit. The table counts 32-bit words, so this field is two of them | unverified |
| 29 | u32 | 1 | anti-aliasing | unverified |
| 30 | u32 | 0 | reserved | unverified |

Word 24 is the only one that needs the print statistics; the rest of the block is the resin and the
print preset. When a caller has no print statistics the three material fields are 0.

## Layer definitions

One 12-word definition per layer, all of them before any layer image, in print order.

| Word | Type | Value written | Field | Mark |
|---|---|---|---|---|
| 0 | u32 | the layer height in um: the first layer at `initial_layer_height` when the resin sets one, the rest at the layer height | height of this layer | unverified |
| 1 | u32 | exposure in ms, the bottom exposure on the bottom layers | exposure of this layer | unverified |
| 2 | u32 | wait before the lift in ms | | unverified |
| 3 | u32 | wait after the lift in ms | | unverified |
| 4 | u32 | wait after the retract in ms | | unverified |
| 5 | u32 | lift distance in um | | unverified |
| 6 | u32 | retract distance in um | the same as the lift distance | unverified |
| 7 | u32 | 0 | volume of this layer in mm3. **Not computed**: the per-layer areas exist (M4.9) but the per-layer volumes are not in the export data | unverified |
| 8 | u32 | the height reached after this layer, in um | total height | unverified |
| 9 | u32 | the length of the layer image in bytes | data size | unverified |
| 10 | u32 | 0 | key of this layer image. **0 means unkeyed**, which is what an unencrypted file is. The encrypted v4/v5 files key their images here, and this writer never does | unverified |
| 11 | u32 | 0 | reserved | unverified |

The um and ms scaling (0.001 of the header's unit) is a choice, not something a sample has
confirmed. It is what gives the integer fields their precision, and it is the first thing to check
against a real `.ctb`.

## Layer images

Right behind the layer definitions, in the same order, the encoded image of each layer, each exactly
`data_size` bytes long.

The encoding is a run-length encoding of the 8-bit gray raster, one byte per pixel, in the order the
rasterizer produced it (the display orientation and the two mirroring settings are already applied
to the pixels, so a reader that honours the header's mirror fields as well would mirror twice):

| Byte | Meaning |
|---|---|
| `0x00`..`0xFD` | a run of (control + 1) pixels, 1 to 254, of the value byte that follows |
| `0xFF` | a literal block: the byte after it is the number of raw pixel bytes (1 to 255) that follow |

A run is closed as soon as the next pixel differs, so a run of two or more equal pixels is always
the two-byte form. A pixel that is alone always goes into a literal block, which is why a lone
`0xFF` pixel costs three bytes and cannot be mistaken for a control. The block ends before a run
starts, so a reader never has to guess where one run of pixels ends and the next begins. There is
no end marker and no checksum: the layer definition carries the byte count, so a reader stops after
`data_size` bytes.

This scheme is **unverified** as well: it is the one the community descriptions give for the layer
images, and `SlaLayerDecoders.hpp` holds the reader the tests use, so the writer and the reader
agree with each other by construction. A real `.ctb` is what settles it.

## End of file

| Type | Value | Field | Mark |
|---|---|---|---|
| u32 | 0 | end-of-file marker | unverified |

Nothing follows the last layer image but this word.

## What is not in this writer

* The encrypted v4/v5 container, and any attempt at its key: out of scope by rule, see the top.
* A `.ctb` reader. The resin reader for `.ctb` presets is M3.8's job and needs a sample too.
* A printer profile for a machine that takes `.ctb`. Nothing is assigned to this format yet: the
  format picker offers `.ctb` as an export type, but no printer preset sets `sla_archive_format:
  ctb`, so the only way to get one is a preset that does.

## Which machines take it

Nothing has been tried, and this fork has no printer that reads a `.ctb` file. What the community
descriptions say, and what a person has to check:

* The **older Elegoo machines** (Mars 2/3 family, Saturn 1/2) are the ones this format was written
  for. Their firmware reads the unencrypted container; whether a given firmware build also wants
  version 3 (and not version 2) is **unverified**.
* The **Anycubic Photon Mono M5** question from ROADMAP M5.3.pw-b is open: check in Chitubox which
  extension it writes for that machine. Its `.pm5` writer exists, so `.ctb` is not needed for it.
* To settle any of it, a file sliced in Chitubox for a known machine is needed (M5.3.samples), and
  then a print. The comparison is field by field, the way `doc/sla-fork/tools/pm5_layout.py` does it
  for `.pm5`.
