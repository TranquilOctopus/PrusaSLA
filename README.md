# ResinSlicer: an SLA-focused fork of PrusaSlicer

ResinSlicer is a fork of PrusaSlicer 3.0 (alpha) being turned into a slicer for resin (MSLA) printers: support point
editing, hollowing with drain holes, resin profile import, and export to the archive formats of several printer
makers. It is for anyone with an MSLA resin printer, not only for Prusa machines. It is an independent project and is
not affiliated with or endorsed by Prusa Research.

Work happens on the `sla/*` branches, with `sla/main` as the integration branch; `master` tracks upstream. The plan
and rules are in [`doc/sla-fork/PLAN.md`](doc/sla-fork/PLAN.md) and [`AGENTS.md`](AGENTS.md).

**User guide:** [`doc/sla-fork/user-guide/getting-started.md`](doc/sla-fork/user-guide/getting-started.md) ·
[`doc/sla-fork/user-guide/switching-from-chitubox-and-lychee.md`](doc/sla-fork/user-guide/switching-from-chitubox-and-lychee.md)

## What works today

Everything below is merged on `sla/main`; the open todos are in the progress table below.

### Prepare

- **Supports, by hand.** The *SLA Support Points* tool puts points on the lifted model: click to add, Ctrl-click to
  remove, drag to move, head diameter per point. The five support classes *0.1*, *0.2*, *0.3*, *0.4* and *0.6* size
  new points, and the tree stays on the model.
- **Point geometry.** Tip shape (Default, Cone or Ball), knot, stem sides, stem taper and the shape of the foot are
  per point and are built into the tree; *Supports & raft* sets what a new point starts from.
- **Auto support.** For the selected models or for all of them, writing the points into the models so you can still
  edit them. In Prepare and from the Preview sidebar, which also says when every model has its points.
- **Support painting.** The *Paint-on supports* tool (`L`) paints *Paint supports* and *Block supports* on
  the model; **Auto support** then places the points inside the painted regions and none in the
  blocked ones, except for the points that catch an island. Painting never slices.
- **Placement rules.** A minimal distance between generated points and an overhang angle above which no point is
  placed, both off by default; the points that catch an island are never filtered.
- **Bracing.** Pillars lean on each other by default; its diameter and start height are in *Supports & raft*, and the
  foot is a cone, a cylinder or a flat disc.
- **Rafts.** Type None, Full plate, Around object or Skate, each a bundle of the shared knobs, plus an edge taper that
  leaves a lip for a spatula, a floor thickness of its own, grid or honeycomb infill with its cell size, wall and skin
  (shown only with a pattern), and an interface band at the top of the raft with its own exposure.
- **Hollowing.** Wall thickness, accuracy and closing distance, with drain holes you add, move and resize.
- **Orientation.** *Auto orient* lays a model on its largest face, or in the pose needing the fewest supports.
- **Nothing slices itself.** Only **Slice** slices: the support tool never runs a slice, and auto-reslicing is off.

### Preview

- **Layer image window.** Every layer as the printer's screen shows it, with previous/next, the layer number and Z,
  and a 1:1 pixel zoom opened by clicking the image.
- **Islands.** A panel lists the current layer's islands with the model they sit on, their area and position, a *Go*
  button per row, Previous/Next island and rings on the image; a notification gives the count and how many models were
  hit after slicing.
- **Cups and trapped resin.** Both are detected on the sliced layers and listed with the model in the Prepare sidebar.
  No drain hole is suggested for a trapped cavity yet.
- **Area and peel force.** Two charts over the per-layer area and the peel-force estimate, marked at the current
  layer. Peel force is a model of cured area, boundary length and the suction of the cups open on that layer, with
  coefficients for the vat film, so the chart names the film and the layers over a limit are flagged.
- **Height band.** Two sliders clip the print to a Z range with capped cut faces, over models, support trees and
  rafts.

### Resin profiles

- **Print settings.** One sidebar button with a summary line, opening a two-tab dialog: **Resin** (exposure, layer
  height, bottom and transition layers) and **Supports & raft** (presets, raft, geometry).
