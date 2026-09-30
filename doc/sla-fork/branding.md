# Branding: every reference to the old name and logo

Written for M1.12. Companion to [name-audit.md](branding/name-audit.md), which established that the
rename in `version.inc` to `ResinSlicer` is functionally safe. This file is the other half: the
*artwork*, and every remaining place the old name, logo or file names still turn up.

**Status: the vector sources and the renderer are done, the binaries are not.** Nothing in
`resources/icons/` has been overwritten yet, so the app still shows the Prusa artwork until
someone runs `doc/sla-fork/tools/render_icons.py` and commits the result. M1.12 stays unticked
until then.

## What is ours now

| File | What it is |
|---|---|
| `resources/icons/resinslicer.svg` | the app mark: a resin drop over a build plate, 128x128 grid |
| `resources/icons/resinslicer-splash.svg` | the splash screen, 600x540 like the `splashscreen.jpg` it replaces |
| `doc/sla-fork/tools/render_icons.py` | rasterises both into the `.ico`, `.icns`, `.png` and `.jpg` the build reads |

Both SVGs use only `rect`, `circle` and `polygon`, so they read without an SVG renderer, and only
the five PLAN 2.1 colours. No gradients and no opacity: the splash is resampled to arbitrary
display scale factors and stored as JPEG, where chroma subsampling smears soft edges.

The mark is sized for its smallest use. At 16 px the drop is 6 px across and the plate 12 x 2.5 px;
check `resources/icons/PrusaSlicer.ico`'s 16 px entry first after rendering.

## Rendering

    python doc/sla-fork/tools/render_icons.py --dry-run  # list sources, outputs and backend, write nothing
    python doc/sla-fork/tools/render_icons.py           # icon + splash
    python doc/sla-fork/tools/render_icons.py --icon    # icon only

Needs `Pillow`, plus one of two rasterisers. `--backend auto` (the default) uses `cairosvg`
(`python -m pip install cairosvg`, which also needs the cairo C library) when it imports and
Inkscape 1.x otherwise; `--backend cairosvg` and `--backend inkscape` force one. Inkscape is
found through the `INKSCAPE` environment variable, else as `inkscape` on PATH, so the Microsoft
Store build needs its app execution alias switched on (Settings > Apps > Advanced app settings >
App execution aliases > Inkscape) and a new terminal opened. It exports one size per run into
`.render_icons-*/` beside the outputs rather than %TEMP%, and the folder is removed when the tool
ends. Whichever backend runs, it exits 2 with the fix if one is missing. It overwrites, in place:

| Output | Sizes | Read by |
|---|---|---|
| `resources/icons/PrusaSlicer.ico` | 16 32 48 64 128 256 | `PrusaSlicer.rc.in:24`, `slic3r-app-launcher.rc.in:24` |
| `resources/icons/PrusaSlicer.icns` | 16 32 64 128 256 512 1024, plus the @2x set | `Info.plist.in:10,51,68,85` |
| `resources/icons/PrusaSlicer_128px.png` | 128 | `MainFrame.cpp:155` (non-Windows window icon) |
| `resources/icons/splashscreen.jpg` | 600x540 | `SplashScreen.cpp:42` |

Those four are the binary files the build and the installers read by name, which is deliberate. The
names are load-bearing for `rc.in`, `Info.plist.in`, the `.desktop` entries and `MainFrame.cpp`, and
renaming them is a coordinated change, not a branding one. The *contents* become ours; the *file
names* stay. The docstring of the renderer says the same thing, so the reasoning travels with the
tool.

The `.icns` is assembled by hand rather than by Pillow, which can only emit the eight fixed
`ic07`-`ic14` chunks; the script writes a `TOC` plus `icp4`/`icp5`/`icp6`/`ic07`-`ic10`/`ic11`-`ic14`,
all PNG-encoded, which covers every size the old file had (`ic04` 16 and `ic05` 32 were ARGB there
and are the PNG `icp4`/`icp5` here).

## Changed in this commit

