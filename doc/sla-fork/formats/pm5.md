# Anycubic Photon Workshop `.pm5` (format version 517)

The Photon Mono M5s (`.pm5s`) and the M7 Pro (`.pm7`) use the same container, written by the same
writer with a different printer name and format version: see *The `.pm5s` variant* and *The `.pm7`
variant* at the end. Everything below describes the `.pm5`, the one variant a real file has been
read from.

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
`doc/sla-fork/tools/pm5_layout.py --compare` checks it, along with the other layout constants; the
script reads whichever variant of the container it is given.

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

Both level counts stay 16 whatever `gamma_correction` is (M4.13b): they count the levels of the
encoding, which the pw0 encoder always writes as a 4-bit grey, and the table is 16 bytes wide. A
thresholded print uses two of the sixteen. Only the sample has been compared, and it was sliced
with anti-aliasing on, so no value is known for a binary layer.

### LAYERDEF (body at 0x126EC)

u32 layer count (289 in the sample), then one 32-byte entry per layer:

| +Off | Type | Meaning |
|---|---|---|
| 0 | u32 | absolute offset of the layer's image data (confirmed) |
| 4 | u32 | image data length in bytes (confirmed) |
| 8 | f32 | lift height, mm (confirmed: 8.0) |
| 12 | f32 | lift speed (confirmed: 6.0) |
| 16 | f32 | exposure, s (confirmed: 25 for the 5 bottom layers, 2.8 after). The raft interface layers of `sla_raft_interface()` carry `raft_interface_exposure` here; a layer that is both a bottom and an interface layer keeps the bottom exposure |
| 20 | f32 | layer height, mm (confirmed: 0.05) |
| 24 | u32 | number of lit pixels in the layer (confirmed: equals the decoded count on every layer checked) |
| 28 | u32 | 0 (unknown) |

### EXTRA (body at 0x14B20)

Declared 24 bytes but 56 bytes before MACHINE. Values: u32 2, then floats 5, 2, 3, 3, 3, 4, then
u32 2, then floats 2, 2, 2, 6, 4, 6. This looks like two-stage lift and retract settings for bottom
and normal layers (likely), but the grouping is unconfirmed.

### MACHINE (body at 0x14B68)

