# Display orientation and mirroring per archive format

Which corner of a layer image a model ends up in, per archive writer, for a given
`display_orientation`, `display_mirror_x` and `display_mirror_y`. This is what tells a writer
apart from one that hands the printer a mirrored or a transposed layer; see PLAN C3 in
[../PLAN.md](../PLAN.md).

## The test pattern

`src/slic3r-shared/test/Slic3r/Biz/ResultExport/SLA/SlaArchiveOrientationTests.cpp` slices an
**F of three rectangles** (26 x 32 mm, 6 mm tall) flush with the plate corner at **x = 0, y = 0**,
decodes a middle layer of the result and reports the image corner the pattern landed in. The
pattern is clear of both centre lines (72 mm and 40 mm on the test display), so all four corners
of the image tell it apart, and the spine and the two arms are on known sides, so a mirrored or a
transposed F is recognisable on its own.

Every registered `ISlaArchiveFormat` is covered, not a hand-written list: the test walks the
registry, slices with the format's first extension, and decodes the layer with the decoder for
that format's file data type (PNG for `.sl1`, the viewBox for `.sl1svg`, the PW0 run-length
encoding for `.pwmx`/`.pm5`, the `.goo` run-length encoding). A format registered without a
decoder fails the test. Decoders live in `SlaLayerDecoders.hpp` next to it.

Run it with:

```
build-default\src\slic3r-shared\Release\slic3r-shared-tests.exe "[orientation]"
```

`SlaRasterOrientationTests.cpp` covers the same ground from the other side: it asserts only
relations (which axis each mirror flag flips), so it cannot tell which corner is which.

## How the corner follows from the settings

All the raster paths build `sla::RasterBase::Trafo` from the profile
(`src/libslic3r/src/libslic3r/SLA/RasterBase.hpp`, used by `Format/SL1.cpp`,
`Format/SL1_SVG.cpp`, `Format/AnycubicSLA.cpp` and `Format/GooSLA.cpp`):

- The image origin is its **top left** corner, so `mirror_y` is always set and a profile's
  `display_mirror_y` inverts it. Without `display_mirror_y` the plate's y axis points **up** in
  the image, as it does on the plate: y = 0 lands on the last row and the largest y on the first.
- `display_mirror_x` is taken as it is in landscape and **inverted in portrait**, which also
  swaps the plate's x and y axes.
- Nothing is offset: the raster turns the slice's millimetres into image units with the pixel
  size, so the plate's x = 0 and y = 0 edges are image edges and a model flush with the plate's
  x = 0, y = 0 corner lands in an image corner.

For a pattern flush with that plate corner:

| `display_orientation` | `display_mirror_x` | `display_mirror_y` | Column / row | Corner of the image |
|---|---|---|---|---|
| landscape | off | off | `x` / `height - y` | **bottom left** |
| landscape | on | off | `width - x` / `height - y` | **bottom right** |
| landscape | off | on | `x` / `y` | **top left** |
| landscape | on | on | `width - x` / `y` | **top right** |
| portrait | off | off | `width - y` / `height - x` | **bottom right** |
| portrait | on | off | `y` / `height - x` | **bottom left** |
| portrait | off | on | `width - y` / `x` | **top right** |
| portrait | on | on | `y` / `x` | **top left** |

The first six rows are sliced and checked against the written bytes; the last two are not sliced
(`SlaRasterOrientationTests.cpp` shows that each flag moves the pattern across exactly one image
axis, so they follow from the rows above).

Landscape writes an image as wide as the display, portrait as tall as it. That is asserted too,
because a transposed image is what a wrongly set `display_orientation` produced (the `.pm5`
presets once said `portrait`, see [pm5.md](pm5.md)).

## Per format

"Expected corner" is the corner the F at the plate's x = 0, y = 0 corner lands in with that
profile as it ships in this repo, derived from `RasterBase::Trafo` as above and asserted against
the written bytes by the test. `display_mirror_x` defaults to **on** and `display_mirror_y` to
**off** (`ConfigDefsSLA.cpp`); the profiles below say nothing about mirroring unless noted, so
they inherit those defaults.

No archive we write has been printed on a printer yet, so every row's hardware result is
**Left**: read the printable pattern of [../orientation-test.md](../orientation-test.md) after
printing it and compare. "Result" says what the test asserts for the bytes we write.