Only user-visible text that names *this* application, and only where the old name was plainly
wrong. The window title needed nothing: `MainFrame.cpp:165` uses `Slic3r::BUILD_ID`, which is
`ResinSlicer-<version>+<suffix>`, and the assert at `MainFrame.cpp:177` checks the same macro rather
than a literal.

| File:line | Before | After |
|---|---|---|
| `slic3r-shared/src/Slic3r/App/MenuBuilder.cpp:139` | `About PrusaSlicer` | `About ResinSlicer` |
| `slic3r-app-desktop/src/Slic3r/App/Desktop/SplashScreen.cpp:296` | `Developed by Prusa Research.` | `PrusaSlicer by Prusa Research.` |

The splash credit was the one string that made a false claim: the fork is not developed by Prusa.
`PrusaSlicer by Prusa Research.` keeps the AGPL attribution and stays short enough not to reflow the
narrow banner, and it fills in the link the first line of the same block was missing (it credits
Slic3r, not PrusaSlicer).

Neither About dialog exists yet in the 3.0 shell: `MenuCommandRegistrar.cpp:1925` registers
`MenuItemName::About` inside `#ifdef SHOW_NOT_IMPLEMENTED_ITEMS`, which is commented out at
`MenuCommandRegistrar.cpp:44`, so the label is compiled but never shown. It was renamed anyway
because the moment somebody implements the dialog, the string is what they will read.

## Changed in the same commit as the binaries

Do not do these before `render_icons.py` has run: the `.ico` is named by an `rc` file the Windows
link step reads, so renaming the asset without the new file breaks the build outright, and the
runtime lookups are a silent no-icon rather than a build error.

| File:line | Refers to | What to change |
|---|---|---|
| `slic3r-app-desktop/src/Slic3r/App/Desktop/SplashScreen.cpp:46` | `WX::get_bmp_bundle("PrusaSlicer", 400)` | the fallback bitmap when the splash JPEG fails to load; the stem is the SVG name |
| `slic3r-app-desktop/src/Slic3r/App/Desktop/SplashScreen.cpp:191` | `load_svg("PrusaSlicer")` | the logo drawn on the splash banner |
| `slic3r-render/src/Slic3r/App/Render/ImguiIconHelper.cpp:98` | `{Icon::PrusaSlicerIcon, "PrusaSlicer"}` | the main-menu mark and the Welcome dialog top bar (`WelcomeDialog.cpp:1451`, `MenuBuilder.cpp:294`, `DoubleSliderForLayers.cpp:2009`) |
| `slic3r-shared/src/Slic3r/App/TestRenderModule.cpp:119` | `icons/PrusaSlicer-gcodeviewer.svg` | the G-code viewer splash mark, see the gaps below |
| `slic3r-app-desktop/src/Slic3r/App/Desktop/MainFrame.cpp:155` | `var("PrusaSlicer_128px.png")` | the non-Windows window icon |
| `src/platform/msw/PrusaSlicer.rc.in:24`, `src/platform/msw/slic3r-app-launcher.rc.in:24` | `icons/PrusaSlicer.ico` | the Windows exe icon |
| `src/platform/osx/Info.plist.in:10,51,68,85` | `CFBundleIconFile` = `PrusaSlicer.icns` | the macOS app and document icons |
| `src/platform/unix/com.prusa3d.PrusaSlicer.desktop.in:4` | `Icon=com.prusa3d.PrusaSlicer` | the icon name the desktop entry asks for; the file name itself comes from `CMakeLists.txt:101-102` |
| `src/platform/unix/PrusaSlicer.desktop:4` | `Icon=PrusaSlicer` | same, in the checked-in copy |

## Left alone on purpose

### Build identifiers and file names

Renaming any of these breaks packaging, upgrades or upstream merges, and none of them is visible
to a user. `version.inc` already carries the new name, which is what the runtime reads
(`Slic3r::APP_NAME` for the data directory, `Slic3r::BUILD_ID` for the title and the single-instance
search).

