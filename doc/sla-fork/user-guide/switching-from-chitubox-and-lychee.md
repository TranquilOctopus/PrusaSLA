# Switching from Chitubox and Lychee

How to bring a resin profile over from another slicer, what arrives with it, what does not, and
where to look when a value lands somewhere you did not expect. It describes what the current code
does; if something you expect is missing, it is not implemented yet. The rest of the workflow is in
[Getting started](getting-started.md).

## What can be imported today

| Source | File | What happens |
|---|---|---|
| Chitubox 1.x resin profile | `.cfg` | Read and mapped, the whole way |
| Sliced Prusa archive | `.sl1`, `.sl1s` | Read and listed in the report, but see the limitation below |
| Chitubox 2 resin profile | `.cfgx` | Can be picked, no reader recognises it yet |
| Lychee resin profile | `.lyr` | Can be picked, no reader recognises it yet |
| Lychee printer profile | `.lyp` | Not offered anywhere |
| Resin vendor datasheet | none, you type it in | Mapped and reviewed like a file |

### The Chitubox `.cfg`

A `.cfg` is plain text, one `key:value` per line, with quoted values that may run over several
lines (the G-code blocks). The extension means nothing on its own, so the format is recognised by
the content: the first 4 KB are looked for a key that only a `.cfg` has, such as
`normalExposureTime`. Old and new spellings of the same setting are both accepted
(`bottomLayCount` and `bottomLayerCount`, `bottomLayExposureTime` and
`bottomLayerExposureTime`), and a file that carries both is not merged: the first spelling wins and
the other gets a row of its own that says it is a second spelling.

One `.cfg` holds the machine settings and the resin settings together, which is why the table
further down ends with a long list of keys that are only reported.

### A sliced archive, `.sl1` and `.sl1s`

A `.sl1` or `.sl1s` is a zip holding a `config.ini` with the settings of the job and, when the
material profile was embedded, a `prusaslicer.ini` with the whole configuration. Both are read and
every key of both is kept for the report. The two files spell the same setting differently
(`expTime` and `exposure_time`), and the reader resolves that into one set of material settings
before the mapping runs.

**Limitation today.** The mapping table is written in Chitubox key names, so the settings of an
archive are reported rather than written: `expTime`, `expTimeFirst`, `faded_layers`, `numFade` and
`bottom_layer_count` all come out as *Unknown*. Of a sliced archive, only a setting the file
happens to spell the Chitubox way, such as `layerHeight`, carries over today. Treat this format as a
way of seeing what a file contains rather than as a way of loading a resin. If you have exported a
Prusa archive from Lychee, it opens and the same applies to it.

An archive is of any size: only its `config.ini` and `prusaslicer.ini` are opened, a few
kilobytes each, so a job of a few hundred megabytes of layer images reads like a small one. Each
of those two is capped at 1 MB on its own, and an entry that claims to be larger is refused by
name.

### Why `.cfgx` and `.lyr` are not read yet

- There is no reader for either format. Neither structure is publicly documented, and the rule in
  this fork is to look at real files, exported from your own Chitubox and Lychee installs or taken
  from a vendor, before writing a reader, and to write down what is found. That inspection is
  work for a person and has not been done yet, so the readers are still to do.
- The file picker and the drop target already accept both extensions, so you can hand ResinSlicer a
  `.cfgx` or a `.lyr`. The review then reports *Unrecognized resin profile format* and there is
  nothing to save. Nothing is written, and the file is left alone.
- If either format turns out to be encrypted or obfuscated, the fork does not break the encryption.
  The plan is to say so in the format notes under `doc/sla-fork/formats/` and point you at the two
  routes that do work: the sliced-archive fallback above, and the datasheet form below.

## The three ways in, and the way back out

### 1. The Import resin profile button

In **Prepare**, click the **Resin** row in the right-hand sidebar to open the resin picker. Under
the list of resins there are two full-width buttons:

