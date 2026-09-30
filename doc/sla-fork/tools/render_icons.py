#!/usr/bin/env python3
"""Rasterise the ResinSlicer vector sources into the binary icons the app loads.

    python doc/sla-fork/tools/render_icons.py            # icon + splash
    python doc/sla-fork/tools/render_icons.py --icon     # icon only
    python doc/sla-fork/tools/render_icons.py --splash   # splash only
    python doc/sla-fork/tools/render_icons.py --dry-run  # report, write nothing

The sources are the two SVGs in resources/icons/:

    resinslicer.svg         the app mark
    resinslicer-splash.svg  the splash screen, 600x540

and the outputs overwrite the files the build and the installers already name. The names keep
the upstream `PrusaSlicer` spelling on purpose: src/platform/msw/*.rc.in,
src/platform/osx/Info.plist.in, src/platform/unix/*.desktop and MainFrame.cpp all look those
paths up, so renaming them is a separate, coordinated change. doc/sla-fork/branding.md lists
every one of those references. The contents are ours; the file names are the build's.

    resources/icons/PrusaSlicer.ico          16 32 48 64 128 256
    resources/icons/PrusaSlicer.icns         16 32 64 128 256 512 1024 plus the @2x variants
    resources/icons/PrusaSlicer_128px.png    128, RGBA
    resources/icons/splashscreen.jpg         600x540, JPEG (SplashScreen.cpp loads it as JPEG)

Nothing here slices or reads a 3MF; it is a build-time asset step. Review the rendered PNGs
before committing the binaries: the 16 px entry is the one that has to survive.

Two backends turn an SVG into PNG bytes, picked with --backend {auto,cairosvg,inkscape}:

    auto      cairosvg when it imports, Inkscape 1.x otherwise (the default)
    cairosvg  python -m pip install cairosvg - which also needs the cairo C library
    inkscape  $INKSCAPE, else `inkscape` on PATH

They differ in that one step and nowhere else: the .ico, .icns, .png and the .jpg are all built
from those bytes by the same Pillow code, so either backend produces the same set of files.
Inkscape 1.2 and newer is the build that installs from the Microsoft Store, so on Windows that is
usually the one that is already there; its `inkscape` command exists only once the app execution
alias is switched on (Settings > Apps > Advanced app settings > App execution aliases > Inkscape)
and a new terminal has been opened afterwards. It exports one size per run, into a scratch folder
beside the outputs rather than in %TEMP%, so a long path or a temp dir the sandbox will not write
to cannot lose an otherwise good render; the folder goes away when the tool finishes, either way.

Pillow is needed whichever backend runs:

    python -m pip install Pillow

Exit status: 0 on success, 1 when a source is missing or unreadable, 2 when a dependency is
missing, 3 when a rendered file is empty.
"""
from __future__ import annotations

import argparse
import io
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
ICONS = REPO / "resources" / "icons"

ICON_SVG = ICONS / "resinslicer.svg"
SPLASH_SVG = ICONS / "resinslicer-splash.svg"

ICO_OUT = ICONS / "PrusaSlicer.ico"
ICNS_OUT = ICONS / "PrusaSlicer.icns"
ICON_PNG_OUT = ICONS / "PrusaSlicer_128px.png"
SPLASH_OUT = ICONS / "splashscreen.jpg"

# The sizes the current PrusaSlicer.ico carries.
ICO_SIZES = (16, 32, 48, 64, 128, 256)
ICON_PNG_SIZE = 128

# 600x540 is the size and 10:9 aspect of the splashscreen.jpg SplashScreen.cpp loads.
SPLASH_SIZE = (600, 540)

# icns type -> pixel size. ic07..ic10 are the 1x set, ic11..ic14 the @2x set, and icp4..icp6 the
# PNG-encoded 16/32/64 entries. Together they cover every size the old PrusaSlicer.icns had.
ICNS_ENTRIES = (
    ("icp4", 16),
    ("icp5", 32),
    ("icp6", 64),
    ("ic07", 128),
    ("ic08", 256),
    ("ic09", 512),
    ("ic10", 1024),
    ("ic11", 32),
    ("ic12", 64),
    ("ic13", 256),
    ("ic14", 512),
)
ICNS_SIZES = sorted({size for _, size in ICNS_ENTRIES})

# The environment variable that names the Inkscape executable when it is not on PATH.
INKSCAPE_ENV = "INKSCAPE"

# An export slower than this is a hung Inkscape rather than a slow one. 1.x can sit there after
# the file is written, and a tool that never returns is worse than one that fails.
INKSCAPE_TIMEOUT = 120

# The scratch folder the Inkscape exports go into, prefixed so it is recognisable if a run is
# killed before the cleanup and .gitignore can keep it out of the repository.
SCRATCH_PREFIX = ".render_icons-"