140 bytes (confirmed: the software block starts right after them). A NUL-padded printer name,
`Anycubic Photon Mono M5` (96 bytes); a NUL-padded layer image format name, `pw0Img` (16 bytes;
the zeros after `pw0Img` are this field's padding, not separate fields); u32 16, u32 7; f32 218.88,
122.88, 200.0 (display width, display height and maximum Z, confirmed against the printer's
specs); u32 517; and 4 bytes `01 47 63 00` (unknown). Whether the printer checks the name or the
image format string is unknown.

An earlier version of this section listed "u32 0, u32 0" before the 16 and 7. That misread the
format field's padding, and a writer following it put 8 extra bytes into MACHINE.

### Software block (0x14BF4, no section header)

164 bytes in total (confirmed: MODEL starts right after). A 32-byte NUL-padded software name
`AC-PC`; a u32 holding the block's own total length, 164 (confirmed by the arithmetic); then 128
bytes of strings in three NUL-padded areas: 32 bytes with the Photon Workshop version and build
date run together (`4.2.0` then `2026-08-31 21:34:28`), 64 bytes with the platform and library
names run together (`win-x64`, `Qt6`, `Cloud4.x`), and 32 bytes with the OpenGL profile
(`3.3-CoreProfile`). Field boundaries inside each area are unknown. A writer should keep the same
three areas and total, with its own identity. Whether the printer reads this block is unknown.

### MODEL (0x14C98)

48 bytes before the first layer image (confirmed). Name `MODEL`, declared length 0, six floats
(the model's bounding box, minimum X, Y, Z and maximum X, Y, Z, in mm; likely), then 8 zero bytes.

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

## The `.pm5s` variant (Anycubic Photon Mono M5s)

**No file has been read from a Photon Mono M5s, so everything in this section is unverified.** The
writer writes the `.pm5` container of this document unchanged, with the per-printer fields below,
which is what the published community descriptions of the Photon Workshop formats support: they
describe one container for the Photon Mono M5 family with the printer named in MACHINE, and nothing
that a newer printer adds a block or changes an encoding.

Field by field, against the `.pm5` sections above:

| Field | `.pm5` (confirmed) | `.pm5s` (unverified) | Written from |
|---|---|---|---|
| File mark 0x0C, format version | 517 | 517 | the variant table, same number as the sample |
| Area count 0x10 | 9 | 9 | fixed |
| The nine address table slots | HEADER, software, PREVIEW, colour table, LAYERDEF, EXTRA, MACHINE, first layer image, MODEL | same | fixed |
| HEADER pixel size +0 | 19.0 um | 19.0 um | the printer profile: 218.88 mm / 11520 px |
| HEADER resolution +44/+48 | 11520 x 5120 | 11520 x 5120 | the printer profile (`display_pixels_x/y`), the same 12K panel as the M5 |
| HEADER grey levels +40 | 16 | 16 | the PW0 encoder, whatever `gamma_correction` is (M4.13b) |
| PREVIEW | 224 x 168, DPI field 120, RGB565 | same | fixed; the thumbnail is written from the slicing result |
| Colour table | 16 entries `0F 1F … FF` | same | fixed |
| LAYERDEF | 4 + 32 bytes per layer | same | fixed, one entry per layer |
| EXTRA | the sample's 56 bytes | same | copied from the sample, meaning still unknown |
| MACHINE +0, printer name (96 bytes) | `Anycubic Photon Mono M5` | `Anycubic Photon Mono M5s` | the variant table; Photon Workshop's own spelling, which no file confirms |
| MACHINE +96, layer image format (16 bytes) | `pw0Img` | `pw0Img` | the encoder that wrote the layers; a printer wanting another name would need it |
| MACHINE +112/+116 | 16, 7 | 16, 7 | copied from the sample, meaning unknown |
| MACHINE display width, height, max Z | 218.88, 122.88, 200.0 mm | same numbers | the printer profile |
| MACHINE +132, version | 517 | 517 | the variant table, same number as the sample |
| Software block | our own identity, same 164 bytes | same | fixed |
| MODEL | 48 bytes, zeros for the bounding box | same | fixed |
| Layer images | PW0 runs, landscape 11520-pixel rows | same | the rasterizer, from the profile's display |

**A real `.pm5s` file has to settle, in this order:** the format version (it is in two places, the
file mark at 0x0C and MACHINE at body +132, and 517 is the M5 sample's number); the printer name
string, which a printer may check and may refuse; the layer image format name; whether the M5s
panel really is 11520 x 5120 with a 19 um pixel; and whether Photon Workshop writes any block for
the M5s that the M5 does not have. Until one of those files exists, the export is experimental.

## The `.pm7` variant (Anycubic Photon Mono M7 Pro)

**No file has been read from a Photon Mono M7 Pro either, so everything in this section is
unverified**, and it is the variant where a wrong guess is most likely: the panel is a different
size from the M5's, so the resolution, the pixel size and the run-length layer images are all
larger, and a printer may reject a file whose resolution is not its own.

| Field | `.pm5` (confirmed) | `.pm7` (unverified) | Written from |
|---|---|---|---|
| File mark 0x0C, format version | 517 | 517 | the variant table, same number as the sample |
| Everything but the fields below | as above | identical block order, declared lengths, encoder, PREVIEW, colour table, EXTRA, software block, MODEL | fixed |
| HEADER pixel size +0 | 19.0 um | 17.0 um | the printer profile: 226.44 mm / 13320 px |
| HEADER resolution +44/+48 | 11520 x 5120 | 13320 x 5120 | the printer profile, the 14K panel |
| MACHINE +0, printer name (96 bytes) | `Anycubic Photon Mono M5` | `Anycubic Photon Mono M7 Pro` | the variant table; no file confirms the spelling |
| MACHINE +96, layer image format (16 bytes) | `pw0Img` | `pw0Img` | the encoder; a 13320-pixel row is longer than 4095, so it still needs the 12-bit runs of grey 0x0 and 0xF, which the encoder writes |
| MACHINE display width, height, max Z | 218.88, 122.88, 200.0 mm | 226.44, 122.88, 230.0 mm | the printer profile |
| Layer images | PW0 runs, landscape rows of `display_pixels_x` pixels | same | the rasterizer |

**A real `.pm7` file has to settle:** the format version; the printer name; whether the panel is
13320 x 5120 with a 17 x 24 um pixel (the vendor page and the shipped preset both say so, and
neither is a spec sheet); whether the M7 Pro needs a block the M5 does not have; and whether its
layer encoding is still PW0 or a newer scheme with more grey levels. The last one is the risk worth
naming: the `.pm5` container's colour table is 16 bytes wide, and a newer printer panel that wanted
more levels would need a wider table, which the sample says nothing about.

## Encryption

Nothing in this fork writes an encrypted file, and nothing here approaches one. The `.pm5` sample is
plain text throughout, and the published community descriptions of the Photon Workshop formats
(`pm5`/`.pm5s`/`.pm7`) describe the same unencrypted container, so no version of it is known to this
work to be encrypted and none is written. This is unlike Chitubox `.ctb`, whose v4 and v5 containers
are encrypted and are not written either (see `ctb.md`). If a printer turns out to want an encrypted
file, that is a decision for the person running the project, not something to work around.