- **Import resin profile...** opens a file dialog filtered to
  *Resin profile files (\*.cfg, \*.cfgx, \*.lyr, \*.sl1, \*.sl1s)*, picks one file, and opens the
  review dialog on it.
- **New resin from datasheet** opens the form described further down.

Both are shown only while a resin printer is selected. `File > Import File` is not involved: the
profile formats are deliberately kept out of the model import, so a `.cfg` dropped in or chosen
there cannot be mistaken for a model.

![TODO screenshot: the resin picker with the Import resin profile and New resin from datasheet buttons at the bottom]()

![TODO screenshot: the file dialog filtered to Resin profile files]()

### 2. Drag and drop

Drop a `.cfg`, `.cfgx` or `.lyr` anywhere on the ResinSlicer window. The first profile of the drop
opens in the review dialog; the model files in the same drop are loaded, and the dialog is the last
thing the drop leaves on the screen, so a model and a profile can be handed over together. Profiles
the drop held besides that one are counted in a warning notification, *3 more resin profiles were not
imported: drop them one at a time*, because the dialog reviews one profile at a time and is modal.

The dialog opens in the view that is on the screen, so dropping a profile while **Preview** is shown
reviews it there. A dropped `.sl1` or `.sl1s` is still loaded as a project, not as a resin profile;
the only way into the review dialog for a sliced archive is the button above.

![TODO screenshot: a .cfg file being dragged onto the window, with the review dialog open on it]()

### 3. The command line

For a folder of profiles, or for a scripted import, the command line does the same work without a
window. It is the same program as the app (`prusa-slicer-launcher`, the `slic3r-app-launcher`
target):

```powershell
# One file, and nothing written: the name and the mapping are reported.
.\prusa-slicer-launcher.exe --import-resin-profile grey.cfg --dry-run

# A whole folder into a chosen printer, with the mapping written to a JSON file.
.\prusa-slicer-launcher.exe --import-resin-profile .\chitubox-profiles --printer-profile "Anycubic Photon Mono M5" --report report.json
```

- `--import-resin-profile` takes a file or a folder. A folder imports every regular file in it,
  sorted by name, at most 1000 files.
- The presets are written into the config container of the selected printer and nowhere else; the
  importer refuses any other target rather than importing into a printer you are not looking at.
  `--printer-profile` is what selects the printer, so pass it explicitly in a script.
- The base material is picked by the import itself: the printer's system resin whose name or vendor
  matches the resin the profile names, and otherwise the printer's own default resin. The base is
  what the new preset inherits from, so the rest of the printer's settings stay sensible.
- `--dry-run` writes nothing at all and still reports the name the import would use and the full
  mapping, which is the quickest way to see what a file would bring.
- `--report <FILE>` writes a JSON document. It is deliberately stable: fixed key order, mapping
  rows sorted by key, file name only and no timestamp, so the same profiles imported from two
  folders give two reports that compare equal.
- One line per file goes to the console, and the exit code is 0 only when every file imported. A
  file that cannot be read fills in its own line and the batch goes on.

The report is one object with a `results` array, one entry per file that was tried, a failed one
included:

```json
{"results":[{"file":"grey.cfg","ok":true,"error":"","preset_name":"Grey resin","base_preset":"Generic Fast Resin","mapping":[{"key":"normalExposureTime","target_key":"exposure_time","source_value":"2.5","source_unit":"s","value":"2.5","target_unit":"s","status":"Exact","note":"Both in seconds, no conversion."}]}]}
```

Each `mapping` row is one key of the file: the key as it appeared (`key`), the resin setting it
becomes (`target_key`, empty when nothing is written), the value the file had (`source_value`) and
the unit it was in (`source_unit`), the value written to it (`value`, empty when none is) and the
unit of that (`target_unit`), then the `status` and the `note` that says why. A converted value is
therefore readable on its own: `150` in `mm/min` as the file had it next to the `2.5` in `mm/s` that
goes into the preset. A key that is not a quantity, such as a layer count or the profile name, has no
unit on either side rather than a guessed one, so read it as the plain number it is.