class Rasteriser:
    """One SVG in, PNG bytes out, at a requested pixel size.

    Nothing downstream cares which subclass produced the bytes, which is the point: the icon,
    the icon set, the PNG and the JPEG are all Pillow work that both backends share.
    """

    def name(self) -> str:
        """What to print, so a render says which backend made it."""
        raise NotImplementedError

    def png(self, svg: Path, width: int, height: int) -> bytes:
        raise NotImplementedError

    def close(self) -> None:
        """Release whatever the backend owns. The base backend owns nothing."""


class CairoSvgRasteriser(Rasteriser):
    """cairosvg, in process. Needs the cairo C library, which is why there is a second backend."""

    def __init__(self, cairosvg: object) -> None:
        self.cairosvg = cairosvg

    def name(self) -> str:
        return "cairosvg"

    def png(self, svg: Path, width: int, height: int) -> bytes:
        data = self.cairosvg.svg2png(
            url=str(svg),
            output_width=width,
            output_height=height,
            background_color=None,
        )
        if not data:
            raise RuntimeError(f"{svg.name} rendered empty at {width}x{height}")
        return data


class InkscapeRasteriser(Rasteriser):
    """The `inkscape` command line, one export per size.

    1.x has no multi-size export, so every size the outputs need is a separate run. Its default
    export background opacity is 0, which is what keeps the mark's alpha: the .ico entries and
    the window icon need a transparent background, not a white one.
    """

    def __init__(self, exe: str) -> None:
        self.exe = exe
        self.scratch = scratch_dir()

    def name(self) -> str:
        return f"inkscape ({self.exe})"

    def png(self, svg: Path, width: int, height: int) -> bytes:
        # Every size of a given SVG is rendered once, so name-after-size cannot collide.
        out = self.scratch / f"{svg.stem}-{width}x{height}.png"
        command = [
            self.exe,
            str(svg),
            "--export-type=png",
            f"--export-width={width}",
            f"--export-height={height}",
            f"--export-filename={out}",
        ]
        try:
            done = subprocess.run(
                command,
                capture_output=True,
                text=True,
                errors="replace",
                timeout=INKSCAPE_TIMEOUT,
            )
        except subprocess.TimeoutExpired:
            raise RuntimeError(
                f"{self.exe} wrote nothing for {svg.name} at {width}x{height} in {INKSCAPE_TIMEOUT}s; "
                "it may be waiting on a first-run dialog"
            ) from None
        except OSError as exc:
            raise RuntimeError(f"could not run {self.exe}: {exc}") from None
        if done.returncode != 0 or not out.is_file() or out.stat().st_size == 0:
            raise RuntimeError(f"{self.exe} failed on {svg.name} at {width}x{height}: {last_line(done)}")
        return out.read_bytes()

    def close(self) -> None:
        shutil.rmtree(self.scratch, ignore_errors=True)


def last_line(done: object) -> str:
    """A failed Inkscape run as one line: its exit code and whatever it said about it."""
    detail = (done.stderr or done.stdout or "").strip().splitlines()
    tail = detail[-1].strip() if detail else "no output"
    return f"exit {done.returncode}, last line: {tail}"


def scratch_dir() -> Path:
    """A scratch folder for the Inkscape exports, inside the output tree.

    Not %TEMP%: on Windows a long path or a temp dir the sandbox will not write to is a
    needlessly easy way to lose a render that would have worked, and the exports are not worth
    being somewhere the rest of the machine cannot see.
    """
    return Path(tempfile.mkdtemp(prefix=SCRATCH_PREFIX, dir=ICONS))


def import_cairosvg() -> object | None:
    """The cairosvg module, or None when it does not import.

    ImportError is the expected outcome on a Windows box without the cairo runtime, not a fault
    of the script, so it is caught here rather than left to crash the run.
    """
    try:
        import cairosvg
    except ImportError:
        return None
    return cairosvg


def find_inkscape() -> str | None:
    """The Inkscape executable, or None: $INKSCAPE first, then `inkscape` on PATH.

    The variable takes a full path or a bare name, so it can also point at a private build that
    is not on PATH, and at the .com beside the .exe when the console wrapper is wanted.
    """
    named = os.environ.get(INKSCAPE_ENV, "").strip()
    if named:
        return shutil.which(named) or (named if Path(named).is_file() else None)
    return shutil.which("inkscape")


def load_pillow() -> object:
    """Import Pillow, or exit(2) with what to install. Both backends need it."""
    try:
        from PIL import Image
    except ImportError as exc:
        print(
            f"render_icons.py needs Pillow to write .ico and .icns ({exc}).\n"
            "  python -m pip install Pillow",
            file=sys.stderr,
        )
        raise SystemExit(2)
    return Image