| Place | What it is |
|---|---|
| `CMakeLists.txt:12` | `project(PrusaSlicer)`: the CMake project name; the build directory is named after it |
| `CMakeLists.txt:101-102` | `configure_file` of `com.prusa3d.PrusaSlicer.desktop` |
| `src/platform/unix/com.prusa3d.PrusaSlicer.desktop.in` filename | reverse-DNS, so the installed file and its icon lookup keep matching |
| `src/platform/unix/*.desktop` `Exec=` | the installed binary is `prusa-slicer` |
| `src/platform/unix/*.desktop` `StartupWMClass=prusa-slicer` | must match the window class; note it does not even match today, `MainFrame.cpp:166` passes `from_u8("mainframe")` |
| `src/platform/osx/Info.plist.in:16` | `CFBundleIdentifier com.prusa3d.slic3r/`: changing it breaks upgrades and signing |
| `src/platform/osx/Info.plist.in:137` | the `prusaslicer://` URL scheme, so existing web links keep working |
| `src/slic3r-app-launcher/CMakeLists.txt:4` | the generated manifest file name |
| `src/platform/msw/*.rc.in:16` | `OriginalFilename`, `prusa-slicer.exe` and `prusa-gcodeviewer.exe` |

### Upstream attribution

Accurate statements about where this software came from. They are not ours to rewrite.

| Place | What it says |
|---|---|
| `slic3r-app-desktop/src/Slic3r/App/Desktop/SplashScreen.cpp:286-296` | `{} is based on Slic3r by Alessandro Ranellucci and the RepRap community.` plus the line changed above |
| `src/platform/msw/PrusaSlicer.rc.in:9,15`, `slic3r-app-launcher.rc.in:9,15`, `PrusaSlicer-gcodeviewer.rc.in:9,15` | `CompanyName` and `LegalCopyright` in the exe's version resource |
| `src/platform/osx/Info.plist.in:8` | `CFBundleGetInfoString` copyright line |
| `LICENSE`, the file headers, `README.md` | AGPLv3 and the copyright notices |
| `slic3r-shared/src/Slic3r/App/MenuBuilder.cpp:127,137` | `PrusaSlicer Website` and `Report PrusaSlicer Issue`; the commands they fire open `prusa3d.com/slicerweb` and `github.com/prusa3d/slic3r/issues` (`MenuCommandRegistrar.cpp:1844,1918`), so the label names the thing the link goes to. Repoint both when the fork has its own site and tracker. |
| `slic3r-app-desktop/src/Slic3r/App/Desktop/DesktopApp.cpp:715-716` | the crash-recovery dialog links two upstream GitHub issues, which is where the DLL-injection bug was reported |

### File-format markers

`starts_with(value, "PrusaSlicer-")` against the `Application` metadata of a loaded 3MF decides
whether a file came from a PrusaSlicer *older* than `last_old_stored_version`. See `name-audit.md`;
do not change them.

| Place | What it is |
|---|---|
| `slic3r-shared/src/Slic3r/Biz/Format/3mf/Model3mf.cpp:921` | the stored-version test |
| `slic3r-shared/src/Slic3r/Biz/Format/3mf.cpp:765` | the same test on the other read path |
| `slic3r-shared/src/Slic3r/Biz/Config/Legacy/3mf_legacy.cpp:2597,2602,2606,2610,2614` | "made with a newer version of PrusaSlicer", about the upstream file the user is opening |

### Still to decide

User-visible, but each needs a decision rather than a substitution.

