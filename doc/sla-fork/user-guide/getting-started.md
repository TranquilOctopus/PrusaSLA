# Getting started

A walkthrough of the ResinSlicer workflow, from an empty project to a file on a USB stick. It
describes what the current code does. If something you expect is missing, it is not implemented
yet.

## 1. Pick a printer, a resin and a supports preset

The right-hand sidebar of the **Prepare** view has three blocks, top to bottom.

**Printer.** Shows the name of the selected printer preset. Click it to open the **Printers**
list — a searchable list with an `Only favorites` filter — and pick a printer. Hovering over the
block gives you *Show info about printer* and *Show extruder settings*; the cog button opens the
same dialog on its settings page. The sheet and nozzle options are hidden for a resin printer.

Bundled resin printers: **Anycubic Photon Mono M5**, Photon Mono M5s, Photon Mono M7 Pro, **Elegoo
Saturn 4 Ultra 12K / 16K**, **Elegoo Mars 5 Ultra**, and the Original Prusa SL1. Every one of them
has an export format the code can write: the Photon Mono M5 writes `.pm5`, the M5s `.pm5s` and the
M7 Pro `.pm7`, the Elegoos `.goo` and the Original Prusa SL1 `.sl1`. The two newer Anycubic files
are experimental and unverified, the same as `.ctb` below.

**Resin.** One row per material slot, each showing the name of the resin preset. Click a row to
open the picker; type filter buttons above it narrow the list to **Tough**, **Flexible**,
**Casting**, **Dental** or **Heat-resistant**. The cog button opens the resin settings, in tabs
named `Resin 1`, `Resin 2`, and so on. That is where **Layer height**, **Transition layers**,
**Exposure time**, **Initial exposure time**, the lift / retract / tilt speeds and wait times, the
material type and colour, and the bottle volume, weight and cost live. Two resins ship with the
fork: **Generic Resin** and **Generic Fast Resin**. Under the list are the two buttons that bring a resin in from elsewhere, **Import resin profile...** and **New resin from datasheet**, both covered in [Switching from Chitubox and Lychee](switching-from-chitubox-and-lychee.md).

**Print settings.** For a resin printer the third block is a **Print settings** button instead of a
dropdown, with a one-line summary under it: the resin name, the layer height and the two exposure
times. The button opens a dialog with two tabs.

*Resin.* A resin dropdown for the first material slot, then **Layer height**, **Exposure time**,
**Initial exposure time**, **Bottom layer count** and **Faded layers**, and the motion block: the
two-stage lift and retract heights and speeds, the waits around them, the light power and the
exposure delays, each with its bottom-layer counterpart. **More resin settings...** opens the full
resin settings, which is where the resin's own **Layer height** and **Transition layers** live.
Both bundled resins set them — 0.05 mm and 3 layers — and a resin that sets them wins over the
print preset, so while one is selected the layer height row is gone from the full print settings
because it would have no effect.

*Supports & raft.* A dropdown with the print presets, **Standard supports** and **Fine
supports**, then **Raft type** and **Object elevation** (mm, 5 by default), which is how far the
supports lift the model, then the four sizes of every support preset — **Mini**, **Light**,
**Medium**, **Heavy**, in that order: tip diameter, pillar diameter, base diameter and base height of
each. Those are the values the preset buttons in the support tool place. **More...** opens the full
print settings: **Generate supports**, **Support tree type**, **Support points density**, **Support
only in enforced regions**, the tip and the stem (**Tip shape**, **Knot diameter**, **Stem sides**,
**Stem taper**), the foot (**Support base shape**, with its diameter and height), the bracing
(**Bracing**, **Brace diameter**, **Brace start height**), where auto support puts its points
(**Minimal distance between support points**, **Overhang angle for support points**) and the **Raft**
group.