### Writing a profile back out

For people going the other way, the same mapping runs in reverse and writes a `.cfg` you can open in
Chitubox. It is a command-line action only; there is no button in the app yet:

```powershell
# The resin of the selected printer, written out as a Chitubox .cfg.
.\prusa-slicer-launcher.exe --export-resin-profile "Grey resin" --printer-profile "Anycubic Photon Mono M5" --output grey.cfg
```

- `--export-resin-profile` takes the preset name (or its id) and looks it up among the resins of the
  **selected printer**, so `--printer-profile` decides which presets can be exported at all.
- `--output` names the file, and the command fails without it.
- Which of the two mapping tables it uses follows the printer model and the preset's `use_tilt`, the
  same way the import picks its table, so a preset that goes out and comes back is mapped the same
  way both times. Speeds are converted back to mm/min, and a `[below, above]` area-fill pair is
  written as the single value Chitubox has, with the other named in the console.
- A setting the `.cfg` format has no key for — the bottle cost, the vendor, `use_tilt`, the lift keys
  on a tilt printer — is not written and is named in the console instead, so nothing is dropped
  quietly. A value of zero is not written either, because zero means the setting is unused.
- What the file is not: it is a resin profile, not a print profile. A real `.cfg` also carries the
  machine's G-code, its model and its build volume, and none of that is a resin setting, so open it
  in Chitubox as a resin rather than as a print.

## New resin from datasheet

When there is no profile file at all, the datasheet form is the way in: a vendor datasheet is a
table of numbers, not a file. The form asks for the resin name and vendor, then the values below:
the four almost every datasheet gives are marked *yes*, the rest are optional.

| Field | Required | Rule |
|---|---|---|
| Resin name | yes | becomes the preset name |
| Vendor | no | shown in the review only |
| Layer height (mm) | yes | a number greater than zero |
| Normal exposure (s) | yes | a number greater than zero |
| Bottom exposure (s) | yes | a number greater than zero |
| Number of bottom layers | yes | a whole number greater than zero |
| Light-off delay (s) | no | a number of zero or more, up to the 30 s the setting takes; empty means the datasheet does not state it |
| Price of a bottle | no | a number of zero or more |
| Bottle volume (ml) | no | from the 50 ml the setting takes up; without a bottle size a 1 litre bottle is assumed |
| Lift distance (mm) | no | a number greater than zero; the lift height of a printer that separates layers by lifting |
| Lift speed (mm/min) | no | a number greater than zero; converted to the mm/s the settings hold |
| Retract speed (mm/min) | no | a number greater than zero; the speed the plate drops back down at |
| Number of transition layers | no | a whole number greater than zero; the layers the exposure is faded over |

The last four are what a generic MSLA datasheet may also give, and like the other optional fields an
empty one means the datasheet does not state it. Speeds are asked in mm/min, the unit the datasheets
and the imported profiles both state them in, and the mapper converts them like it converts a
`.cfg`. A printer that separates layers by tilting has no lift, so those rows arrive as *Not
applicable* and a transition layer count instead.

Every field is also checked against the range its resin setting declares, and a number outside it is
refused with a message that names the field and the limit, so nothing is carried into the review
that the setting would not take. The limit is the one of the setting itself, in the unit the setting
holds, so a speed is compared as the mm/s it becomes.

**Next** checks the fields and hands them to the same review dialog, where **Save** or
**Save & select** finishes the job. A value the datasheet does not state is left out of the profile
altogether instead of being written as a zero, so it does not appear in the review table either. A
datasheet gives a price per bottle, which is what the resin preset holds, so the row shows the price
as it was typed and the note says what it works out to per litre.

![TODO screenshot: the New resin from datasheet form filled in from a vendor datasheet]()

## What the review dialog is telling you