| Format | Printer profile in the repo | Orientation | Mirror x / y | Expected corner | Result |
|---|---|---|---|---|---|
| `.sl1`, `.sl1s` (PNG layers) | `sl1` Original Prusa SL1 | portrait | on / off | bottom left | asserted; printer **Left** |
| `.sl1`, `.sl1s` (PNG layers) | `sl1s` Original Prusa SL1S SPEED | portrait | on / off | bottom left | asserted; printer **Left** |
| `.sl1svg` | none | - | - | as the row above | asserted; no profile writes it |
| `.pwmo`, `.pwmx`, `.pwms` (PW0 RLE) | none | - | - | follows the table above | asserted; no profile writes it |
| `.pm5` (PW0 RLE) | `photon_mono_m5` Anycubic Photon Mono M5 | landscape | on (default) / off | bottom right | asserted; printer **Left** |
| `.goo` (goo RLE) | `saturn_4_ultra_12k`, `saturn_4_ultra_16k` Elegoo Saturn 4 Ultra | landscape | on (default) / off | bottom right | asserted; printer **Left** |
| `.goo` (goo RLE) | `mars_5_ultra` Elegoo Mars 5 Ultra | landscape | on (default) / off | bottom right | asserted; printer **Left** |

Notes:

- **All six `community-sla` printer profiles mirror in X**, because they do not set
  `display_mirror_x` and the default is on. Prusa's own profiles mirror in X as well (the SL1S
  SPEED preset says so explicitly, the SL1 by default), which is where the default comes from.
  Nothing in the repo's format notes says whether the Anycubic or Elegoo screens are mirrored, so
  this is unverified: if a print comes out mirrored in x, clear `display_mirror_x` in that
  printer profile. **Left.**
- **Mirroring in `.pm5` is unknown from the sample**: the Photon Workshop file in
  `local-samples/` has its model in the middle of the plate, so it cannot show it
  ([pm5.md](pm5.md), "Not yet known"). The table above records what we write today.
- Which physical corner of the machine the plate's x = 0, y = 0 corner is depends on the
  printer, so the table stays in image coordinates. The printable pattern in
  [../orientation-test.md](../orientation-test.md) is the same idea on the plate: read the F
  after printing and compare it with the corner in the row for the profile used.
- `.sl1svg` does not use `display_pixels_x`/`y` at all but `sla_output_precision` for its
  viewBox, so its image is much larger than the display's pixel grid; the corner is the same,
  because the transform is the same.

## Against a real Photon Workshop file

The rows above are derived from the code, so they can only be as right as the code is. The check
against a real file is `SlaPm5WorkshopOrientationTests.cpp`, which is hidden and local because it
needs the maintainer's own sample, and nothing of that sample is copied into the repository or into
test data:

```
set SLA_LOCAL_SAMPLES=<repo>\local-samples
build-default\src\slic3r-shared\Release\slic3r-shared-tests.exe "[.local]"
```

The test skips itself unless that variable is set. What it does, in order:

1. It reads `anycubic-photon-mono-m5/01_5.pm5` as the container [pm5.md](pm5.md) describes it: the
   address table in the file mark, the HEADER resolution, and one LAYERDEF entry per layer, whose
   +20 offset is the thickness of that layer. The layer images stay on disk; only the ones being
   compared are pulled in, and decoded with the PW0 decoder in `SlaLayerDecoders.hpp`.
2. It loads the `5.stl` the reference is a slice of, slices it with the `photon_mono_m5` printer
   preset of the shipped `community-sla` bundle, and writes the result with the `.pm5` writer. The
   bundle is loaded and the printer selected through the preset interactor, so the mirroring the
   test runs is the one the profile inherits (M5.4c) rather than one the test sets up itself. That
   file is read back with the same reader, and its display has to be the same pixel grid as the
   reference's.
3. Three layers are picked at about 25%, 50% and 75% of the height and matched across the two
   files by the height of their middle rather than by their index, so a difference in how the two
   slicers count their layers does not matter. Each layer becomes a binary mask, where a grey value
   of half the light or more counts as lit, the rule the other decoder users here follow.
4. The two masks are lined up on their centroids, because the two slicers put the model wherever
   they like on the plate, and the intersection over union is reported under identity, a mirror in
   X, a mirror in Y and a turn of 180 degrees. Identity has to be the best of the four; when another
   transform wins, the test names it.

**Not run yet, so there are no numbers here.** Slicing `5.stl` at the M5's 12K display with
supports and a pad on takes minutes, and the maintainer runs it. What a result means:

| Best match | Reading |
|---|---|
| identity | our `display_mirror_x: true` agrees with Photon Workshop for this printer, so the row for `photon_mono_m5` above is what the display expects |
| mirror_x | Photon Workshop does not mirror in X, so `display_mirror_x` should be off in the Photon Mono M5 profile |
| mirror_y | the two disagree about the y axis instead, which points at `display_orientation` or at `display_mirror_y` |
| rotate_180 | both axes are flipped, which is a transposed image, the bug the portrait presets caused |

A low IoU under all four is not an answer about mirroring: it means the two slicers produced
different cross sections (the model was placed or scaled differently), and the comparison says
nothing about the mirroring.
