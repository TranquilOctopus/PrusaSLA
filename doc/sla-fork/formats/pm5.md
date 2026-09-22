# Anycubic Photon Workshop `.pm5` (format version 517)

Observed from one file sliced for the **Anycubic Photon Mono M5** in Photon Workshop (its own
metadata says version 4.2.0, 2026-08-31), provided by the maintainer on 2026-09-22 and kept in the
git-ignored `local-samples/anycubic-photon-mono-m5/`. The sample itself is not committed; this
document records layout only. The container is **not encrypted**.

Confidence marks: **confirmed** = checked against the data (for example decoded pixel counts equal
the resolution); **likely** = consistent with the values seen but not proven; **unknown** = present
but meaning not established. One sample cannot distinguish fields that happened to be zero.

All integers and floats are little-endian, 4 bytes.

## File mark (offset 0)

| Offset | Type | Value in sample | Meaning |
|---|---|---|---|
| 0x00 | char[12] | `ANYCUBIC` + 4 NULs | magic (confirmed) |
| 0x0C | u32 | 517 | format version (confirmed; also repeated in MACHINE) |
| 0x10 | u32 | 9 | number of addresses that follow (confirmed) |
| 0x14 | u32 × 9 | see below | absolute offsets of the blocks |

Address table as found, in order: `0x38` HEADER, `0x14BF4` software block, `0xA4` PREVIEW,
`0x126C0` layer image colour table, `0x126DC` LAYERDEF, `0x14B10` EXTRA, `0x14B58` MACHINE,
`0x14CC8` first layer image, `0x14C98` MODEL. The order of the table is **not** the order in the
file; a writer should fill each slot with the right block's offset.

Physical order of the blocks in the file (confirmed): HEADER, PREVIEW, colour table, LAYERDEF,
EXTRA, MACHINE, software block, MODEL, then the layer images. Write them in this order.
`doc/sla-fork/tools/pm5_layout.py --compare` checks it, along with the other layout constants.

## Named sections

Named sections start with a 12-byte NUL-padded name and a u32 length. The length does **not**
always equal the bytes up to the next block:

| Section | Declared length | Actual span to next block |
|---|---|---|
| HEADER | 92 | 92 |
| PREVIEW | 75292 | 75292 |
| LAYERDEF | 9252 | 9252 (4 + 289 × 32) |
| EXTRA | 24 | 56 |
| MACHINE | 156 | 140, then the unnamed software block begins |
| MODEL | 0 | 32 (bounding box floats follow) |

A writer should reproduce the declared lengths as observed rather than compute them, until a second
sample shows how they are derived.

### HEADER (92 bytes, body at 0x48)

| +Off | Type | Sample | Meaning |
|---|---|---|---|
| 0 | f32 | 19.0 | pixel size in µm (confirmed: 218.88 mm / 11520 px) |
| 4 | f32 | 0.05 | layer height, mm (confirmed) |
| 8 | f32 | 2.8 | normal exposure, s (confirmed against LAYERDEF) |
| 12 | f32 | 0.5 | light-off / wait before cure, s (likely) |
| 16 | f32 | 25.0 | bottom exposure, s (confirmed against LAYERDEF) |
| 20 | f32 | 5.0 | bottom layer count, stored as a float (confirmed: 5 layers at 25 s) |
| 24 | f32 | 8.0 | lift height, mm (confirmed against LAYERDEF) |
| 28 | f32 | 6.0 | lift speed (confirmed against LAYERDEF) |
| 32 | f32 | 6.0 | retract speed (likely) |
| 36 | f32 | 0.407 | resin volume, ml (likely) |
| 40 | u32 | 16 | anti-aliasing grey levels (likely; matches the colour table) |
| 44 | u32 | 11520 | resolution X (confirmed) |
| 48 | u32 | 5120 | resolution Y (confirmed) |
| 52 | f32 | 0 | resin weight, g (likely) |
| 56 | f32 | 0.009 | resin price (likely) |
| 60 | u32 | 36 | currency symbol, `$` (likely) |
| 64 | u32 | 0 | per-layer overrides flag (unknown) |
| 68 | u32 | 3209 | estimated print time, s (likely) |
| 72 | u32 | 10 | transition layer count (likely) |
| 76 | u32 | 0 | transition type (unknown) |
| 80 | u32 | 0 | unknown |
| 84 | u32 | 0x00030000 | unknown |
| 88 | u32 | 10 | unknown |