The review dialog opens on a dry run, so everything below is what the import *would* do.

- **The source line** names the file, the format that was recognised, and the resin name and vendor
  the file gives. A `.cfg` names no vendor, so that part is empty; a datasheet has no file, so the
  line names the format and the resin only.
- **Target printer** is the selected printer, shown rather than picked: the import writes into that
  printer's config container, so choose the printer in the sidebar before you import. There is no
  printer picker in this dialog.
- **Base material** is the system resin of that printer the new profile inherits from. Changing it
  re-runs the report, because the base says how the printer separates the layers, which decides
  which mapping is used at all.
- **Preset name** is editable. The importer sanitises it (it becomes a file name, so `:` and `/`
  and the other illegal characters become `_`) and makes it unique, so importing the same profile
  twice gives you *Grey resin* and *Grey resin (2)* and never overwrites the first one.
- **The table** has the three columns *From the file*, *Into the resin profile* and *Status*. One
  row per key of the file: the key as the file wrote it with the value it had
  (`normalExposureTime = 2.5 s`), then what it becomes as `exposure_time = 2.5 s`, then a badge. A
  key that writes nothing has an empty second column and is dimmed, and its value is still on the
  left. Under each row is the note that explains the status, which is what makes an approximation
  auditable rather than a surprise; the note is one line long, elided, and hovering it shows all of
  it.
- **The count line** under the table counts the rows of every kind, for instance
  `12 Exact  ·  3 Converted  ·  2 Approximated  ·  40 Not applicable  ·  8 Unknown`. Kinds with no
  rows are left out.
- **Save** saves the profile and keeps whichever resin was selected before. **Save & select** saves
  it and leaves the new profile in the resin slot, which is usually what you want right after an
  import. **Cancel** closes the dialog and writes nothing.

What you end up with is a real user resin profile: it inherits from the base, carries only the
imported values on top of it, and reloads with the bundle like any profile saved from the resin
settings. Everything the base had, including the tilt, area-fill and other printer-specific
settings, is still there. Where it came from is stored with the profile as
`material_source_note`, for example *Chitubox grey.cfg, imported 2026-09-30*, or *Datasheet, entered
2026-09-30* for the form above. That option is hidden, so it does not show in the resin settings
dialog; it is in the profile's own file under the user presets.

![TODO screenshot: the review dialog, with the source line, the base material picker, the preset name and the mapping table]()

## What the badges mean

Every key of the file gets a row and every row gets a status, so nothing is dropped quietly. The
badges are theme colours, read from the palette rather than chosen per row.

| Badge | What it means | Colour token | What to do |
|---|---|---|---|
| **Exact** | copied as it is, no unit change | `AccentPrimary` | nothing |
| **Converted** | the unit or the shape changed, for example mm/min to mm/s, or a per-litre price to a bottle cost | `AccentSecondary` | read the note if the unit assumption is not what your file means |
| **Approximated** | the two programs mean different things by it, and the closest one was chosen | `Warning` | read the note before printing with it |
| **Not applicable** | it does not apply to the target printer, for example a lift height on a printer that separates layers by tilting | `Text`, disabled | nothing, the printer cannot use it |
| **Unknown** | not a ResinSLA resin setting; kept in the report and nothing written | `Text`, disabled | nothing; it is machine or app data, or a spelling the mapping does not know yet |

## What carries over and what does not

The table below is the mapping table of the importer, in
`src/slic3r-shared/src/Slic3r/Biz/ResinProfile/ResinProfileMapper.cpp`, which is what both the review
dialog and the JSON report read. There are two status columns because the mapping is genuinely
different for the two kinds of printer: the Original Prusa SL1 and SL1S separate layers by
**tilting**, and everything else bundled here lifts the build plate. Which column applies is decided
by the printer you import into.

### Written to the resin profile