The **Raft** group starts with **Raft type** and then shows only the settings that type actually
uses. **None** prints no raft and shows nothing else; **Full plate** (the default) covers the whole
build plate; **Around object** makes a raft that only the object sits on; **Skate** is *Around
object* with half the expansion and a 70° wall slope. There is no *Use raft* switch, the type turns
the raft on and off. A raft around the object also means **Object elevation** is ignored, because
the raft carries the model instead of the supports. Four of the rows below shape a raft that a plain
slab cannot, and each of them defaults to what the raft was before, so a print preset that sets none
of them still prints the raft of the type it names:

- **Raft floor thickness** (mm, 0) is the slab on the build plate the walls stand on, as thick as
  the walls at 0. A thicker floor makes the raft taller without moving its outline, its walls or
  where the object sits.
- **Raft edge taper** (mm, 0) pulls the top edge of the raft in over a 45° bevel, as deep as it is
  wide, which leaves a thin lip to get a spatula under so the raft can be pried off the plate rather
  than cut off it.
- **Raft infill** is None (the default, the solid raft), Grid or Honeycomb, cut out of the inside of
  the raft. **Raft infill spacing** (mm, 2.0), **Raft infill wall** (mm, 0.4) and **Raft infill
  skin** (mm, 0.5) only show while a pattern is selected, since they cut nothing out of a solid
  raft. A solid skin is left under the top face and a solid rim around the pattern, so the object
  never rests on a hole.
- **Raft interface thickness** (mm, 0) makes the top of the raft a band of whole layers of its own,
  and **Raft interface exposure** (s, 0 = the normal exposure) exposes that band with its own time,
  which bonds the object to the raft more strongly. The thickness is rounded to whole layers, a
  layer that is also a bottom layer keeps the bottom exposure, and only the formats with an exposure
  per layer can apply the exposure: `.pm5`, `.pwmx`, `.goo` and `.ctb`. A `.sl1` or `.sl1s` file
  holds one exposure for the whole print, so an interface there prints like every other layer.

## 2. Arrange and orient the models

Import an STL, OBJ, STEP, SVG or 3MF file with **File > Import File**, or drag one into the
window. A finished print can be read back in as a model too: import a `.sl1` or `.sl1s` file and
its geometry is rebuilt from the layer images and placed on the build plate like any other model.
The print settings stored in the archive are not imported yet.

The model lands on the build plate and the tool bar above the viewport shows the editing tools:

| Tool | Key | What it does |
|---|---|---|
| Move | `M` | Drag the model, or use the X/Y/Z arrow handles |
| Rotate | `R` | Drag a ring, or type into the X/Y/Z fields |
| Scale | `S` | Drag the handles |
| Place On Face | `F` | Snaps the model flat onto a face of its own hull |
| Arrange | `Q` | Spreads the models over the build plate |

**Place on build plate** drops a floating model back down onto the plate. The button only appears
while the selection is off the plate.

**Rotate** panel: *Relative rotation* with X, Y and Z fields, the *Auto orient* button, and a
*Coordinates* switch between *Part*, *Object* and *Build plate*. The rings snap to 45° near the
outside and 5° in the middle.

**Auto orient** turns the model so that it sits as low as possible on the build plate, which means
the fewest layers. It only appears for a resin printer and only acts on exactly one selected model.
It rotates the model about its own centre — it does not move the model down, so follow it with
**Place on build plate** if the model ends up floating.

Shift+click adds a model to the selection, `Ctrl+A` selects everything, right-click on empty space
in the viewport clears it.

**Height band**, in the left column of **Prepare**, is shown for a resin printer. Its two sliders,
**Bottom** and **Top** (mm), limit the 3D view to that slice of the model, with the cut faces
capped so the inside stays readable, and **Reset** brings the whole print height back. It clips the
view and the selected model only: the slice and the exported file do not change with it. The panel
stands down while a tool has the view.

## 3. Supports

**SLA Support Points** (`P`) in the tool bar. It is only offered for a resin printer, and only when
one whole model is selected.

The panel, top to bottom. The first block is a **Support point settings** section, open when the tool
opens, and everything below it comes after that section:

- **Support points density** (%) and the four shape sliders — **Head diameter**, **Stem
  diameter**, **Base diameter**, **Base height** — each with a *Use global …* toggle. A toggle on
  means the selected points take the value from the *Supports & raft* preset; a toggle off means
  the slider value is written onto the points you have selected.