- **The resin owns the layer height** and the transition-layer count, with a fall-back to the print preset, so older
  presets and projects still load.
- **Import a Chitubox `.cfg`** from *Import resin profile* or by dropping the file on the window, and every key gets a
  badge: Exact, Converted, Approximated, Not applicable or Unknown. The table shows the value the file had on one side
  of the row and the value written on the other, so a converted value is readable without the note.
- **Import from a sliced `.sl1`/`.sl1s` archive**, read lazily so a large file imports too.
- **New resin from datasheet:** the numbers a resin sheet states — exposure, layer height, bottom layers and the delay,
  plus the lift distance, the lift and drop speeds and the transition-layer count a generic MSLA sheet may give —
  validated, then reviewed and saved.
- **Command line.** `--import-resin-profile` with `--dry-run` and a JSON `--report`, and `--export-resin-profile` to
  write a `.cfg` back out.

### Export

- **Formats.** Every registered archive writer, the printer's own format first: `.sl1`/`.sl1s` and `.sl1svg` (Prusa),
  `.goo` (Elegoo), `.pwmo`, `.pwmx`, `.pwms` (Photon), `.pm5`, `.pm5s` and `.pm7` (Photon Mono M5 family, the
  latter two experimental) and `.ctb` (experimental).
- **Destinations.** Local drive, removable drive (with **Eject** on the finished-export notification) and upload to a
  print host. A host only gets the file when it can read the plate's format: PrusaLink and Prusa Connect take the SL1
  format, the FFF servers take G-code, and anything else is refused by name instead of being uploaded.
- **Anti-aliasing follows the gamma.** *Printer gamma correction* of 0 rasterizes hard black and white, and the header
  fields of `.pwmx`, `.goo` and `.ctb` then say no anti-aliasing; the default of 1 keeps the 8-bit grey image.
- **A checklist first.** Nothing exports from an unfinished slice; a plate with unsupported models or islands gets
  *Check before printing* before the save dialog.

## Supported printers

The community bundle in [`resources/presets/community-sla/`](resources/presets/community-sla/) ships six profiles; the
Prusa SLA profiles ship in [`resources/presets/prusa-research-sla/`](resources/presets/prusa-research-sla/).

| Printer | Resolution | Export format |
|---|---|---|
| Anycubic Photon Mono M5 | 11520 x 5120 (12K) | `.pm5` |
| Anycubic Photon Mono M5s | 11520 x 5120 (12K) | `.pm5s`, experimental, unverified |
| Anycubic Photon Mono M7 Pro | 13320 x 5120 (14K) | `.pm7`, experimental, unverified |
| Elegoo Saturn 4 Ultra 12K | 11520 x 5120 (12K) | `.goo` |
| Elegoo Saturn 4 Ultra 16K | 15120 x 6230 (16K) | `.goo` |
| Elegoo Mars 5 Ultra | 8520 x 4320 (9K) | `.goo` |
| Original Prusa SL1 | 2560 x 1440 | `.sl1` |
| Original Prusa SL1S SPEED | 2560 x 1620 | `.sl1s` |

**No format has been verified on a real printer:** none has read a file from this fork. The specifications come from
vendor pages. Print the [orientation test piece](doc/sla-fork/orientation-test.md) once per printer and fix the
profile if the F comes out mirrored.

## Known limitations

- **Chitubox `.cfgx` and Lychee `.lyr` / `.lyp` are not read.** The picker accepts them, but no reader exists: they
  were never inspected against a real file, and this fork will not break an encryption. Use a sliced archive.
- **`.ctb` is experimental.** The unencrypted v3 container round-trips through its own reader, but no sample and no
  printer has read one, so the layer-definition units are a guess. The encrypted v4/v5 container is not touched.
- **`.pm5`, `.pm5s` and `.pm7` are unverified.** The `.pm5` layout was written from one Photon Workshop
  file; the other two are the same container with an unconfirmed printer name and format version.
  The field-by-field comparison and the mirroring check are waiting on a build, and nothing has been
  printed.