def cairosvg_hint() -> str:
    """What to do about a cairosvg that did not import."""
    return (
        "render_icons.py needs cairosvg to rasterise SVG, and it did not import.\n"
        "  python -m pip install cairosvg\n"
        "cairo is a C library: on Windows install a cairosvg wheel or the GTK/cairo runtime,\n"
        "on Debian/Ubuntu `apt install libcairo2`, on macOS `brew install cairo`."
    )


def inkscape_hint() -> str:
    """What to do about an Inkscape that cannot be found. The alias is the usual reason."""
    named = os.environ.get(INKSCAPE_ENV, "").strip()
    lines = ["render_icons.py needs Inkscape to rasterise SVG, and could not find it."]
    if named:
        lines.append(f"  the INKSCAPE environment variable names {named}, which is not an executable.")
    lines += [
        "The Microsoft Store build only puts `inkscape` on PATH once its app execution alias is",
        "switched on: Settings > Apps > Advanced app settings > App execution aliases > Inkscape,",
        "then open a new terminal, because PATH is read when one starts.",
        "Or point the INKSCAPE environment variable at the executable, e.g.",
        '  set INKSCAPE="C:\\Program Files\\Inkscape\\bin\\inkscape.com"',
    ]
    return "\n".join(lines)


def report_missing(*blocks: str) -> None:
    """Print what is missing, one block per way out, and exit 2."""
    for index, block in enumerate(blocks):
        if index:
            print(file=sys.stderr)
        print(block, file=sys.stderr)
    raise SystemExit(2)


def make_rasteriser(choice: str) -> Rasteriser:
    """The rasteriser --backend asks for, already checked, or exit(2) with what would fix it.

    `auto` prefers cairosvg because it is the backend this script was written against and it is
    the faster of the two by the length of a process launch per size; Inkscape is the fallback
    for the machines where cairo is not installed, which is the Windows case that motivated it.
    """
    if choice in ("auto", "cairosvg"):
        cairosvg = import_cairosvg()
        if cairosvg is not None:
            return CairoSvgRasteriser(cairosvg)

    if choice in ("auto", "inkscape"):
        inkscape = find_inkscape()
        if inkscape is not None:
            return InkscapeRasteriser(inkscape)

    if choice == "cairosvg":
        report_missing(cairosvg_hint(), "Or render with Inkscape instead: --backend inkscape.")
    elif choice == "inkscape":
        report_missing(inkscape_hint(), "Or render with cairosvg instead: --backend cairosvg.")
    else:
        report_missing(cairosvg_hint(), inkscape_hint())


def describe_backend(choice: str) -> str:
    """What --backend would use now, reported without failing when nothing is installed.

    A dry run is for answering "what would this do", and a machine with neither backend is
    exactly the one that wants to read that.
    """
    cairosvg = import_cairosvg() if choice in ("auto", "cairosvg") else None
    inkscape = find_inkscape() if choice in ("auto", "inkscape") else None
    if choice == "cairosvg":
        return "cairosvg" if cairosvg is not None else "cairosvg, which did not import"
    if choice == "inkscape":
        return f"inkscape ({inkscape})" if inkscape else "inkscape, which was not found"
    if cairosvg is not None:
        return "cairosvg, because it imported (auto)"
    if inkscape is not None:
        return f"inkscape ({inkscape}), because cairosvg did not import (auto)"
    return "none: neither cairosvg nor Inkscape is available"


def open_rgba(Image: object, png: bytes) -> object:
    """Open rasterised PNG bytes as RGBA, whichever backend handed them back."""
    image = Image.open(io.BytesIO(png))
    image.load()
    return image if image.mode == "RGBA" else image.convert("RGBA")


def png_bytes(image: object) -> bytes:
    buffer = io.BytesIO()
    image.save(buffer, "PNG")
    return buffer.getvalue()


def write_ico(images: dict[int, object], out: Path) -> None:
    """Write a multi-size Windows icon. PNG-compressed entries, which Windows has read
    since Vista and which keep the alpha channel; the old .ico used raw DIB below 256."""
    base = images[max(ICO_SIZES)]
    base.save(
        out,
        format="ICO",
        sizes=[(size, size) for size in ICO_SIZES],
        append_images=[images[size] for size in ICO_SIZES],
    )


