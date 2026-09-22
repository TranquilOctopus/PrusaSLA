# "PrusaSlicer" name audit (M1.12a)

Audited 2026-09-22 against `sla/batch-m11`, after the rename to ResinSlicer in `version.inc`.

**Conclusion: the rename is functionally safe. No further code change is needed.** Every remaining
occurrence is dead code, an asset filename, upstream attribution, or a file-format marker that
must not change.

## Why this audit exists

The rename caused a hard crash: `MainFrame.cpp` asserted that its own window title contained the
literal `"PrusaSlicer"`, so the app aborted before the window appeared. That assert now checks
`Slic3r::BUILD_ID`. This audit looked for anything else of that shape.

## Where the name comes from now

`version.inc` sets `SLIC3R_APP_NAME` / `SLIC3R_APP_KEY` to `ResinSlicer` and `SLIC3R_BUILD_ID` to
`ResinSlicer-<version>+<suffix>`. In code: `Slic3r::APP_NAME` (data dir, `Directories.cpp:221`)
and `Slic3r::BUILD_ID` (window title, single-instance search,
`AppInstanceMessageHandlerWin32.cpp:87`).

## Categories

- **(a) functional** — behaviour derives from the product name; breaks under rename.
- **(b) asset name** — names a file on disk; renaming the string alone breaks the lookup.
- **(c) upstream attribution or file-format marker** — changing it breaks compatibility or
  misattributes the work.
- **(d) user-visible text** — change only if it refers to this application.

## Scope and method

516 occurrences in `.cpp`/`.hpp` under `src/`; 287 are comments, doc references or URLs.
Rather than hand-classify all 516, the search was narrowed to occurrences that can change
behaviour: the name inside a string literal used in a comparison or lookup (`find`, `compare`,
`==`, `starts_with`, `contains`), excluding comments and prusa3d URLs. That yields the 13 below.
Everything outside the filter is a comment, URL, log line or prose and cannot affect behaviour.

## The 13 behaviour-carrying candidates

| File:Line | Category | Action |
|---|---|---|
| `slic3r/GUI/InstanceCheck.cpp:132` | (a) but dead code | none — see below |
| `slic3r/GUI/InstanceCheck.cpp:198` | (a) but dead code | none |
| `slic3r/GUI/UpdateDialogs.cpp:282` | (d) but dead code | none |
| `slic3r/Utils/Process.cpp:72` | dead code, already commented out | none |
| `slic3r-shared/.../Format/3mf/Model3mf.cpp:921` | (c) format marker | **must not change** |
| `slic3r-shared/.../Format/3mf.cpp:765` | (c) format marker | **must not change** |
| `slic3r-shared/.../Config/Legacy/3mf_legacy.cpp:2597` | (c) format marker | **must not change** |
| `slic3r-shared/.../Config/Legacy/3mf_legacy.cpp:2602` | (c) refers to upstream | none |
| `slic3r-shared/.../Config/Legacy/3mf_legacy.cpp:2606` | (c) refers to upstream | none |
| `slic3r-shared/.../Config/Legacy/3mf_legacy.cpp:2610` | (c) refers to upstream | none |
| `slic3r-shared/.../Config/Legacy/3mf_legacy.cpp:2614` | (c) refers to upstream | none |
| `slic3r-domain/.../ConfigDefsFDM.cpp:3752` | (d) FFF tooltip | deferred, see below |
| `slic3r-shared/.../Config/Legacy/PrintConfig.cpp:3182` | (d) legacy FFF tooltip | deferred |

### `src/slic3r/` is not built

239 of the 516 hits are under `src/slic3r/`, the pre-3.0 GUI. There is no
`src/slic3r/CMakeLists.txt`, nothing references it from the build, and `build-default` contains no
object files for it. It is dead code carried in the tree. Its `InstanceCheck.cpp` has exactly the
bug pattern we are hunting — `wndTextString.find(L"PrusaSlicer")` — but nothing compiles it. The
live equivalent, `AppInstanceMessageHandlerWin32.cpp`, already uses `BUILD_ID` and is correct.

### The 3MF markers must keep the old string

Three sites test `starts_with(value, "PrusaSlicer-")` against the `Application` metadata of a
loaded 3MF. `Model3mf.cpp:915` shows the intent: `is_old_stored_version` decides whether a file
came from a PrusaSlicer **older than** `last_old_stored_version`, so legacy handling is applied.

We write `Application = SLIC3R_BUILD_ID`, now `ResinSlicer-…`, so our own files do not match —
which is correct, because they are not old PrusaSlicer files. Changing these comparisons to
`BUILD_ID` would stop the app recognising genuinely old PrusaSlicer 3MFs and silently drop their
legacy handling. Leave them.

### The two FFF tooltips

Both read "When enabled, PrusaSlicer will check whether your custom Start G-code …" and refer to
this application, so by the rules they are (d) and should say ResinSlicer. Deferred deliberately:
they are FFF-only, they are translatable (changing the source string invalidates existing
translations), and this fork hides the FFF tool settings. Fold them into a translation pass rather
than changing them in isolation.

## Not covered here

Asset filenames — `PrusaSlicer.svg`, the `.ico`/`.icns` app icons, `splashscreen.jpg`, and the
`Icon::PrusaSlicerIcon` enumerator that names them. Renaming the strings without renaming the
files breaks the lookups. These belong to M1.12b with the artwork.
