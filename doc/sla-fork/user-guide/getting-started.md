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
named `Resin 1`, `Resin 2`, and so on. That is where **Layer height**, **Transition layers**,
**Exposure time**, **Initial exposure time**, the lift / retract / tilt speeds and wait times, the
material type and colour, and the bottle volume, weight and cost live. Two resins ship with the
fork: **Generic Resin** and **Generic Fast Resin**.

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
supports lift the model. **More...** opens the full print settings: **Generate supports**,
**Support tree type**, **Support points density**, **Support only in enforced regions**, the
support shape values and the **Raft** group.

The **Raft** group starts with **Raft type** and then shows only the settings that type actually
uses. **None** prints no raft and shows nothing else; **Full plate** (the default) covers the whole
build plate; **Around object** makes a raft that only the object sits on; **Skate** is *Around
object* with half the expansion and a 70° wall slope. There is no *Use raft* switch, the type turns
the raft on and off. A raft around the object also means **Object elevation** is ignored, because
the raft carries the model instead of the supports.

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
  - **Click the image** and a **Zoom** panel opens under it: a 256 × 256 pixel crop of the display
    around the point you clicked, at 1:1, so single pixels are visible. It follows the layer while
    it is open, and the button beside *Zoom* closes it.
  - The **Area (mm²)** and **Peel force (N)** charts run over the whole print, with a vertical line
    on the layer you are on, and the values of that layer are written out under them.
  - Islands on the current layer are ringed on the image, and the **Islands** list under the charts
    gives each one with its layer, its area and its position, plus a **Go** button that jumps to
    that layer. **Previous island** and **Next island** step to the closest island below or above
    the current layer. A long list shows the first 50 islands, then a line reading *and N more*.
- A **Supports** block above the buttons lists the models on the selected build plate with their
  support point counts. **Edit supports** switches to **Prepare**, selects that model and opens the
  support tool on it, so a missing support can be added from where the warning came from.

Two warnings can pop up after a slice finishes, and both stay until you dismiss them:

- *N islands found, first on layer M. They can fall off during printing.* An island is a small
  area of the layer that touches nothing else; it will detach during printing.
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