def write_icns(out: Path, images: dict[int, bytes]) -> None:
    """Write a macOS icon set by hand.

    Pillow can only emit the eight fixed ic07..ic14 entries, so the container is assembled here
    instead: an `icns` header, a TOC listing every chunk, then the chunks, each one a PNG.
    """
    entries = [(kind.encode("ascii"), images[size]) for kind, size in ICNS_ENTRIES]
    toc = b"TOC " + struct.pack(">i", 8 + 8 * len(entries))
    for kind, payload in entries:
        toc += kind + struct.pack(">i", 8 + len(payload))
    body = b"".join(kind + struct.pack(">i", 8 + len(payload)) + payload for kind, payload in entries)
    total = 8 + len(toc) + len(body)
    out.write_bytes(b"icns" + struct.pack(">i", total) + toc + body)


def check_sources() -> list[Path]:
    """Return the sources that are missing, complaining about each."""
    missing = [svg for svg in (ICON_SVG, SPLASH_SVG) if not svg.is_file()]
    for svg in missing:
        print(f"render_icons.py: missing source {rel(svg)}", file=sys.stderr)
    return missing


def rel(path: Path) -> str:
    """Repo-relative path with forward slashes, whatever the platform separator is."""
    try:
        return path.relative_to(REPO).as_posix()
    except ValueError:
        return path.as_posix()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    which = parser.add_mutually_exclusive_group()
    which.add_argument("--icon", action="store_true", help="render the app icon only")
    which.add_argument("--splash", action="store_true", help="render the splash screen only")
    parser.add_argument(
        "--dry-run",
        "--check",
        dest="dry_run",
        action="store_true",
        help="report the sources, the files that would be written and the backend, then stop",
    )
    parser.add_argument(
        "--backend",
        choices=("auto", "cairosvg", "inkscape"),
        default="auto",
        help="how the SVGs are rasterised: auto uses cairosvg when it imports and Inkscape "
        "otherwise (default: auto)",
    )
    args = parser.parse_args(argv)

    want_icon = args.icon or not args.splash
    want_splash = args.splash or not args.icon

    if check_sources():
        return 1

    if args.dry_run:
        print("sources:")
        for svg in (ICON_SVG, SPLASH_SVG):
            print(f"  {rel(svg)}")
        print("outputs:")
        if want_icon:
            print(f"  {rel(ICO_OUT)}  {', '.join(str(s) for s in ICO_SIZES)}")
            print(f"  {rel(ICNS_OUT)}  {', '.join(str(s) for s in ICNS_SIZES)}")
            print(f"  {rel(ICON_PNG_OUT)}  {ICON_PNG_SIZE}")
        if want_splash:
            print(f"  {rel(SPLASH_OUT)}  {SPLASH_SIZE[0]}x{SPLASH_SIZE[1]}")
        print("backend:")
        print(f"  {describe_backend(args.backend)}")
        return 0

    Image = load_pillow()
    rasteriser = make_rasteriser(args.backend)
    print(f"rendering with {rasteriser.name()}")

    written: list[Path] = []

    try:
        if want_icon:
            rendered = {}
            for size in sorted(set(ICO_SIZES) | {ICON_PNG_SIZE} | set(ICNS_SIZES)):
                rendered[size] = open_rgba(Image, rasteriser.png(ICON_SVG, size, size))

            write_ico({s: rendered[s] for s in ICO_SIZES}, ICO_OUT)
            written.append(ICO_OUT)

            write_icns(ICNS_OUT, {size: png_bytes(rendered[size]) for size in ICNS_SIZES})
            written.append(ICNS_OUT)

            rendered[ICON_PNG_SIZE].save(ICON_PNG_OUT, "PNG")
            written.append(ICON_PNG_OUT)

        if want_splash:
            width, height = SPLASH_SIZE
            splash = open_rgba(Image, rasteriser.png(SPLASH_SVG, width, height))
            # SplashScreen.cpp reads this file as a JPEG, so it has to be one. The SVG is fully
            # opaque; if that ever stops being true, JPEG turns the transparent pixels black.
            if splash.getextrema()[3] != (255, 255):
                print(
                    f"render_icons.py: warning, {rel(SPLASH_SVG)} rendered with transparent pixels; "
                    "they will be black in the JPEG. Give the SVG a full-bleed background rect.",
                    file=sys.stderr,
                )
            # 4:4:4 chroma, so the palette edges survive the JPEG round trip.
            splash.convert("RGB").save(SPLASH_OUT, "JPEG", quality=92, subsampling=0)
            written.append(SPLASH_OUT)
    except (RuntimeError, OSError, ValueError) as exc:
        print(f"render_icons.py: {exc}", file=sys.stderr)
        return 1
    finally:
        rasteriser.close()

    for path in written:
        if not path.is_file() or path.stat().st_size == 0:
            print(f"render_icons.py: {path.name} is missing or empty after writing", file=sys.stderr)
            return 3
        print(f"wrote {rel(path)} ({path.stat().st_size} bytes)")

    print("Review the 16 px entry and the splash, then commit the binaries.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