| Place | Text | Note |
|---|---|---|
| `slic3r-app-desktop/src/Slic3r/App/Desktop/SplashScreen.cpp:207-208` | draws the wordmark as `Prusa` + `Slicer` in two colours | this is Prusa's logotype, the one piece of logo the app still shows on screen. Change the two literals to `Resin` and `Slicer` and the two-tone layout still works; not done here because it is a drawing change that has to be eyeballed. |
| `slic3r-app-desktop/src/Slic3r/App/Desktop/SplashScreen.cpp:115` | `m_highlighted_text_color(wxColour(237, 107, 33))` | Prusa orange. PLAN 2.1 rule 1 puts palette hex values in `Theme.cpp` only, so this cannot be moved onto a token from here; it belongs to F2, which is already tasked with the stray RGB literals in `App/`. |
| `slic3r-shared/src/Slic3r/App/WelcomeDialog.cpp:1049,1065` | "when PrusaSlicer crashes, a report is automatically sent to Prusa Research" | only the first word is wrong; where the reports actually go is a crash-reporting decision, not a string one. |
| `slic3r-shared/src/Slic3r/App/Init.cpp:41-49` | the injected-DLL warning names the PrusaSlicer process and links an upstream issue | Windows only, and the strings are plain `L()` rather than `_u8L()`. |
| `slic3r-shared/src/Slic3r/Biz/Network/HttpCurl.cpp:71` | "PrusaSlicer detected system SSL certificate store in:" | a Windows-only system dialog |
| `slic3r-shared/src/Slic3r/Biz/Config/Legacy/PrintConfig.cpp:2843,3182,3723,6068,6127` | FFF tooltips that name PrusaSlicer | already deferred to a translation pass by `name-audit.md` |
| `src/platform/unix/*.desktop:2` | `Name=PrusaSlicer` and `Name=PrusaSlicer (@SLIC3R_BUILD_ID@)` | user-visible. `CMakeLists.txt` substitutes `@SLIC3R_BUILD_ID@` into the `.in`, so the generated entry already says "PrusaSlicer (ResinSlicer-3.0.0-...)"; making the generated one read `@SLIC3R_APP_NAME@` is safe, the checked-in copy is a separate decision |

### Gaps found while auditing

- `SplashScreen.cpp:42` loads `splashscreen-gcodepreview.jpg` for the G-code viewer splash. That file
  does not exist, so that path already falls through to the line 46 fallback. M1.12 renders the
  editor splash only.
- `PrusaSlicer-gcodeviewer.svg` and `PrusaSlicer-gcodeviewer-mac_128px.png` are still Prusa
  artwork. The G-code viewer is a second app identity and needs its own mark; out of scope here.
  Its `src/platform/msw/PrusaSlicer-gcodeviewer.rc.in:24` points at
  `icons/PrusaSlicer-gcodeviewer.ico`, which does not exist in `resources/icons/`, and no
  `CMakeLists.txt` configures that `.rc.in` either.
- `resources-src/Slic3rIcons.sfd` is the upstream icon source and `resources-src/ReadMe.md` says the
  folder is not deployed. Nothing reads them; add the new SVGs there only if the folder is kept.
- `DesktopIntegrationDialog.cpp:419-433` (in `src/slic3r/GUI`, not built) writes a
  `PrusaSlicer-gcodeviewer` desktop entry and icon; it will need the same treatment if that tree is
  ever revived.
- `.github/ISSUE_TEMPLATE/*`, `.github/CONTRIBUTING.md` and the `.github/workflows/*` files are
  upstream: the workflows call `Prusa-Development/PrusaSlicer-Actions` by name, so they cannot work
  without that organisation. Not branding, and worth a separate todo.
- There is no installer script in this repository. The NSIS installer lives in a separate upstream
  build repository, so its strings, its shortcut names and its Start-menu entries have to be
  changed there; nothing in this tree can do it.

## Checklist for whoever renders the binaries

1. `python doc/sla-fork/tools/render_icons.py --dry-run`, then run it. It prints the backend it
   picked, so an unexpected Inkscape (or cairosvg) is visible before the files are written.
2. Look at the 16 px entry of `PrusaSlicer.ico` and the splash. The 16 px entry is the one that
   has to survive.
3. `git status` should show only the four binaries in `resources/icons/` as modified.
4. Commit the binaries, then do the "Changed in the same commit as the binaries" table, then
   re-check that nothing still greps as the old artwork.