- **Tip shape**, a dropdown of Default, Cone and Ball, then **Knot diameter**, **Stem sides** and
  **Stem taper**. These four are per point too, but they have no *Use global* toggle: they are the
  values a newly placed or generated point starts from, set in *Supports & raft*. One field at a
  time is written, so changing the tip shape keeps the knot, the cross-section and the taper the
  selected points already had. They show what the selection holds; when the selected points
  disagree, or nothing is selected, the numbers are blank and the dropdown reads *Mixed*.
- **Mini** / **Light** / **Medium** / **Heavy** set all four shape values at once, and clear the
  four *Use global* toggles. Like the sliders they apply to the selected points only. Their four
  dimensions each live in *Supports & raft*, so the buttons follow that preset.
- **Generate** computes points for the selected model and shows them; **Apply** then writes them to
  the model. **Discard** throws the pending points away and closes the tool. **Auto support all**
  does the whole build plate one model at a time and writes each result as it finishes, so it needs
  no Apply; if some models already have supports it asks whether to keep them and add around them.
- **Lock island supports** protects the points that were placed to catch small floating islands, so
  you cannot move or delete them by accident.
- **Clipping of view** (%) just limits how far into the model you can see, and **Reset** restores
  it.

Two settings in *Supports & raft* decide where **Generate** and **Auto support** put points:
**Minimal distance between support points** (mm, 0 by default) refuses an overhang point that would
land nearer to another one than that, and **Overhang angle for support points** (°, 90 by default,
so every overhang is kept) drops the overhang samples of a surface steeper than the angle from
horizontal. A point placed to catch an island is never filtered by either, and both default to what
the generator did before they existed.

Not yet: a tip diameter as a value of its own, a tip length as a value of its own, and a per-point
*start on model* instead of on the raft or the plate. The **Head diameter** slider is the only way to
give one point its own tip diameter, because neither *Supports & raft* nor a preset has a key for it;
the other two have no control at all, so a point takes the global tip width and the tree decides where
a stem starts.

Editing points in the 3D view: click empty model surface to add a point, click a point to select
and drag it, Shift+click to add to the selection, Shift+drag on empty space to rubber-band select,
right-click or Ctrl+click a point to delete it, and `Ctrl+A` then `Delete` to clear the
selection. Edits are kept as you make them, so closing the tool does not lose them. Auto-generated
points and island points are drawn in different colours from points you placed yourself.

The tool works on a worker thread and never runs a full slice. **Generate** slices the selected
model internally, but only to work out where the support points belong; the support tree and the
raft are then built from those points and drawn. The layer images and the export file still come
from **Slice** in step 4. Before you slice, set **Raft type** and **Object elevation** in the
**Print settings** dialog (section 1).

## 4. Slice

Slicing happens when you press **Slice**, at the bottom of the right-hand sidebar in the **Prepare**
view. The same button reads *Slice all* with more than one build plate, *Add objects to slice* when
nothing is on a plate, *Cancel* while a slice is running, and *Invalid settings* if it failed — in
which case the reason is in the error message and in the dialog the button opens.

Nothing slices by itself. The `Auto-reslice` switch that FFF users see in **Preview** is hidden for
a resin printer, so entering Preview, moving a model or editing supports never starts a slice on
its own. Switching between an FFF printer and a resin printer, or loading a project for a resin
printer, while **Preview** is open is checked as well: the rule is read again whenever the printer,
the presets or the project change, so no switch leaves auto slicing on for a resin build plate.
As soon as anything changes, the existing slice is stale and the button turns back into **Slice**.
A progress notification shows the current step while it runs.

## 5. Preview

The app switches to **Preview** after a successful slice. From here:

- The **layer slider** runs vertically between the 3D view and the right sidebar, with two thumbs
  so you can view a range of layers. Hovering shows the estimated time for that layer. `Up`/`Down`
  step one layer, `Shift` five, `Ctrl` ten, and *Jump to height* (`Shift+G`) goes to a height in mm.
