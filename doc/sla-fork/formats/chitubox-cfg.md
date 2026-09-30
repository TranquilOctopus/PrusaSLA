# Chitubox `.cfg` (resin profile)

**No Chitubox-sliced file has been read by this fork.** Everything below is written from the reader
(`src/slic3r-shared/src/Slic3r/Biz/ResinProfile/ChituboxCfgReader.cpp`), from the mapping table
(`ResinProfileMapper.cpp`) and from the fixtures M3.4 wrote by hand, so it says what this fork
assumes about the format, not what a file was observed to contain. `ROADMAP.md` M3.1 is where a real
file has to come from, and M3.2 is the todo that finishes this document; the section *What a real
file still has to settle* at the end is the list of what is open.

Confidence marks used here:

| Mark | Meaning |
|---|---|
| **unverified** | Taken from the published descriptions of the format and from what the reader and the mapping table have to assume. No sample has confirmed it. This is the mark on nearly every field. |
| **this fork** | A fact about this repository's own code: what the reader does with a file, which is checkable by reading it and is covered by `ChituboxCfgReaderTests.cpp`. |

Nothing here approaches the encryption of any format. The `.cfgx` of Chitubox 2 is a different
container and is not read; see the interoperability rule in `ROADMAP.md` (M3.3) and, when it exists,
`chitubox-cfgx.md`.

## Container

| Property | Value | Mark |
|---|---|---|
| Container | flat text, UTF-8 | unverified |
| One record per line | `key: value`, the key trimmed of whitespace, the value trimmed of it too | unverified |
| Separator | the **first** colon of the line only. A value may contain colons, which is what a G-code block is made of | unverified |
| Comments | a line whose first non-blank character is `#` or `;` is not a record | unverified |
| Blank lines | ignored | unverified |
| Quoted values | a value that opens with `"` and does not close on the same line continues over the following lines, newlines included, until a line ends with `"`. This is how the machine's G-code is stored | unverified |
| Line endings | CRLF and a lone CR are both normalized to LF before parsing | unverified |
| Byte order mark | a UTF-8 BOM at the start of the file is removed, and also before sniffing, so a BOM'd file is not rejected as unrecognized | unverified |
| Size cap | 8 MB, refused above it rather than read (this fork) | **this fork** |
| Encoding of a value | taken verbatim; nothing is evaluated, unescaped or otherwise interpreted | **this fork** |

**Format detection is by content, not by extension** (this fork): `.cfg` is a generic extension, and
`ResinProfileReaderRegistry` picks this reader when one of `normalExposureTime`, `layerHeight` or
`bottomExposureTime` appears as a key in the first 4 KB. A file that only has the other spellings of
those keys (`bottomLayExposureTime`, `bottomLayCount`) is not recognized as a `.cfg`, which is a
known gap and not a format rule.

## The key list

The reader keeps **every** key of the file verbatim in `ForeignResinProfile::raw_values`, so nothing
is lost even where the mapping does not use it. The lists below are the keys this fork has a name
for; a key in none of them still reaches the report, as *Unknown*.

### Keys the reader expects to be numbers

Used only to warn about a value that is not a number; it never fails a read.

`normalExposureTime`, `layerHeight`, `bottomLayerHeight`, `exposureTime`, `bottomExposureTime`,
`lightPWM`, `bottomLightPWM`, `liftDistance`, `bottomLiftDistance`, `liftSpeed`, `bottomLiftSpeed`,
`retractSpeed`, `bottomRetractSpeed`, `restTimeAfterLift`, `restTimeAfterRetract`, `bottomLayers`,
`transitionLayers`, `antiAliasingLevel`, `blurLevel`, `contrast`, `saturation`, `gamma`,
`machineWidth`, `machineHeight`, `machineDepth`, `resolutionX`, `resolutionY`, `pixelSize`,
`offTime`, `bottomOffTime`.

### Keys the mapping table carries

Two spellings of one setting exist where a Chitubox version renamed the key: the first spelling in the
table is the one that is read, and the other one gets a row of its own saying so.

| Chitubox key(s) | PrusaSLA material key | Unit in the file | Unit written | Status | Mark |
|---|---|---|---|---|---|
| `normalExposureTime` | `exposure_time` | s | s | Exact | unverified |
| `bottomLayerExposureTime`, `bottomLayExposureTime` | `initial_exposure_time` | s | s | Exact | unverified |
| `transitionLayers`, `transitionLayerCount`, `fadedLayers`, `fadedLayerCount` | `resin_faded_layers` | a layer count | a layer count | Exact | unverified |
| `bottomLayerCount`, `bottomLayCount` | tilt: `resin_faded_layers` (clamped to 3-20); generic MSLA: `bottom_layer_count` | a layer count | a layer count | Approximated (tilt) / Exact (generic) | unverified |
| `layerHeight` | `resin_layer_height` | mm | mm | Exact | unverified |
| `resinDensity` | `material_density` | g/ml | g/ml | Exact | unverified |
| `resinPrice` with `resinUnit` | `bottle_cost` | a price per litre, or a price per bottle | money per bottle | Converted | unverified |
| `bottleVolume`, `bottle_volume` | read with `resinPrice`, writes nothing | ml | - | Converted | unverified |
| `lightOffTime`, `bottomLightOffTime` | `delay_before_exposure` (the same value above and below the area fill) | s | s | Approximated | unverified |
| `resetTimeBeforeLift` | tilt: `delay_after_exposure`; generic MSLA: `wait_before_lift` | s | s | Approximated (tilt) / Exact (generic) | unverified |
| `resetTimeAfterLift` | generic MSLA: `wait_after_lift`; nothing on a tilt printer | s | s | Not applicable (tilt) / Exact (generic) | unverified |
| `normalLayerLiftHeight`, `liftHeight`, and the `_2` and `bottomLayer...` variants | `lift_height[_2]`, `bottom_lift_height[_2]` | mm | mm | Exact (generic) / Not applicable (tilt) | unverified |
| `normalLayerLiftSpeed`, `liftSpeed`, and the `_2` and `bottomLayer...` variants | `lift_speed[_2]`, `bottom_lift_speed[_2]` | **assumed mm/min** | mm/s | Converted (generic) / Not applicable (tilt) | unverified |
| `normalDropSpeed`, `normalDropSpeed2` | `retract_speed`, `retract_speed_2` | **assumed mm/min** | mm/s | Converted (generic) / Not applicable (tilt) | unverified |
| `normalLightIntensityPWM`, `bottomLightIntensityPWM` | `light_pwm`, `bottom_light_pwm` | a PWM level | a PWM level | Exact (generic) / Not applicable (tilt) | unverified |

