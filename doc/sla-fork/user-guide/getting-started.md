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
Saturn 4 Ultra 12K / 16K**, **Elegoo Mars 5 Ultra**, and the Original Prusa SL1. Today only the
Photon Mono M5 (`.pm5`) and the Elegoo (`.goo`) presets have an export format the code can write;
selecting the M5s or M7 Pro fails at the end of the slice with *Unsupported output format.*

**Resin.** One row per material slot, each showing the name of the resin preset. Click a row to
open the picker; type filter buttons above it narrow the list to **Tough**, **Flexible**,
**Casting**, **Dental** or **Heat-resistant**. The cog button opens the resin settings, in tabs
named `Resin 1`, `Resin 2`, and so on. That is where **Exposure time**, **Initial exposure time**,
the lift / retract / tilt speeds and wait times, the material type and colour, and the bottle
volume, weight and cost live. Two resins ship with the fork: **Generic Resin** and **Generic Fast
Resin**.

**Supports & raft.** A dropdown with the print presets plus a cog button for the settings. The
presets are `0.05mm Standard` and `0.03mm Detail`. In the settings you find **Generate supports**,
**Support tree type**, **Support points density**, **Support only in enforced regions**, the
**Object elevation** and **Raft type** options, and the rest of the raft and hollowing group.

## 2. Arrange and orient the models

Import an STL, OBJ, STEP or 3MF, or drag one in. The model lands on the build plate and the tool
bar above the viewport shows the editing tools:

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

## 3. Supports

**SLA Support Points** (`P`) in the tool bar. It is only offered for a resin printer, and only when
one whole model is selected.

The panel, top to bottom:

- **Support points density** (%) and the four shape sliders — **Head diameter**, **Stem
  diameter**, **Base diameter**, **Base height** — each with a *Use global …* toggle. A toggle on
  means the selected points take the value from the *Supports & raft* preset; a toggle off means
  the slider value is written onto the points you have selected. The last slider, **Clipping of
  view** (%), just limits how far into the model you can see, and **Reset** restores it.
- **Light** / **Medium** / **Heavy** set all four shape values at once, and clear the four *Use
  global* toggles. Like the sliders they apply to the selected points only.
- **Generate** computes points for the selected model and shows them; **Apply** then writes them to
  the model. **Discard** throws the pending points away and closes the tool. **Auto support all**
  does the whole build plate one model at a time and writes each result as it finishes, so it needs
  no Apply; if some models already have supports it asks whether to keep them and add around them.
- **Lock island supports** protects the points that were placed to catch small floating islands, so
  you cannot move or delete them by accident.

Editing points in the 3D view: click empty model surface to add a point, click a point to select
and drag it, Shift+click to add to the selection, Shift+drag on empty space to rubber-band select,
right-click or Ctrl+click a point to delete it, and `Ctrl+A` then `Delete` to clear the
selection. Edits are kept as you make them, so closing the tool does not lose them. Auto-generated
points and island points are drawn in different colours from points you placed yourself.

The tool computes the support tree and the raft in a worker job and draws them, but it does not
run a full slice: the layer data is still produced by **Slice** in step 4. Before you slice,
check **Object elevation** and **Raft type** in the *Supports & raft* preset — *Object elevation*
(mm, 5 by default) is how far the supports lift the model, and is ignored when *Raft around
object* is on. *Raft type* offers **None**, **Full plate** (the default), **Around object** and
**Skate**; *Use raft* switches the raft on and off.

## 4. Slice

Slicing happens when you press **Slice**, at the bottom of the right-hand sidebar in the **Prepare**
view. The same button reads *Slice all* with more than one build plate, *Add objects to slice* when
nothing is on a plate, *Cancel* while a slice is running, and *Invalid settings* if it failed — in
which case the reason is in the error message and in the dialog the button opens.

Nothing slices by itself. The `Auto-reslice` switch that FFF users see in **Preview** is hidden for
a resin printer, so entering Preview, moving a model or editing supports never starts a slice on
its own. As soon as anything changes, the existing slice is stale and the button turns back into
**Slice**. A progress notification shows the current step while it runs.

## 5. Preview

The app switches to **Preview** after a successful slice. From here:

- The **layer slider** runs vertically between the 3D view and the right sidebar, with two thumbs
  so you can view a range of layers. Hovering shows the estimated time for that layer. `Up`/`Down`
  step one layer, `Shift` five, `Ctrl` ten, and *Jump to height* (`Shift+G`) goes to a height in mm.
- The right sidebar shows **Resin** in ml, **Cost**, and **Layers**, each a `—` until the plate has
  been sliced. The object list shows **Sliced Info** with *Used material* and *Printing time*.
- The **Layer image** window, on the left under the object list, appears as soon as the plate has
  been sliced. It shows the layer exactly as the printer's screen will show it: the same greyscale
  raster the printer gets, rendered with the display size, pixel count, mirroring and orientation
  from the printer preset. The caption gives the layer number, the total and the Z height, and the
  **Previous** / **Next** buttons step one layer, moving the slider's top thumb with them. The 3D
  view and this window follow the slider together, so they always show the same layer.

Two warnings can pop up after a slice finishes, and both stay until you dismiss them:

- *N islands found, first on layer M. They can fall off during printing.* An island is a small
  area of the layer that touches nothing else; it will detach during printing.
- *Sliced without supports: …* with the model names, followed by *Open the support tool to add
  supports, or ignore this if the model should sit on the build plate.* Ignore it for a model
  printed flat on the plate.

## 6. Export

The **Export** button at the bottom of the **Preview** sidebar, with the tooltip *Export print
file*. It only becomes an export button once the slice has finished; if the plate changed since, it
reads **Slice** again. If you reach the export path some other way with an unfinished slice, you get
*Export failed* and *The plate is not fully sliced yet. Press Slice, wait until it finishes, then
export again.*

The file format follows the printer preset. The Anycubic Photon Mono M5 writes `.pm5`, the Elegoo
Saturn and Mars write `.goo`, and the Original Prusa SL1 writes `.sl1`. The **Export as** dialog
pre-selects the printer's own format, with the other supported formats (`pwmo`, `pwmx`, `pwms`,
`goo`, `sl1`, `sl1s`, `sl1svg`) available in the filter dropdown. Choosing an extension that does
not match the printer you sliced for opens a *Different file type* warning and sends you back to
choose a name.

**Select Destination**, just above the button, chooses where it goes: **Local Drive**,
**Removable Drive**, or **Prusa Connect**. *Removable Drive* only appears in the list while a
removable drive is plugged in, and selecting it makes the save dialog start in that drive. There
is no automatic eject: when the export finishes you get an **Export Finished** notification with
**Open folder**, plus an **Eject** button when the file went to a removable drive.

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