- **The hollowing wall thickness fix is unverified.** The offset was read in voxels and compared against millimetres,
  so the wall came out set by the quality and closing distance. The fix and its tests are written but not built.
- **No drain hole suggestions.** Islands, suction cups and trapped resin are found on the sliced layers and listed with
  the model they belong to, and nothing suggests where to put a drain hole for a trapped cavity.
- **One model colour for SLA.** A resin tint and translucency were declined; models draw in one theme colour.
- **No release yet.** No packaged build or known-issues list (M6.7) and no CI or visual regression suite. This is a
  working tree, not a release, and the UI above has not been walked end to end in a built app: M6.4 is open.

## Building

Build the dependencies once, then the app: [`doc/sla-fork/BUILD.md`](doc/sla-fork/BUILD.md) has the prefix path, the
configure and build commands, the test binaries, the debug flags and the upstream sync procedure.

## Contributing

[`AGENTS.md`](AGENTS.md) has the rules, the layering and the workflow for a change;
[`doc/sla-fork/ROADMAP.md`](doc/sla-fork/ROADMAP.md) is the plan, with every todo and its result note. One todo, one
branch, one commit, the box ticked in the same commit.

## Screenshots

<!-- TODO screenshot: Prepare, support tool open on a lifted model with the tree on it -->
<!-- TODO screenshot: Print settings dialog, Supports & raft tab with the Raft group -->
<!-- TODO screenshot: Preview layer image window with the 1:1 zoom and the island list -->
<!-- TODO screenshot: Resin import review dialog with the mapping badges -->

<!-- PROGRESS:START (generated by doc/sla-fork/tools/readme_progress.py; edit ROADMAP.md instead) -->

## Progress

**355 of 392 todos done (91%)** · updated 2026-10-07 · full list and result notes in [`doc/sla-fork/ROADMAP.md`](doc/sla-fork/ROADMAP.md)

| Milestone | Done | |
|---|---|---|
| M0: Foundation | 75/76 | `████████████` 99% |
| M1: Look, feel and SLA-first shell | 44/45 | `████████████` 98% |
| M2: SLA editing tools (porting the legacy gizmos) | 99/100 | `████████████` 99% |
| M3: Resin profile import (Chitubox, Lychee and others) | 24/28 | `██████████░░` 86% |
| M4: Engine quality (measure first; every PR includes before/after metrics) | 32/41 | `█████████░░░` 78% |
| M5: Formats and inspection | 38/43 | `███████████░` 88% |
| M6: Quality gates and release | 17/22 | `█████████░░░` 77% |
| M7: Excellent auto-supports *(parked)* | 26/37 | `████████░░░░` 70% |

### Waiting on you

- **M3.1** Put a few real `.cfg`, `.cfgx`, `.lyr` and `.lyp` files in `local-samples/`, exported from your own Chitubo…
- **M5.3.samples** Provide one sliced sample archive per target printer (from Chitubox/Lychee/Photon Workshop) and list the pr…
- **M6.2a** The visual regression has never been run end to end: build the app, choose the three fixture scenes and bot…
- **M6.4** End-to-end walk through the M1.1 journeys on an integrated build, filing new todos for gaps.

### Open todos

<details><summary>M0: Foundation — 1 open</summary>

- [ ] **M0.13** Benchmark harness that writes a metrics JSON (PLAN A6).

</details>

<details><summary>M1: Look, feel and SLA-first shell — 1 open</summary>

- [ ] **M1.1b** Runtime screen audit with an SLA printer selected, following the R1–R10 checklist in `ux/journeys.md`. Reco…

</details>

<details><summary>M2: SLA editing tools (porting the legacy gizmos) — 1 open</summary>

  - [ ] **M2.9b** Resin tint and translucency: the model follows the material's `material_colour` (PLAN 2.1 rule 4) and a tra…

</details>

<details><summary>M3: Resin profile import (Chitubox, Lychee and others) — 4 open</summary>

