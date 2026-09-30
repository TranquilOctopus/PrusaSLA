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
- a fixed camera: the one the thumbnail customizer sets (perspective, framed on the bed), not the
  one the window happens to have;
- an exact size, so a reference is comparable at all: a screenshot of the window depends on the
  window size, the monitor and the theme of the desktop, a render depends on none of them.

It is *not* a screenshot of the running app. What it does show is what the fork draws: the bed and
the plate, the models, the SLA supports and the pad, in the colours of the theme tokens.

`--render-view prepare` draws the bed (the Prepare tab), `--render-view preview` draws the sliced
print. Both wait for the slice of the fixture to finish first, because that is when the supports
and the pad exist.

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
| `--render-view prepare\|preview` | which view; defaults to `prepare` |
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

### The manifest

The CI job (below) needs to know which renders to make and what to compare them with, so the
references are listed in `doc/sla-fork/visual-regression/manifest.json`, written by the same
person who commits them:

```json
{
  "size": "1280x960",
  "fixtures": [
    {
      "id": "bracket-prepare",
      "fixture": "tests/data/sla_fixtures/bracket.3mf",
      "view": "prepare",
      "size": "1280x960",
      "reference": "doc/sla-fork/visual-regression/bracket-prepare.png",
      "probes": "doc/sla-fork/visual-regression/bracket-prepare.probes.json"
    }
  ]
}
```

| key | meaning |
|---|---|
| `size` | the render size of every entry that names none, the app's own default `1280x960` |
| `id` | the name of the render and of its output files, a name and not a path |
| `fixture` | the 3MF `--sla-fixture` gets, relative to the repository root |
| `view` | `prepare` or `preview` |
| `size` (in an entry) | overrides the top level one, for a render of another size |
| `reference` | the committed PNG to compare with |
| `probes` | the optional probe file that puts the lightness check on the rendered pixels too |

One entry is one (fixture, view) pair, so the prepare and the preview view of a fixture are two
entries. A reference PNG whose sidecar and probes are in the repository is not enough on its own:
without its row in the manifest no job renders it, and without the manifest there is nothing to
compare against.

## In CI

`doc/sla-fork/tools/visual_ci.py` is the glue: the renders are the app's and the comparison is
`visual_diff.py`, and this drives both and turns them into one exit code.

```powershell
# What a run would do, without rendering anything.
python doc\sla-fork\tools\visual_ci.py plan

# The run itself. The app needs a GL context even though the render is offscreen, so on a headless
# machine the command goes through Xvfb.
xvfb-run -a python doc\sla-fork\tools\visual_ci.py run --app <path to slic3r-app-launcher> --out visual-out

# One render of one fixture, to see what a run would do for it.
python doc\sla-fork\tools\visual_ci.py run --only bracket-prepare --app <path> --out visual-out
```

It writes a Markdown report (`visual-regression.md` by default) with a table of the verdicts and
what `visual_diff.py` said for each of them, and it exits 0 when every render matched its reference,
1 when a render changed more than the tolerances allow or a lightness check failed, and 2 when the
run could not be made at all (no app, a manifest that is not valid, a reference the manifest names
but that is not in the commit, a render that wrote no PNG, a missing sidecar).

**A fork with no references yet is not a failing run.** While `manifest.json` is missing or empty
the tool says so, writes that in the report and exits 0, and the job skips the render and the
comparison instead of building an app to compare against nothing.

Its unit tests need no app and no build:

```bash
python doc/sla-fork/tools/test_visual_ci.py
python -m unittest discover -s doc/sla-fork/tools -p "test_visual_ci.py"
```

### The job

`sla-ci.yml` has a `visual-regression` job beside the test one ([ci.md](ci.md) has the cost and the
first-run watch list). It is manual only, like the rest of that file: the Actions tab, or the
`run-ci` label on a pull request. A run:

1. runs `test_visual_ci.py` and then `visual_ci.py plan`, which prints one line per render or the
   reason there is nothing to do, and writes `have_references` for the steps that follow,
2. installs the system packages, restores the deps prefix from the Actions cache and builds the
   app, all of it skipped while `have_references` is false,
3. renders every entry of the manifest with `--render-to` and compares it with `visual_diff.py
   check`, under Xvfb and the llvmpipe software rasteriser because a GitHub runner has no GPU,
4. writes the report into the job summary and uploads the renders, the diff images, the greyscale
   variants and the log as the artifact `sla-visual-regression`, whether it passed or not.

Nothing in the job slices: `--render-to` renders the slice that `--sla-fixture` started, and the
Slice button is still the only other way to start one.

## Left

- No references and no probe files yet, so nothing here has been run end to end: the render path
  and the tool are unbuilt, and the first render is what will tell whether the offscreen path needs
  a fix. The job skips itself for the same reason, and the first run of it is a person's job too.
- Three fixture scenes and both themes (F3) are still to be chosen and rendered by a person. The
  job renders with the default theme, because there is no command line flag for another one.
- The render is the scene, not the window: the left bar, the sidebar, the top bar and the preview
  overlays are not covered by this. A window screenshot would need a fixed window size and a fixed
  desktop theme, which is a bigger job than this todo.
- The job has never been run on a runner, and neither has the test one. The steps worth watching on
  a first run are in [ci.md](ci.md).
- `--render-view preview` renders the sliced print through the plater scene, the way the objects
  list thumbnails do, not the app's Preview tab with its own camera and overlays.