- The right-hand sidebar of **Prepare** shows **Resin** in ml, **Cost**, and **Layers** for the
  selected build plate, each a `—` until it has been sliced. The object list shows **Sliced Info**
  with *Used material* and *Printing time*.
- The **Layer image** window, on the left under the object list, appears as soon as the plate has
  been sliced. It shows the layer exactly as the printer's screen will show it: the same greyscale
  raster the printer gets, rendered with the display size, pixel count, mirroring and orientation
  from the printer preset. The caption gives the layer number, the total and the Z height, and the
  **Previous** / **Next** buttons step one layer, moving the slider's top thumb with them. The 3D
  view and this window follow the slider together, so they always show the same layer.
  - **Click the image** and a **Zoom** panel opens under it: a 256 × 256 pixel crop of the display
    around the point you clicked, at 1:1, so single pixels are visible. It follows the layer while
    it is open, and the button beside *Zoom* closes it.
  - The **Area (mm²)** and **Peel force (N)** charts run over the whole print, with a vertical line
    on the layer you are on, and the values of that layer are written out under them. The peel chart
    names the vat film in its title, *Peel force (N, FEP)*, because the coefficients behind it are
    the film's. **Vat film** (FEP, nFEP, PFA or ACF) is in the printer settings, with
    **Peel force per area**, **Peel force per perimeter** and the **Peel force warning** that turns
    a layer above it into an issue; a layer over it reads *Peel 24.1 N (over 20.0 N)*. The estimate
    is a model, not a measurement: cured area plus boundary length plus the suction of the cups open
    on that layer, with rough coefficients from the literature.
  - Islands on the current layer are ringed on the image, and the **Islands** list under the charts
    gives each one with the model it sits on, its layer, its area and its position, plus a **Go**
    button that jumps to that layer. **Previous island** and **Next island** step to the closest
    island below or above the current layer. A long list shows the first 50 islands, then a line
    reading *and N more*.
- A **Supports** block above the buttons lists the models on the selected build plate with their
  support point counts. **Edit supports** switches to **Prepare**, selects that model and opens the
  support tool on it, so a missing support can be added from where the warning came from.
  **Auto support selected** does the models selected in the 3D view, **Auto support all** every
  printable model on the build plate; both are off while a run is going and both open the support
  tool, which takes over the work. Under them a one-line status says whether every printable model
  has its points, and ends in *Every model has support points. Press Slice.* The block itself never
  slices: **Slice** stays the only thing that does.

The **Issues** list sits just under those three figures, still in **Prepare**, and is filled from the
same slice the layer view reads. One row per island and per cup, `Island on vase, layer 218  4.2 mm²`,
islands before cups, sorted by layer and the biggest first, capped at 20 rows plus *and N more…*. A
click takes the layer view to that layer. A **cup** is a hole in a printed layer that stays a hole
going up and has nothing solid under it, so it pins a vacuum against the film on every peel;
**trapped resin** is a hole closed on every side, which is what a hollow print without a drain hole
leaves. Trapped resin and the layers over the peel-force limit count in the `Issues (N)` header but
have no row of their own yet, and nothing suggests a drain hole for them: drain holes are still
placed by hand in the hollow tool.

Two warnings can pop up after a slice finishes, and both stay until you dismiss them:

- *N islands found on M models, first on layer K. They can fall off during printing.* An island is a
  small area of the layer that touches nothing below it; it will detach during printing. The models
  are the ones the islands could be put on; an island that sits only on a raft is left out, and the
  message then falls back to *N islands found, first on layer K.*
- *Sliced without supports: …* with the model names, followed by *Open the support tool to add
  supports, or ignore this if the model should sit on the build plate.* Ignore it for a model
  printed flat on the plate.

## 6. Export

A resin export runs a checklist first. If the plate has models without supports, or the slice found
islands, a **Check before printing** dialog lists them — *N models has no supports: …* and *N
islands, first on layer M* — under the line *Printing this as is is likely to waste resin. Fix the
problems, or export anyway.* **Export anyway** goes on to the save dialog, **Cancel** goes back.
A plate with nothing to report is not interrupted.