- [ ] **M3.1** Put a few real `.cfg`, `.cfgx`, `.lyr` and `.lyp` files in `local-samples/`, exported from your own Chitubo… *(needs you)*
- [ ] **M3.2** Add `local-samples/` to `.gitignore`. Document the observed structure of each sample format in `doc/sla-for…
- [ ] **M3.12** `ChituboxCfgxReader`, if M3.2 marked it feasible; otherwise close this todo with a link to the M3.2 finding.
- [ ] **M3.13** `LycheeLyrReader` (and `.lyp` printer hints), if M3.2 marked it feasible; otherwise close it with a link to…

</details>

<details><summary>M4: Engine quality (measure first; every PR includes before/after metrics) — 9 open</summary>

- [ ] **M4.1** Tracy profiling run over the benchmark set. Write a hotspot report in `doc/sla-fork/profiling/`. No code ch…
- [ ] **M4.2** Re-rank M4.3–M4.10 based on the M4.1 report. *(needs you)*
- [ ] **M4.3** Support point generation: regression tests for `tests/data/sla_islands/*.svg` and the benchmark set (PLAN B2).
- [ ] **M4.4** Support point generation: fix the worst unsupported-island cases found in M4.3.
- [ ] **M4.5** Branching tree: reduce support volume with no new unsupported points (PLAN B3).
  - [ ] **M4.5b** A/B the key and the existing widening factor on the benchmark set with the M0.13 harness, record the suppor…
- [ ] **M4.6** Pad robustness when printing directly on the plate, plus pad generation speed (PLAN B4).
- [ ] **M4.7** Hollowing performance and wall thickness tolerance test (PLAN B5).
- [ ] **M4.14** Peak memory when slicing for 12K and 16K displays (Photon Mono M5: 11520 × 5120, about 59 megapixels per la…

</details>

<details><summary>M5: Formats and inspection — 5 open</summary>

- [ ] **M5.3.pw-b** Anycubic newer formats (`.pm5`, `.pm5s`, `.pm7`) — **`.pm5` first: it is the format the maintainer's Photon…
- [ ] **M5.3.pw-b3** The `.pm5` we write has never been compared field by field with the maintainer's own Photon Mono M5 sample:… *(needs you)*
- [ ] **M5.3.samples** Provide one sliced sample archive per target printer (from Chitubox/Lychee/Photon Workshop) and list the pr… *(needs you)*
  - [ ] **M5.4d** Check the orientation of the `.pm5` we write for the Photon Mono M5 against a real Photon Workshop file, si…
- [ ] **M5.4f** No printer has confirmed any row of the orientation table, and all six `community-sla` printers inherit `di… *(needs you)*

</details>

<details><summary>M6: Quality gates and release — 5 open</summary>

- [ ] **M6.2a** The visual regression has never been run end to end: build the app, choose the three fixture scenes and bot… *(needs you)*
- [ ] **M6.4** End-to-end walk through the M1.1 journeys on an integrated build, filing new todos for gaps. *(needs you)*
- [ ] **M6.5** Retune default presets after the M4 changes.
- [ ] **M6.6** Fork README and user guide.
- [ ] **M6.7** Release candidate: version bump, packaging, known-issues list. *(needs you)*

</details>

<details><summary>M7: Excellent auto-supports — 11 open (parked)</summary>

- [ ] **M7.3** Contact extraction script.
- [ ] **M7.4** Rule mining across the dataset. Measure things like:
- [ ] **M7.6** Support quality scorecard. The benchmark harness writes the generator's support points and tip sizes to JSO…
- [ ] **M7.7** Baseline scorecard for the current generator (default and branching tree). Commit only the aggregate number…
- [ ] **M7.8** Implement the rulebook in the generator, one sub-todo per rule (`M7.8.<n>`). Each must raise the scorecard…
  - [ ] **M7.8.6** Automatic bracing of close stems (R5.4), modelled on Lychee's auto braces: braces join support stems that s…
- [ ] **M7.9** Cosmetic surface awareness. Detect likely cosmetic surfaces (faces, fine detail, high-curvature upward regi…
- [ ] **M7.10** Size tips for each region, following the rulebook: light tips on small detail, heavier tips where the load…
- [ ] **M7.11** Automated tuning. Search the generator parameters against the dataset scorecard, and promote the best setti…
- [ ] **M7.12** Print validation. Print a set of models using auto supports only. Record the results in `doc/sla-fork/suppo… *(needs you)*
- [ ] **M7.13** *(optional research)* A learned contact predictor trained on the extracted contacts, used only as a scoring…

</details>

<!-- PROGRESS:END -->

---

*The upstream PrusaSlicer README follows, unchanged.*

![PrusaSlicer logo](/resources/icons/PrusaSlicer_128px.png)

# PrusaSlicer

PrusaSlicer enables you to take your 3D models, generate 3D printing instructions and send them to your 3D printer. It supports both FDM 3D printers and mSLA 3D printers. It is developed by [Prusa Research](https://www.prusa3d.com/) and apart from Prusa printers it supports machines from a wide variety of manufacturers.

PrusaSlicer is originally based on [Slic3r](https://github.com/Slic3r/Slic3r) by Alessandro Ranellucci and the RepRap community.

## Installation

The **recommended installation method** is to go to the [PrusaSlicer project page](https://www.prusa3d.com/prusaslicer/) and download and install the software by following the instructions there.

**Alternatively**, for Windows and macOS, you can download the software directly from the [GitHub releases page](https://github.com/prusa3d/PrusaSlicer/releases). For Linux, PrusaSlicer is currently distributed exclusively through [Flathub](https://flathub.org/en/apps/com.prusa3d.PrusaSlicer).

If you prefer, you can always build PrusaSlicer yourself from source. See the [documentation](doc/Build.md) to learn how to do it.

### Main features

* Set the **printing parameters** with precision - from settings affecting the **whole print** down to **single-layer adjustments**.
* Modify your objects before printing to best suit your needs, you can **paint**, **cut**, **arrange** and do **many more** directly in PrusaSlicer.
* Open **multiple projects** at once, each containing **multiple beds** (each with potentially **different settings**).
* Make full use of **multi-material printing**
* Use your **preferred method to send your prints to the printer**, PrusaSlicer supports a wide range of possibilities.
* **View the generated print instructions** in an advanced **3D preview**.
* You can make use of the **command-line interface** to use **PrusaSlicer without GUI** in your automation setups.
* If you want, you **can make use of the integration** with [PrusaConnect](https://connect.prusa3d.com/) and [Printables](https://www.printables.com) to greatly simplify your workflow.

### Reporting a bug

Did you find a bug? Bugs can be reported in our [GitHub issue tracker](https://github.com/prusa3d/PrusaSlicer/issues), but **first make sure your report complies with the [Issue tracker policy](https://github.com/prusa3d/PrusaSlicer?tab=contributing-ov-file#issue-tracker-policy)**. **Are you not sure?** Get in touch in our [GitHub Discussions](https://www.github.com/prusa3d/discussions), before filing a bug report in the issue tracker. You can always file a bug report later, once you have more confidence.

### How to get in touch

We maintain a [GitHub Discussions](https://www.github.com/prusa3d/discussions) page in this repository to be used for **general discussions**, **questions** and **feature requests**. Furthermore, there is an announcements category, which we use to communicate with you directly.

### Pull requests

Read our [contribution guide](.github/CONTRIBUTING.md) to get more information.

### Technical stack

All of PrusaSlicer is written in C++, using CMake as the build system. The code assumes that the compiler supports C++20.

The slicing backend heavily relies on [Clipper](https://www.angusj.com/clipper2) by Angus Johnson, which handles polygon boolean operations, offsets and similar. [Eigen](https://libeigen.gitlab.io/) library is used for basic types and linear algebra calculations.

[wxWidgets](https://wxwidgets.org) library is used to handle GUI windows and events across platforms. Most of the UI is implemented using a custom OpenGL-based UI toolkit based on the [Yoga layout engine](https://github.com/facebook/yoga) and [Dear ImGui](https://github.com/ocornut/imgui), which makes it platform-independent.

The application uses many other libraries. You can see the [deps/](deps/) and [bundled_deps/](bundled_deps/) folders in the source tree to see the complete list. We are grateful to the authors and maintainers for open-sourcing their work.
