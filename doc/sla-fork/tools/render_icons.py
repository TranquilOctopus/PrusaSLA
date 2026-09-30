#!/usr/bin/env python3
"""Rasterise the ResinSlicer vector sources into the binary icons the app loads.

    python doc/sla-fork/tools/render_icons.py            # icon + splash
    python doc/sla-fork/tools/render_icons.py --icon     # icon only
    python doc/sla-fork/tools/render_icons.py --splash   # splash only
    python doc/sla-fork/tools/render_icons.py --check    # report, write nothing

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

Requires cairosvg (which needs the cairo C library) and Pillow:

    python -m pip install cairosvg Pillow

Exit status: 0 on success, 1 when a source is missing or unreadable, 2 when a dependency is
missing, 3 when a rendered file is empty.
"""
from __future__ import annotations

import argparse
import io
import struct
import sys
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


def load_dependencies() -> tuple[object, object]:
    """Import cairosvg and Pillow, or exit(2) with what to install."""
    try:
        import cairosvg
    except ImportError as exc:
        print(
            f"render_icons.py needs cairosvg to rasterise SVG ({exc}).\n"
            "  python -m pip install cairosvg\n"
            "cairo is a C library: on Windows install a cairosvg wheel or the GTK/cairo runtime,\n"
            "on Debian/Ubuntu `apt install libcairo2`, on macOS `brew install cairo`.",
            file=sys.stderr,
        )
        raise SystemExit(2)
    try:
        from PIL import Image
    except ImportError as exc:
        print(
            f"render_icons.py needs Pillow to write .ico and .icns ({exc}).\n"
            "  python -m pip install Pillow",
            file=sys.stderr,
        )
        raise SystemExit(2)
    return cairosvg, Image


def render_png(cairosvg: object, svg: Path, width: int, height: int) -> bytes:
    """Rasterise one SVG at one size and return the PNG bytes."""
    data = cairosvg.svg2png(
        url=str(svg),
        output_width=width,
        output_height=height,
        background_color=None,
    )
    if not data:
        raise RuntimeError(f"{svg.name} rendered empty at {width}x{height}")
    return data


def open_rgba(Image: object, png: bytes) -> object:
    """Open rasterised PNG bytes as RGBA, whatever cairosvg handed back."""
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
    parser.add_argument("--check", action="store_true", help="report what would be written and stop")
    args = parser.parse_args(argv)

    want_icon = args.icon or not args.splash
    want_splash = args.splash or not args.icon

    if check_sources():
        return 1

    if args.check:
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
        return 0

    cairosvg, Image = load_dependencies()

    written: list[Path] = []

    try:
        if want_icon:
            rendered = {}
            for size in sorted(set(ICO_SIZES) | {ICON_PNG_SIZE} | set(ICNS_SIZES)):
                rendered[size] = open_rgba(Image, render_png(cairosvg, ICON_SVG, size, size))

            write_ico({s: rendered[s] for s in ICO_SIZES}, ICO_OUT)
            written.append(ICO_OUT)

            write_icns(ICNS_OUT, {size: png_bytes(rendered[size]) for size in ICNS_SIZES})
            written.append(ICNS_OUT)

            rendered[ICON_PNG_SIZE].save(ICON_PNG_OUT, "PNG")
            written.append(ICON_PNG_OUT)

        if want_splash:
            width, height = SPLASH_SIZE
            splash = open_rgba(Image, render_png(cairosvg, SPLASH_SVG, width, height))
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

    for path in written:
        if not path.is_file() or path.stat().st_size == 0:
            print(f"render_icons.py: {path.name} is missing or empty after writing", file=sys.stderr)
            return 3
        print(f"wrote {rel(path)} ({path.stat().st_size} bytes)")

    print("Review the 16 px entry and the splash, then commit the binaries.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