The two rows in bold are the ones whose unit this fork only assumes. Every converted speed carries
that caveat in the note of its row, and the conversion is a division by 60.

### Keys that are read but never written to a material preset

- `bAntiAliasing`, `antiAliasLevel`, `bImageBlur`, `minGreyLevel`, `maxGreyLevel`: a preview image
  setting. The grey levels come from the printer calibration here (unverified).
- `resolutionX`, `resolutionY`, `machineWidth`, `machineDepth`, `machineHeight`, `projectType`,
  `machineType`, `machineName`, `printerName`, `name`: a printer hint, used to suggest a printer and
  a preset name (unverified).
- `currProfile`: the profile's own name, which is what the importer suggests the new preset is called
  and what it matches a system resin against (unverified).
- `startGcode`, `layerGcode`, `endGcode`: foreign G-code. It belongs to the machine profile and is
  never imported (unverified).
- any key starting with `displayCorrect` or `buildAreaOffset`: a display or build area correction,
  which belongs to the printer calibration (unverified).
- anything else: reported, never imported.

## Vendor and brand: the format carries none this fork can read

**This is the finding of ROADMAP M3.10e, and it is what the empty vendor field of the import report
means.** It is recorded here rather than guessed at in code:

- No key in the reader's own key table above names a vendor or a brand.
- No row of the mapping table names one either, and the mapped targets are the `sla_material_settings`
  options, which have no key for a vendor: the vendor of a resin preset is not something a foreign
  profile is assumed to know.
- No Chitubox-sliced file has been read (M3.1), so there is no key list from an observed file that a
  vendor key could be missing from.

`ChituboxCfgReader` therefore leaves `ForeignResinProfile::material.material_vendor` unset, and
`ResinImportResult::resin_vendor` is the empty string for every `.cfg`. The review dialog's source
line leaves the vendor out rather than showing an empty field, and the preset that is saved is named
after `currProfile` alone.

**What would change this, and what must not:** if a real file turns out to carry a vendor or brand
key, M3.2 names the key in this document, the reader reads it into `material_vendor`, and
`ChituboxReaderTests` gets a case for the spelling that file uses. What is not allowed is picking a
plausible-looking key name and reading it in the hope that it is the one Chitubox writes: a name that
was guessed at would end up in the vendor field of a saved preset, where a user would take it for
what the file said. Until then the field stays empty, and
`ChituboxCfgReader.hpp` says so next to the reader.

The "New resin from datasheet" form (M3.11) is the other way in and the one that does carry a
vendor: a datasheet is a table of numbers with a name on it, and the form asks for the vendor
explicitly. That is a PrusaSLA field of the form, not a key of this format.

## What a real file still has to settle

The list M3.1 exists to answer, in the order it would be cheapest to check:

1. **Whether there is a vendor or brand key at all**, and its spelling (M3.10e above).
2. **The unit of the motion speeds.** Whether `normalLayerLiftSpeed` and `normalDropSpeed` are mm/min
   or mm/s decides whether every lift and retract speed of every imported profile is out by a factor
   of 60. Compare the file against the values the Chitubox UI shows.
3. **The unit of `resinPrice` and the spellings of `resinUnit`.** A price only becomes a bottle cost
   when the unit says how it is counted: a per-litre price is multiplied by the bottle volume, a
   per-bottle price already is the cost of one bottle, and anything else (a per-kilo price, no unit
   at all) is reported and nothing is written. This fork accepts the usual spellings of the first two
   and refuses the rest.
4. **What `lightOffTime` means.** It is written as a delay before the exposure, above and below the
   area fill, which is an approximation rather than a conversion.
5. **What `resetTimeBeforeLift` and `resetTimeAfterLift` mean** on a tilt printer, where they become a
   delay after the exposure.
6. **Whether a newer version renamed more keys** than the two spellings above, and whether the
   two-stage lift keys of a recent file are spelled the way M3.4's fixture spells them.
7. **Whether `machineName` or `printerName` is the one a profile carries**, and what the printer hint
   should be matched against in the printer list.
8. **The keys a real file has that no list above names.** They would arrive as *Unknown* rows of the
   report, which is the intended way of finding out.