The **Export** button at the bottom of the **Preview** sidebar, with the tooltip *Export print
file*. It only becomes an export button once the slice has finished; if the plate changed since, it
reads **Slice** again. If you reach the export path some other way with an unfinished slice, you get
*Export failed* and *The plate is not fully sliced yet. Press Slice, wait until it finishes, then
export again.*

The file format follows the printer preset. The Anycubic Photon Mono M5 writes `.pm5`, the M5s
`.pm5s` and the M7 Pro `.pm7`, the Elegoo Saturn and Mars write `.goo`, and the Original Prusa SL1
writes `.sl1`. The **Export as** dialog pre-selects the printer's own format, with the other
supported formats (`pwmo`, `pwmx`, `pwms`, `pm5`, `pm5s`, `pm7`, `goo`, `ctb`, `sl1`, `sl1s`,
`sl1svg`) available in the filter dropdown. Choosing an extension that does not match the printer
you sliced for opens a *Different file type* warning and sends you back to choose a name.

**`ctb`, `pm5s` and `pm7` are the experimental ones.** `.ctb` is the unencrypted Chitubox v3
container: it round-trips through this app's own reader, but no printer has read a file written here
and no Chitubox-sliced sample has been compared against, so its layer-definition units are still a
guess, and the encrypted v4/v5 container is neither written nor read. `.pm5s` and `.pm7` are the
Photon Workshop container `.pm5` is, written for the Photon Mono M5s and the M7 Pro: no file either
printer's own slicer wrote has been compared against, so the printer name and the format version in
the file are unverified, and no printer has read one.

**Select Destination**, just above the button, chooses where it goes: **Local Drive**,
**Removable Drive**, **Prusa Connect**, or a print host of yours. *Removable Drive* only appears in
the list while a removable drive is plugged in, and selecting it makes the save dialog start in that
drive. There is no automatic eject: when the export finishes you get an **Export Finished**
notification with **Open folder**, plus an **Eject** button when the file went to a removable drive.

A print host is uploaded to instead of saved into a folder, through **Send print file to printer
host** rather than the save dialog. Only a destination that can read the plate's format takes it:
PrusaLink takes `.sl1`, `.sl1s` and G-code, the SL1 host takes `.sl1` and `.sl1s`, Prusa Connect takes
those of PrusaLink, and the FFF servers — OctoPrint, Moonraker, Duet, FlashAir, AstroBox, Repetier,
MKS — take G-code only. Anything else is refused before a byte leaves, *OctoPrint does not accept
.pm5 files*, through the same failure notification a failed export uses. The upload dialog asks for
the file name, and if it does not fit the plate's format it says what the export dialog says and
offers to choose again. Neither the upload nor a filesystem destination has been tried against real
hardware yet.

One header field is worth knowing about. **Printer gamma correction** in the printer settings is 1
by default, which rasterizes a layer as an 8-bit grey image; 0 is a hard black-and-white raster with
no anti-aliasing. The header now follows it: the `.pwmx` antialiasing flag, the `.goo` anti-aliasing
level and grey level and the `.ctb` flag are written from the setting, so a binary export no longer
claims to be anti-aliased. The `.pm5` field is not the raster's anti-aliasing — it counts the levels
of the run-length encoding, next to a 16-entry colour table — so it stays at 16 either way.

## 7. Check the orientation once per printer

Before trusting a printer, print the orientation test piece once.

```bash
python doc/sla-fork/tools/orientation_test_piece.py
```

Load the generated STL, place it in the middle of the build plate unrotated, and slice and print
it flat without supports. Looking down at the part on the build plate, the **F** must read
correctly with its arms pointing right, the square boss must be at the **back right**, and the bar
at the **front right**. A mirrored F means `display_mirror_x` or `display_mirror_y` is wrong in
the printer preset; a rotated F means the display orientation is wrong. See
[`../orientation-test.md`](../orientation-test.md) for the full table.
