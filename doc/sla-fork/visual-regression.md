# Visual regression renders (roadmap M6.2, PLAN G3)

Renders of the plater views of a fixture, and the tool that compares them with a stored reference
and checks the grayscale lightness rule of PLAN 2.1.

The rule is short: the model, the supports and the pad have to be told apart **by lightness**, not
by hue alone. F3 gives the scene its materials, G3 checks it: a render has to survive being turned
into a greyscale picture.

## What a render is

`--render-to` renders the 3D view of a fixture **offscreen**, through the same
`ThumbnailRenderer` path the objects list and the gallery thumbnails use: the scene goes into a
framebuffer of the size asked for, is read back, flipped and written as an RGB PNG. A render is
therefore:

- the scene only: no window, no ImGui UI, no sidebar, no overlays, no cursor;
- a fixed camera: the camera of the view it draws, and not the one the window happens to have.
  Prepare is framed on the bed the way a thumbnail is, the Preview view is looked at from where the
  app is looking at it (M6.2b);
- an exact size, so a reference is comparable at all: a screenshot of the window depends on the
  window size, the monitor and the theme of the desktop, a render depends on none of them.

It is *not* a screenshot of the running app. What it does show is what the fork draws: the bed and
the plate, the models, the SLA supports and the pad, in the colours of the theme tokens.

`--render-view prepare` draws the bed (the Prepare tab), `--render-view preview` draws the Preview
tab itself: its own scene, its own camera and its own customizer, and not the print the way the
objects list thumbnails draw it, which is what made a change in that view invisible to the check.
Both wait for the slice of the fixture to finish first, because that is when the supports and the pad
exist, and a view that has no print in its scene yet is asked again rather than rendered as an
empty bed.

What a render of the Preview view keeps is the eye, the target, the angles around them and the zoom
the view has, handed over exactly the way the two views hand their cameras to each other when a tab
is switched. The one thing the size asked for changes is the viewport of the camera, because two
sizes of the same view are not the same picture.

## Rendering

Build the app (see [BUILD.md](BUILD.md)), then:

```powershell
.\build-default\src\slic3r-app-launcher\Release\prusa-slicer-launcher.exe --sla-fixture C:\path\to\fixture.3mf --render-to out\prepare.png --render-view prepare
.\build-default\src\slic3r-app-launcher\Release\prusa-slicer-launcher.exe --sla-fixture C:\path\to\fixture.3mf --render-to out\preview.png --render-view preview
```

| flag | meaning |
|---|---|
| `--sla-fixture <file.3mf>` | loads the fixture and slices it (M0.11b, unchanged) |
| `--render-to <file.png>` | renders one view offscreen to this PNG, writes `<file.png>.json` next to it and quits |
| `--render-view prepare\|preview` | which view; defaults to `prepare`. `preview` is the Preview tab as itself |
| `--render-size WIDTHxHEIGHT` | size of the image, 1..8192 per side; defaults to `1280x960` |

The app quits when the render is written, with exit code 0. A render that could not be made (no
project, no bed, nothing in the view, a file that cannot be written) exits 1 and says why in the
log, so a script can tell it from a crash. A wrong `--render-view`, a wrong `--render-size` or a
`--render-to` without `--sla-fixture` is refused before the window opens.

Nothing new slices anything: the render waits for the slice that `--sla-fixture` has always
started, and the Slice button is still the only other thing that starts one.

## The sidecar

Every render comes with `<file.png>.json`:

```json
{
  "tool": "M6.2 visual regression render",
  "view": "prepare",
  "scene": "plater",
  "width": 1280,
  "height": 960,
  "composited_background_token": "SceneBgBottom",
  "lightness": {
    "model":             { "token": "SlaModelResin", "rgb": [132, 169, 140], "L": 65.88 },
    "supports":          { "token": "SlaSupport",    "rgb": [202, 210, 197], "L": 83.27 },
    "pad":               { "token": "SlaPad",        "rgb": [82, 121, 111],  "L": 47.77 },
    "background_top":    { "token": "SceneBgTop",    "rgb": [53, 79, 82],    "L": 31.71 },
    "background_bottom": { "token": "SceneBgBottom", "rgb": [47, 62, 70],    "L": 25.23 }
  }
}
```

`scene` is what drew the picture: the bed of the Prepare tab (`plater`) or the Preview view
(`preview`). It is in the sidecar so a diff of two sidecars says whether a changed image is a change
of the view or a change of the scene behind it, and so a reference of a preview render is not
mistaken for one of the objects list thumbnail of the same print.

The L* values are resolved by the app from the theme tokens it drew with, so the numbers describe
the palette that is in `Theme.cpp` and not a copy of it. The tool never sees a hex value of its
own. Anything the render drew transparent (the preview view draws no background) is composited
over `SceneBgBottom` before the PNG is written, because a transparent pixel has no lightness.

## Comparing

`doc/sla-fork/tools/visual_diff.py` does the comparing. Standard library only, so it runs
anywhere the render runs.

