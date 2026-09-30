# The Prusa SL1 / SL1S archive (`.sl1`, `.sl1s`)

A zip holding the layer images of a job and the settings it was sliced for, written by
`store_sl1()` in `src/slic3r-shared/src/Slic3r/Biz/ResultExport/SLA/SL1.cpp` and registered as
the two export formats **SL1** (grayscale PNG layers) and **SL1_SVG** (SVG layers) in
`SL1Format.cpp`. The extension is the user's choice: `sl1`, `sl1s` and `zip` are the same writer.

**No file sliced by Prusa's own slicer has been compared with this writer, and no printer has
printed one of these files from this fork.** Everything below is therefore what this writer
writes and what its own readers read, not a statement about what a firmware accepts. The one
exception is marked as a fork key: a key the firmware has no name for, which the firmware is
expected to ignore because it reads the keys it knows. That is **unverified** on hardware.

## What the archive holds

| Entry | What it is |
|---|---|
| `config.ini` | the settings of the job, in the lowerCamelCase spelling of the SL1 format, one `key = value` per line |
| `config.json` | the same keys as a JSON object, plus `version` and the `exposure_profile` block (area fill below and above a tilt cycle) |
| `prusaslicer.ini` | the whole configuration of the job, in the legacy ini spelling |
| `prusaslicer.json` | the same configuration as the slicer metadata JSON |
| `<jobDir>%05d.png` / `.svg` | one layer image, numbered from the plate up |
| `thumbnail/thumbnail<W>x<H>.png` | a preview per size, when the job has one |

`<jobDir>` is `jobDir` from `config.ini`: the project name, or the file name of the archive
itself. The keys are written in alphabetical order, because the map behind `to_ini()` is sorted.

## `config.ini`

| Key | Written from | Read by |
|---|---|---|
| `layerHeight` | `sla_effective_layer_height()` | firmware; the resin reader, as the layer height |
| `expTime` | `exposure_time` | firmware; the resin reader, as the exposure time |
| `expTimeFirst` | `initial_exposure_time` | firmware; the resin reader, as the first layer exposure |
| `expUserProfile` | `material_print_speed` (0 fast, 1 slow, 2 custom) | firmware |
| `printerModel`, `printerVariant` | the printer settings | firmware; the resin reader, as a printer hint |
| `fileCreationTimestamp`, `prusaSlicerVersion` | this build | firmware |
| `usedMaterial` | the statistics of the print, in ml | firmware |
| `numFade` | the effective transition layer count | firmware; the resin reader, as the faded layer count |
| `numBottom` | `sla_bottom_layer_count()` | this fork only, see below |
| `numSlow`, `numFast` | the slow and fast layer counts of the statistics | firmware; the resin reader, as the slow and fast layer counts |
| `printTime` | the estimated print time, in s | firmware |
| `hollow` | 1 when the print was hollowed | firmware |
| `action` | always `print` | firmware |
| `jobDir` | the project name or the file name | firmware |

`materialName`, `printProfile` and `printerProfile` exist in the format and are commented out in
the writer, because nothing in this fork's config view names the resin or the profile the job was
sliced from. The raft interface exposure (`raft_interface_exposure`) has nowhere to go either:
the format has one exposure for the whole print and one for its first layer.

## The bottom layer count

**The SL1 container has no key for it.** Prusa's own writer records none, and the format has no
room for one: an SL1 job is exposed layer by layer from the bottom up, one exposure for the first
layer and one for the rest, so the bottom layers are not a block the file states. `numFade` is
**not** that count, and must not be used as it: `numFade` is the number of layers over which the
exposure is faded in, which `SlicedArchiveResinReader` reads as `faded_layer_count`.

So the count the print actually used (`Domain::sla_bottom_layer_count()`, which is
`bottom_layer_count` when the resin states one and the transition count plus one otherwise) is
written under a key of this fork's, in both metadata files:

| File | Key | Written by | Notes |
|---|---|---|---|
| `config.ini` | `numBottom` | `fill_iniconf()` | next to `numFade`, `numSlow` and `numFast`, so it reads as one of the counts of the printer config |
| `prusaslicer.ini` | `bottom_layer_count` | `store_sl1()` | appended, because the legacy serialization behind that file has no such key. The line is only added when the text does not state the key already |

`SlicedArchiveResinReader` reads `bottom_layer_count` first and `numBottom` second, so an archive
written here comes back with the same count whether or not it has an embedded profile. The
round trip is pinned by `Sl1ImportTests.cpp` ("SL1 archive keeps the bottom layer count"), which
slices, exports and reads the archive back; the `numBottom` spelling on its own is pinned by
`SlicedArchiveResinReaderTests.cpp`.

## Readers

- `SlicedArchiveResinReader` (`Biz/ResinProfile/`): the two ini files, for a resin profile
  (ROADMAP M3.8). It extracts those two entries and no layer image, so the size of the archive
  does not matter to it.
- `import_sl1_archive()` (`Biz/ResultExport/SLA/SL1Import.cpp`): the same two files plus the
  layer images, to bring a print back into the app as a mesh (ROADMAP M5.1a2).

Both read only the entries they need and cap what they read per entry, so a job of a few hundred
megabytes reads like a small one.

## See also

- [ctb.md](ctb.md), [pm5.md](pm5.md): the other archive formats this fork writes.
- [orientation.md](orientation.md): where a model lands in a layer image, per format.
- [../profiling/aa-and-z-correction.md](../profiling/aa-and-z-correction.md): the layer encoding
  of this format (8-bit grayscale PNG, no anti-aliasing key in `config.ini`).