| Chitubox key(s) | Resin setting | Unit | SL1 / SL1S (tilt) | Generic MSLA |
|---|---|---|---|---|
| `normalExposureTime` | `exposure_time` | s to s | Exact | Exact |
| `bottomLayerExposureTime`, `bottomLayExposureTime` | `initial_exposure_time` | s to s | Exact | Exact |
| `transitionLayers`, `transitionLayerCount`, `fadedLayers`, `fadedLayerCount` | `resin_faded_layers` | count | Exact | Exact |
| `bottomLayerCount`, `bottomLayCount` | SL1: `resin_faded_layers`; MSLA: `bottom_layer_count` | count, clamped to 3-20 on tilt | Approximated | Exact |
| `layerHeight` | `resin_layer_height` | mm to mm | Exact | Exact |
| `resinDensity` | `material_density` | g/ml to g/ml | Exact | Exact |
| `resinPrice` with `resinUnit` | `bottle_cost` | per bottle as it is; per litre x bottle volume / 1000 | Converted | Converted |
| `bottleVolume`, `bottle_volume` | read with `resinPrice`, writes nothing of its own | ml | Converted | Converted |
| `lightOffTime`, `bottomLightOffTime` | `delay_before_exposure` | s, written twice as `v,v` | Approximated | Approximated |
| `resetTimeBeforeLift` | SL1: `delay_after_exposure`; MSLA: `wait_before_lift` | s, `v,v` on tilt | Approximated | Exact |
| `resetTimeAfterLift` | MSLA: `wait_after_lift` | s | Not applicable | Exact |
| `normalLayerLiftHeight`, `liftHeight` | `lift_height` | mm | Not applicable | Exact |
| `normalLayerLiftHeight2`, `liftHeight2` | `lift_height_2` | mm | Not applicable | Exact |
| `bottomLayerLiftHeight`, `bottomLiftHeight` | `bottom_lift_height` | mm | Not applicable | Exact |
| `bottomLayerLiftHeight2`, `bottomLiftHeight2` | `bottom_lift_height_2` | mm | Not applicable | Exact |
| `normalLayerLiftSpeed`, `liftSpeed` | `lift_speed` | mm/min to mm/s | Not applicable | Converted |
| `normalLayerLiftSpeed2`, `liftSpeed2` | `lift_speed_2` | mm/min to mm/s | Not applicable | Converted |
| `bottomLayerLiftSpeed`, `bottomLiftSpeed` | `bottom_lift_speed` | mm/min to mm/s | Not applicable | Converted |
| `bottomLayerLiftSpeed2`, `bottomLiftSpeed2` | `bottom_lift_speed_2` | mm/min to mm/s | Not applicable | Converted |
| `normalDropSpeed` | `retract_speed` | mm/min to mm/s | Not applicable | Converted |
| `normalDropSpeed2` | `retract_speed_2` | mm/min to mm/s | Not applicable | Converted |
| `normalLightIntensityPWM` | `light_pwm` | level | Not applicable | Exact |
| `bottomLightIntensityPWM` | `bottom_light_pwm` | level | Not applicable | Exact |

`resin_layer_height` and `resin_faded_layers` are resin settings, not print settings, so a value
that arrives here overrides the print preset while that resin is selected. That is the point of
them: the layer height belongs to the resin, not to the print.

### Read, but never written to the resin

| Chitubox key(s) | What it is for | Status |
|---|---|---|
| `currProfile` | the suggested preset name, and what the importer looks for among the system resins to pick as the base | Converted |
| `resolutionX`, `resolutionY`, `machineWidth`, `machineDepth`, `machineHeight`, `projectType`, `machineType`, `machineName`, `printerName`, `name` | printer hints: they say which printer the profile was written for, and they are never written to the material | Converted |
| `bAntiAliasing`, `antiAliasLevel`, `bImageBlur`, `minGreyLevel`, `maxGreyLevel` | preview image settings; the grey levels come from the printer calibration here | Not applicable |
| `startGcode`, `layerGcode`, `endGcode` | foreign G-code, which belongs to the machine profile and is never imported | Not applicable |
| any key starting with `displayCorrect` or `buildAreaOffset` | a display or build area correction, which belongs to the printer calibration | Not applicable |
| anything else | reported, never imported | Unknown |