```powershell
# per pixel diff against a stored reference, with the percentage of changed pixels
python doc\sla-fork\tools\visual_diff.py compare out\prepare.png doc\sla-fork\visual-regression\fixture-prepare.png

# the same, plus a difference image, plus the grayscale variant
python doc\sla-fork\tools\visual_diff.py compare out\prepare.png doc\sla-fork\visual-regression\fixture-prepare.png --diff-out out\diff.png --tolerance-pct 0.5 --channel-tolerance 8

# the lightness rule, on the tokens the render was drawn with
python doc\sla-fork\tools\visual_diff.py lightness out\prepare.png.json

# the lightness rule, on the rendered pixels as well
python doc\sla-fork\tools\visual_diff.py lightness out\prepare.png.json --probes doc\sla-fork\visual-regression\fixture-prepare.probes.json

# the greyscale variant of a render, the thing a person looks at
python doc\sla-fork\tools\visual_diff.py grayscale out\prepare.png out\prepare-gray.png

# everything, with one exit code, which is what a CI job wants
python doc\sla-fork\tools\visual_diff.py check --render out\prepare.png --reference doc\sla-fork\visual-regression\fixture-prepare.png --sidecar out\prepare.png.json --probes doc\sla-fork\visual-regression\fixture-prepare.probes.json --diff-out out\diff.png --grayscale out\prepare-gray.png
```

The exit code is 0 when everything passed and 1 when something did not.

### compare

A pixel counts as changed when any of its channels moves by more than `--channel-tolerance`
(8 of 255 by default), which keeps a render stable against the last bits of a driver rounding
differently on another GPU. The run passes while at most `--tolerance-pct` of the pixels are
changed (0.5 % by default). It prints the changed count and percentage, the biggest channel delta,
the mean delta over the changed pixels and the bounding box of the change, which is the fastest way
to see that a change is one label and not the whole scene. A different size or a different pixel
format fails without a number: a reference of another size is not a reference.

### lightness

The pairs that are compared are every object role against every other one (model, supports, pad)
and each of them against both ends of the background gradient. The two ends of the background are
never compared with each other: PLAN 2.1 allows `Slate700` against `Slate900` for surface layering,
it just may not be the only cue for information.

The threshold is **10 L\***, a just noticeable lightness difference, `--threshold` changes it. The
palette of PLAN 2.1 keeps its tightest pair (`SlaPad` against `SlaSupport` in the dark theme) at
16 L\*, so the check is a tripwire for a palette change that collapses two roles, not a limit the
palette is fighting today. The same rule is a unit test, in
`src/slic3r-shared/test/Slic3r/App/FixtureRenderTests.cpp`, so a palette change that breaks it
fails `slic3r-shared-tests` and not only a render run.

With `--probes` the same pairs are checked on the rendered pixels, using the median L* of a
rectangle per role and a probe file next to the reference:

```json
{ "probes": {
    "model":             [430, 300, 120, 120],
    "supports":          [560, 300, 60, 120],
    "pad":               [400, 470, 200, 20],
    "background_top":    [10, 10, 40, 40],
    "background_bottom": [1230, 900, 40, 40] } }
```

The rectangles are in pixels of that render, `[x, y, width, height]`. This is the part that catches
what the token numbers cannot: a light source, a shading type or a material that renders the
supports as dark as the model, on paper or not. The run prints the shading difference between what
a probe measured and what the token says, so a probe that has drifted off its role is visible
before it starts failing somebody else's change.

### grayscale

Writes the render as an 8 bit greyscale PNG where the byte is the CIE L* of the pixel, and prints
the L* percentiles. This is the picture the lightness rule is about: if the model, the supports
and the pad are still apart in it, the rule holds.

## References

**There are no committed references yet.** Whoever renders the fixtures first decides which
renders are the references, and commits them, suggested layout:

```
doc/sla-fork/visual-regression/<fixture>-<view>.png
doc/sla-fork/visual-regression/<fixture>-<view>.png.json
doc/sla-fork/visual-regression/<fixture>-<view>.probes.json
```

To make a reference, or to update one after a change that is meant to change the picture: render
it with the command above, look at the greyscale variant, and commit the PNG, its sidecar and a
probe file. A diff that is a whole new picture is not an accident to work around with a tolerance.

## Left

- No references and no probe files yet, so nothing here has been run end to end: the render path
  and the tool are unbuilt, and the first render is what will tell whether the offscreen path needs
  a fix.
- Three fixture scenes and both themes (F3) are still to be chosen and rendered by a person.
- The render is the scene, not the window: the left bar, the sidebar, the top bar and the preview
  overlays are not covered by this. A window screenshot would need a fixed window size and a fixed
  desktop theme, which is a bigger job than this todo.
- Nothing runs the tool in CI yet. That is M0.14 plus a job; the exit codes are there for it.
- `--render-view preview` renders the Preview tab as itself since M6.2b - its scene, its camera and
  its customizer - so the check covers the 3D view of that tab and not the window around it: the
  sidebars, the top bar and the preview overlays are still outside it, which is M6.2c.
