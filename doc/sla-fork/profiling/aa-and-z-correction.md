# Anti-aliasing and Z-correction in the SLA pipeline

M4.13 review (PLAN B8). This is a map of what every writer does with anti-aliasing and
with the Z-side printer corrections, with the file and the config key behind each
decision, plus the tests that now hold the writers to it.

No behaviour changed in this session. The three findings that need a decision are in
[Findings](#findings) at the end: two header fields that do not follow
`gamma_correction`, and one guard that keeps the raft and the supports out of the
printer corrections. None of the three can be settled without a real printer.

Update 2026-09-30 (M4.13b): findings 1 and 2 are now partly changed - the three AA
fields follow `gamma_correction`, still without a sample. Finding 3 is untouched.

## Where the raster comes from

One rasterizer serves every format. `SLAPrint::Steps::rasterize()`
(`src/libslic3r/src/libslic3r/SLAPrintSteps.cpp:1697`) picks an
`ISlaRasterizer` from `sla_archive_format` (`get_output_type`, same file, line 1669)
and hands it the merged, already-corrected layer polygons:

| `sla_archive_format` | rasterizer | encoder | container writer |
|---|---|---|---|
| `sl1`, `sl1s`, `zip` | `create_sl1_rasterizer` (`Format/SL1.cpp:70`) | `PNGRasterEncoder` (`SLA/RasterBase.cpp:17`) | `store_sl1` (`Biz/ResultExport/SLA/SL1.cpp:196`) |
| `sl1svg` | `create_sl1_svg_rasterizer` (`Format/SL1_SVG.cpp:260`) | none (SVG text) | `store_sl1` |
| `pwmo`, `pwmx`, `pwms` | `create_anycubic_rasterizer` (`Format/AnycubicSLA.cpp:115`) | `AnycubicSLARasterEncoder` (same file, line 30) | `store_anycubic` (`Biz/ResultExport/SLA/AnycubicSLA.cpp:367`) |
| `pm5` | `create_anycubic_rasterizer` (reused; see the comment at `SLAPrintSteps.cpp:1737`) | `AnycubicSLARasterEncoder` | `store_pm5` (same file, line 510) |
| `goo` | `create_goo_rasterizer` (`Format/GooSLA.cpp:160`) | `GooSLARasterEncoder` (same file, line 16) | `store_goo` (`Biz/ResultExport/SLA/GooSLA.cpp:251`) |

`sl1svg` is the odd one out: it is vector output, so there is no grey level at all and
nothing below applies to it.

## Anti-aliasing

### The one setting: `gamma_correction`

`gamma_correction` is a printer setting (`ConfigDefsSLA.cpp:255`, range 0..1, default
1.0). All three rasterized formats read it in their constructor and pass it to
`create_raster_grayscale_aa()` (`src/libslic3r/src/libslic3r/SLA/RasterBase.cpp:68`),
which picks the agg gamma function:

| `gamma_correction` | agg gamma | raster |
|---|---|---|
| `0` | `agg::gamma_threshold(.5)` | **binary**: 0 or 255 only, no anti-aliasing |
| anything above 0 | `agg::gamma_power(gamma)` | 8-bit greyscale, gamma-shaped coverage |

The `agg::gamma_none()` branch in the middle of that factory is unreachable: the first
test is `gamma > 0`, so `gamma = 1` (the default) already goes to `gamma_power(1.0)`,
which is the identity, and the `abs(gamma - 1) < 1e-6` test can then only be reached
with a gamma of 0 or less. Same pixels either way, so it is left alone.

So the raster is 8-bit greyscale except at `gamma_correction = 0`, where it is binary.
What the *encoders* then do with those 8 bits differs per format, and that is where the
level counts in the headers come from.

### sl1 PNG: greyscale, no level count anywhere

`PNGRasterEncoder` writes the raster buffer out as a PNG through miniz
(`tdefl_write_image_to_png_file_in_memory`, 1 component), so a layer is an 8-bit
greyscale PNG. `store_sl1` puts those PNGs in the zip and writes `config.ini`
through `fill_iniconf` (`SL1.cpp:144`).

`config.ini` has **no** anti-aliasing or grey-level key at all: `layerHeight`,
`expTime`, `expTimeFirst`, `expUserProfile`, `printerModel`, `printerVariant`,
`fileCreationTimestamp`, `prusaSlicerVersion`, `usedMaterial`, `numFade`, `numSlow`,
`numFast`, `printTime`, `hollow`, `action`, `jobDir`. The printer is expected to read
the PNG's own bit depth, which is 8. Nothing to keep consistent here; the test only
pins that a thresholded layer really comes out binary and an anti-aliased one really
comes out greyscale.

### Anycubic `.pwmo` / `.pwmx` / `.pwms` and `.pm5`: 16 levels, nibble RLE

`AnycubicSLARasterEncoder` (`Format/AnycubicSLA.cpp:30`) throws the low nibble of
every pixel away (`pixel & 0xF0`) and run-length encodes the high nibble: 16 levels,
0..255 in steps of 17. For grey 0 and grey 15 the run length takes two bytes, otherwise
one (see `anycubicsla_get_pixel_span`, line 16, and `doc/sla-fork/formats/pm5.md:140`).

- `.pwmo`/`.pwmx`/`.pwms`: `anycubicsla_format_header` has a `std::uint32_t
  antialiasing` field (line 66) written from `h.antialiasing = sla_raster_anti_aliased(cfg)
  ? 1 : 0` (`Biz/ResultExport/SLA/AnycubicSLA.cpp:319`). That field is a *flag*, not a level
  count, and since M4.13b it says whether the raster was anti-aliased. See
  [finding 1](#finding-1-the-anycubic-antialiasing-flag-does-not-follow-gamma_correction).
- `.pm5`: the field at the same body offset 40 is the level count.
  `PM5_LAYER_COLOR_LEVELS = 16` (`AnycubicSLA.cpp:478`) is written there (line 665)
  and again in the layer colour table (line 717, with the 16-byte `0F 1F 2F .. EF FF`
  ramp). `doc/sla-fork/formats/pm5.md:63` reads the same offset as "anti-aliasing grey
  levels". 16 matches the encoder and is written whatever the raster is, so the header is
  right and the pixel check is the interesting one: every decoded value must be a multiple
  of 17.

### Elegoo `.goo`: 16 levels in the raster, 4 grey bits claimed

`GooSLARasterEncoder` (`Format/GooSLA.cpp:16`) does the same nibble truncation as the
Anycubic one (`pixel_val = (*src) & 0xF0`), with a `0x55` magic byte in front, a grey
run type byte per run (nibble 4..7) and a trailing checksum byte.

`store_goo` writes the two anti-aliasing fields, and both follow the raster:

| field | offset in the header | value | source |
|---|---|---|---|
| `anti_aliasing_level` (int16 BE) | 188 | 1 anti-aliased, 0 thresholded | `GooSLA.cpp:293` |
| `grey_level` (int16 BE) | 190 | 4 bits anti-aliased, 1 bit thresholded | `GooSLA.cpp:295` |
| `blur_level` (int16 BE) | 192 | 0 | `GooSLA.cpp:297` |
| `gray_scale_level` (uint8) | second to last byte | 1 | `GooSLA.cpp:397` |

With the anti-aliasing on, the rasterizer can put 16 distinct greys into a layer and the 4 bits
`grey_level` declares are exactly the nibble the encoder keeps, so the two agree; a thresholded
layer was rasterized to 0 and 255, and the header says no anti-aliasing and one bit. See
[finding 2](#finding-2-the-goo-header-level-count-and-aa-flag-are-constants).

## Z-correction

Two independent things are called "Z correction" in this codebase. They run in
different steps and only one of them is a printer compensation.

### Light-bleed compensation: `zcorrection_layers`

`zcorrection_layers` is a material setting (`ConfigDefsSLA.cpp:245`, default 0). The
engine function is `sla::apply_zcorrection` in
`src/libslic3r/src/libslic3r/SLA/ZCorrection.cpp`, with the algorithm in
`zcorr_detail`:

1. `create_depthmap` (line 29) walks the slices upwards and, for each region, records
   how many layers in a row it has been continuously present. Regions that continue
   from the layer below get `depth + 1`; regions that appear out of nothing (an
   overhang) get depth 0. That is the *only* correct light-bleed data, and it is not
   reachable from outside `zcorr_detail`.
2. `apply_zcorrection(DepthMap&, size_t layers)` (line 80) then deletes every region
   whose depth is below `min(layer_index, layers)`. A feature thinner than `layers`
   layers is removed outright; the rest survives.
3. The grid overload `apply_zcorrection(slices, grid, depth)` (line 19) is the older
   Z-fade form and is only used by `tests/sla_print/sla_zcorrection_tests.cpp`.

This is a subtraction, not an offset: it deletes geometry, and the deleted pixels
never reach the raster. It runs in
`SLAPrint::Steps::apply_printer_corrections` (`SLAPrintSteps.cpp:360`), and only for
the model slices, at the end (line 397):

```cpp
if (o == soModel) { // Z correction applies only to the model slices
    slices = sla::apply_zcorrection(slices, m_print->print_config().get<int>("zcorrection_layers"));
}
```

So supports are never Z-corrected, and the pad is never Z-corrected. At the default
of 0 nothing is removed.

### Elephant foot and the other printer offsets

All three run in `apply_printer_corrections` (`SLAPrintSteps.cpp:360-400`), which is
called twice: after the model is sliced (`slice_model`, line 735) and after the
supports and pad are sliced (`slice_supports`, line 1074). In order:

1. **`absolute_correction`** (`ConfigDefsSLA.cpp:221`, mm, default 0): `offset_ex` on
   every layer of the origin being corrected, both model and support. Inflates or
   deflates by the signed value, an XY offset and not a Z one.
2. **`elefant_foot_compensation`** + **`elefant_foot_min_width`**
   (`ConfigCommon.cpp:83` and `ConfigDefsSLA.cpp:234`, mm, default 0 and 0.2):
   `elephant_foot_compensation` (`libslic3r/src/libslic3r/ElephantFootCompensation.cpp:652`)
   on the first `faded_layers` layers, with the amount ramping down linearly:

   ```cpp
   auto efc = [start_efc, faded_lyrs_efc](size_t pos) {
       return (faded_lyrs_efc - pos) * start_efc / faded_lyrs_efc;
   };
   ```

   `faded_lyrs` is `min(slice_index.size(), sla_effective_faded_layers)` and
   `faded_lyrs_efc` is `max(1, faded_lyrs - 1)`, so layer 0 gets the full setting and
   the last faded layer gets `start_efc / faded_lyrs_efc`, i.e. nearly nothing. The
   function is a *variable* inward offset: features narrower than
   `elefant_foot_min_width` are left alone, wider ones are pulled in by up to the
   compensation, and the transition between the two is smoothed. A contour whose
   bounding box or area is below `min_contour_width_compensated` is returned
   unchanged.
3. **`zcorrection_layers`**, model slices only, as above.

An early return guards the support call (`SLAPrintSteps.cpp:364`):

```cpp
if (o == soSupport && po.m_supportable_mesh && !po.m_supportable_mesh->emesh.vertices().empty())
    return;
```

`m_supportable_mesh->emesh` is filled from the preview mesh before the support-point
step looks at `supports_enable` (line 813), so for any real object it is non-empty
whatever the supports setting, and in practice points 1 and 2 reach the **model
slices only**: the support pillars and the raft are sliced in `slice_supports` into
the same vector as the supports (line 1069) and the whole origin is then skipped.
That is upstream PrusaSlicer's condition verbatim, minus the null check the fork
added in `687672cbdd` (the support-point step can bail out and leave the optional
empty). See [finding 3](#finding-3-the-raft-and-the-supports-never-see-a-printer-correction).

### The faded layers, and why the count matters twice

`sla_effective_faded_layers` (`src/slic3r-domain/src/Slic3r/Domain/SlaLayerHeight.cpp:36`)
resolves the transition layer count: the resin's own `resin_faded_layers` when it is
>= 0, otherwise the print preset's `faded_layers`. One number then drives three
things:

- the exposure fade in `merge_slices_and_eval_stats` (`SLAPrintSteps.cpp:1434`):
  `delta_fade_time = (initial_exposure_time - exposure_time) / (fade_layers_cnt + 1)`,
  and `layer_times += max(exposure_time, initial_exposure_time - i * delta_fade_time)`
  (line 1574);
- the ramp length of the elephant foot compensation above;
- the writers: `transition_layers` in the `.goo` header, `transition_layer_count` in
  both Anycubic headers, and `numFade` in the `.sl1` `config.ini`.
  `sla_bottom_layer_count` (same file, line 43) is the related `+1`: the first layer
  is exposed at `initial_exposure_time` and the fade runs over the transition layers
  on top of it, so the count of layers a file must expose at the bottom exposure is
  `faded_layers + 1` unless the resin states its own `bottom_layer_count`.

## Invalidation, and which keys may move the layer hash

`invalidated_by` / `diff_to_invalidated_steps` (`SLAPrint.cpp:600`ff) decide what
re-runs when a key changes. The relevant entries:

| key | re-runs | moves the layer hash? |
|---|---|---|
| `exposure_time`, `initial_exposure_time` | `slapsMergeSlicesAndEval` | no: exposure is a time, not a shape |
| `faded_layers`, `resin_faded_layers` | `slaposObjectSlice` | no directly; only via the EFC ramp when `elefant_foot_compensation > 0` |
| `elefant_foot_compensation`, `elefant_foot_min_width` | all steps | yes, it rewrites the first layers |
| `absolute_correction` | all steps | yes |
| `zcorrection_layers` | all steps | yes, it deletes geometry |
| `gamma_correction` | all steps | no: the hash is over polygons, and the raster is built after |

`gamma_correction` invalidating every step is deliberate (the raster is produced by
the last one) but it is the reason a layer hash cannot be compared between runs that
differ only in AA: the polygons are identical, the pixels are not.

## Tests

- `src/slic3r-shared/test/Slic3r/Biz/ResultExport/SLA/SlaLayerDecoders.hpp` - decoders
  for the three layer encodings (goo RLE, pw0 RLE, SL1 greyscale PNG), shared by the
  tests below.
- `SlaAntiAliasingTests.cpp` - slices a 20 mm cube with `gamma_correction` 0 and 1,
  exports to each of the four rasterized formats, and checks the header field at the
  offset above plus the decoded pixels: a thresholded layer must be binary, an
  anti-aliased one must not be, and every value must sit on the quantization grid that
  format's encoder actually uses (17 for pw0/pm5, 16 for goo, anything for a PNG).
- `tests/sla_print/sla_layer_hash_tests.cpp` - FNV over the layer polygons, the same
  construction as the benchmark harness
  (`tests/sla_print/sla_benchmark_tests.cpp:468`). Unchanged by `exposure_time`,
  `initial_exposure_time` and `gamma_correction`; changed by `elefant_foot_compensation`
  and `absolute_correction`; and stable across two runs of one config, without which the
  other two assertions would pass on a hash that simply changes every time.

## Findings

### Finding 1: the Anycubic antialiasing flag does not follow `gamma_correction`

`h.antialiasing = 1` (`Biz/ResultExport/SLA/AnycubicSLA.cpp`, `fill_header_and_misc`) used to
be written for every `.pwmo`/`.pwmx`/`.pwms` file, including ones sliced with
`gamma_correction = 0`, where the raster is binary. Reading the field as "the file is
anti-aliased" then lies; reading it as "the format supports anti-aliasing" it is
redundant. There is no sample `.pwmx` in the repository and no spec for the field, so
which reading the firmware uses is unknown.

Update (M4.13b, 2026-09-30): changed to the fix this finding names,
`h.antialiasing = gamma_correction > 0 ? 1 : 0`, through `sla_raster_anti_aliased` in
`SlaAntiAliasing.hpp`, which the `.goo` and `.ctb` writers use as well. A value of 0 says the
file is not anti-aliased, which is what it is; if a firmware reads the field the other way
("this format supports anti-aliasing"), 0 would turn the feature off on a printer that needs
it, and a printer run is what settles it. An export above a gamma of 0 is byte for byte what
it was, and every printer preset in the shipped bundles leaves `gamma_correction` at its
default of 1, so no export from a stock profile moves.

### Finding 2: the `.goo` header level count and AA flag are constants

`anti_aliasing_level = 1` and `grey_level = 4` (`Biz/ResultExport/SLA/GooSLA.cpp`,
`store_goo`) used to be written unconditionally, while the encoder emits up to 16 greys
(`Format/GooSLA.cpp:43`). If `grey_level` counts levels, the header understated the
data by 4x; if it counts bits, 4 was right and the field was not a level count at all.
Either way `anti_aliasing_level = 1` had the same problem as finding 1. There is no
`.goo` sample and no spec in the repository.

Update (M4.13b, 2026-09-30): both fields follow the raster now, on the reading that `grey_level`
is the grey depth in bits: 1 and 4 bits for an anti-aliased layer, 0 and 1 bit for a thresholded
one. A thresholded raster has no intermediate greys, so one bit is what it uses. The other
reading is left open: if `grey_level` turns out to count levels, the anti-aliased value has to
move to 16 as well, which is a one-line change here. The test pins both values and the
pixel-level truth (a thresholded layer is binary, an anti-aliased one has more than two levels),
so a future fix cannot silently move the pixels without moving the header.

### Finding 3: the raft and the supports never see a printer correction

The guard at the top of `apply_printer_corrections` returns for the support origin
whenever `m_supportable_mesh->emesh` is non-empty, which it is for every object that
made it as far as the support-point step (the mesh is filled at `SLAPrintSteps.cpp:813`
before `supports_enable` is read). So `absolute_correction` and the elephant foot
compensation reach the model's first layers and nothing else: the support pillars keep
their full width on the plate and the raft is never pulled in. Whether that is intended
(a pillar's footprint is what holds the object down) or an upstream slip, it is upstream
PrusaSlicer's condition verbatim and changing it would move the layer hash of every
supported print, so it is not changed here. The Z-correction is not affected either way:
it is model-only by its own `if (o == soModel)`.

### Not a finding: pm5

`PM5_LAYER_COLOR_LEVELS = 16` matches the nibble encoder exactly, and
`doc/sla-fork/formats/pm5.md:63` already records the field as the grey level count. It counts the
levels of the encoding, which a binary raster does not change, and the layer colour table beside it
is 16 bytes wide either way, so M4.13b left both writes alone.