The last row of that table is why a Chitubox `.cfg` produces a long table: one file carries the
machine settings too. These spellings are the ones the reader knows as numbers but the mapping does
not use yet, so they arrive as *Unknown* rather than silently:

`bottomLayerHeight`, `exposureTime`, `bottomExposureTime`, `lightPWM`, `bottomLightPWM`,
`liftDistance`, `bottomLiftDistance`, `retractSpeed`, `bottomRetractSpeed`, `restTimeAfterLift`,
`restTimeAfterRetract`, `bottomLayers`, `offTime`, `bottomOffTime`, `antiAliasingLevel`,
`blurLevel`, `contrast`, `saturation`, `gamma`, `pixelSize`.

### Four things worth knowing about the table

- **The speed unit is an assumption.** Chitubox is taken to write mm/min and the settings want mm/s,
  so every converted speed is divided by 60. The note on each of those rows says so, because the
  unit is not confirmed against a real file yet. If a lift speed looks far too slow or too fast
  after an import, this is the first thing to check.
- **A price becomes a bottle cost when the file says how it is counted.** A per-litre price is
  multiplied by the bottle volume and divided by 1000; a per-bottle price already is the cost of one
  bottle and is written as it is, with the per-litre equivalent of that bottle in the note. A
  per-kilo price, or a file with no unit, is reported and nothing is written. The currency is never
  converted, so the cost estimate stays in the currency of the source profile. Without a
  `bottleVolume` in the file a 1 litre bottle is assumed, and the note says so.
- **On a tilt printer the bottom layer count becomes the transition layer count**, clamped to the
  3 to 20 layers a tilt printer fades the exposure over, because a tilt printer has no block of
  bottom layers. If the file states a transition layer count as well, that one is what lands in
  `resin_faded_layers` and the bottom count gets a row saying it was not written.
- **Two spellings of one setting**: the first spelling of the table is used, and the other row says
  which one was taken.

## Known limits

- A row reads `normalLayerLiftSpeed = 150 mm/min` on the left and `lift_speed = 2.5 mm/s` on the
  right, so a converted value is readable without opening the note. What a row cannot show is the
  unit of a value whose unit is not settled yet (the speeds above) and of a key that is not a
  quantity at all, such as a layer count or a profile name: those are shown as plain numbers.
- The note under a row is a sentence or two, and the table shows it on one line, elided. Hovering
  the line shows all of it.
- A `.cfg` names no vendor, so the vendor is empty in the source line. The resin name comes from the
  profile name key, `currProfile`, and it is that name the base material is matched against. The
  format notes in [chitubox-cfg.md](../formats/chitubox-cfg.md) record that no key of the format
  that has been seen names a vendor or a brand, so nothing is guessed at one. A key that did would
  be kept and shown in the table as *Unknown* rather than dropped, which is how it would be found.
- There is no printer picker in the review dialog. Pick the printer in the sidebar first; the
  import goes into the selected one or it does not happen.
- There is no button for writing a profile back out; that is the `--export-resin-profile` command
  line above, and it carries only what the `.cfg` format has keys for.
- A single text profile (`.cfg`) is capped at 8 MB and a folder import at 1000 files. A sliced
  archive (`.sl1`, `.sl1s`) has no cap on the file: only its two ini entries are read.
- The table shows at most 500 rows, so a `.cfg` with a very long machine section is cut off there.

## See also

- [Getting started](getting-started.md), for the rest of the workflow: printer, resin and supports,
  slicing, and export.
- `doc/sla-fork/ROADMAP.md`, milestone M3, for what is still to do: the `.cfgx` and `.lyr` readers.
- `doc/sla-fork/formats/`, for the notes on foreign file formats.