### PREVIEW (body at 0xB4)

u32 width 224, u32 120 (likely a resolution or DPI field), u32 height 168, then pixel data. The
pixel bytes are 75280, which is 224 × 168 × 2 + 16: 16-bit pixels (likely RGB565) plus 16 bytes
not yet explained.

### Layer image colour table (0x126C0, no section header)

u32 0 (likely "use full greyscale" = off), u32 16 (grey level count), 16 bytes `0F 1F 2F … EF FF`,
u32 0. The 16 levels match HEADER +40.

### LAYERDEF (body at 0x126EC)

u32 layer count (289 in the sample), then one 32-byte entry per layer:

| +Off | Type | Meaning |
|---|---|---|
| 0 | u32 | absolute offset of the layer's image data (confirmed) |
| 4 | u32 | image data length in bytes (confirmed) |
| 8 | f32 | lift height, mm (confirmed: 8.0) |
| 12 | f32 | lift speed (confirmed: 6.0) |
| 16 | f32 | exposure, s (confirmed: 25 for the 5 bottom layers, 2.8 after) |
| 20 | f32 | layer height, mm (confirmed: 0.05) |
| 24 | u32 | number of lit pixels in the layer (confirmed: equals the decoded count on every layer checked) |
| 28 | u32 | 0 (unknown) |

### EXTRA (body at 0x14B20)

Declared 24 bytes but 56 bytes before MACHINE. Values: u32 2, then floats 5, 2, 3, 3, 3, 4, then
u32 2, then floats 2, 2, 2, 6, 4, 6. This looks like two-stage lift and retract settings for bottom
and normal layers (likely), but the grouping is unconfirmed.

### MACHINE (body at 0x14B68)

A NUL-padded printer name, `Anycubic Photon Mono M5` (96 bytes), a NUL-padded layer image format
name, `pw0Img` (16 bytes), u32 0, u32 0, u32 16, u32 7, then f32 218.88, 122.88, 200.0 (display
width, display height and maximum Z, confirmed against the printer's specs), u32 517, and 4 bytes
`01 47 63 00` (unknown). Whether the printer checks the name or the image format string is unknown.

### Software block (0x14BF4, no section header)

A NUL-padded software name `AC-PC`, a u32 164, then strings for the Photon Workshop version and
build date, the platform (`win-x64`), UI and cloud libraries, and the OpenGL profile. A writer
should put its own software identity here. Whether the printer reads this block is unknown.

### MODEL (0x14C98)

Name `MODEL`, declared length 0, then six floats: the model's bounding box, minimum X, Y, Z and
maximum X, Y, Z, in mm (likely).

## Layer images

**Encoding: PW0 run-length (confirmed).** This is the scheme the Anycubic encoder in
`src/libslic3r/src/libslic3r/Format/AnycubicSLA.cpp` already writes, and MACHINE names it
(`pw0Img`). Each byte's high nibble is a 4-bit grey value. For grey 0x0 and 0xF, the low nibble and
the next byte form a 12-bit run length (at most 4095); for other greys, the low nibble is the run
length. Every layer checked decodes to exactly 11520 × 5120 = 58,982,400 pixels.

**Scan order: rows of 11520 pixels, top row first (confirmed).** Read as 11520-pixel rows, layer 0
is a compact 755 × 467 px shape, 79% filled, centred on the plate. Read as 5120-pixel rows, it is a
6%-filled smear. So the image is landscape, and the printer presets must say
`display_orientation: landscape`. They said `portrait`, copied from Prusa's SL1, whose screen is
mounted the other way; that would have produced correctly sized, transposed images.

**Not yet known:** mirroring. The sample's model sits in the middle of the plate, so it cannot show
whether the image is mirrored in X or Y. Settle it with an asymmetric test print (M5.4).
