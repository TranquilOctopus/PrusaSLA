#!/usr/bin/env python3
"""Compare SLA visual regression renders (PLAN G3 / roadmap M6.2).

    python doc/sla-fork/tools/visual_diff.py compare RENDER.png REFERENCE.png
    python doc/sla-fork/tools/visual_diff.py lightness RENDER.png.json
    python doc/sla-fork/tools/visual_diff.py grayscale RENDER.png OUT.png
    python doc/sla-fork/tools/visual_diff.py check --render a.png --reference b.png --sidecar a.png.json

compare
    Per-pixel difference between a render and a stored reference. Reports the percentage of
    pixels that changed, the biggest channel delta, the mean delta over the changed pixels and
    the bounding box of the change, and exits 1 when more than --tolerance-pct of the pixels
    changed. A pixel counts as changed when any of its channels moves by more than
    --channel-tolerance, which keeps a render stable against the last bit of a driver rounding
    on a different GPU. With --diff-out a grayscale difference image is written: what changed is
    scaled up and everything else is dimmed to a tenth.

lightness
    The grayscale check PLAN 2.1 asks for: the model, the supports and the pad have to be told
    apart from each other and from the background by lightness, not by hue. The render harness
    writes the CIE L* of the theme tokens it drew with into a sidecar JSON next to every PNG, and
    this reads them back and fails when two of those are closer than --threshold L*. The
    background is a gradient, so each object role is compared against both of its ends and the
    two ends are never compared with each other, which is what PLAN 2.1 allows for surface
    layering. The threshold is a just noticeable lightness difference; the palette of PLAN 2.1
    keeps its tightest pair (SlaPad against SlaSupport in the dark theme) at 16 L*, so the check
    is a tripwire for a palette change that collapses two roles, not a limit the palette is
    fighting today. Nothing here knows a hex value: the numbers come from the app, which resolves
    them from the tokens. With --probes the same check also runs on the rendered pixels, using
    the rectangles in the probe file (a role name to [x, y, w, h] map) and the median L* of each
    rectangle, which is what catches a light source that flattens two roles that are well apart
    on paper.

grayscale
    Writes the L* encoded grayscale of a render, the grayscale variant of G3, and prints the L*
    percentiles so a change in the spread is visible without opening the image.

check
    compare plus lightness in one run, for CI: one exit code for both.

The exit code is 0 when everything passed and 1 when a comparison or a check failed, so a CI job
(M0.14) can gate on it. Only the Python standard library is used, on purpose: the tool has to run
where the render runs. PNG reading covers the 8 bit greyscale, RGB, greyscale+alpha and RGBA
non-interlaced images the app writes.
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
import zlib

# Roles the grayscale check looks at, in reporting order. The names match the keys of the
# "lightness" object in the sidecar the harness writes. The background is a gradient, so it has
# two ends and neither of them is a role of its own: every object role is compared against both
# ends, and the two ends are never compared with each other. PLAN 2.1 says as much about
# Slate700 vs Slate900: surface layering only, never the only cue for information.
OBJECT_ROLES = ["model", "supports", "pad"]
BACKGROUND_ROLES = ["background_top", "background_bottom"]
ROLES = OBJECT_ROLES + BACKGROUND_ROLES


def role_pairs() -> list[tuple[str, str]]:
    """The pairs the lightness check compares, each one once."""
    pairs = []
    for i, first in enumerate(OBJECT_ROLES):
        for second in OBJECT_ROLES[i + 1:]:
            pairs.append((first, second))
    for first in OBJECT_ROLES:
        for second in BACKGROUND_ROLES:
            pairs.append((first, second))
    return pairs

# A just noticeable lightness difference in CIE L*. See the module docstring.
DEFAULT_THRESHOLD = 10.0

# Default tolerances of compare(): a percentage of pixels, and a per channel delta in 0..255.
DEFAULT_TOLERANCE_PCT = 0.5
DEFAULT_CHANNEL_TOLERANCE = 8


class Image:
    """A decoded 8 bit image: width, height, channels and rows-major pixel bytes."""

    def __init__(self, width: int, height: int, channels: int, pixels: bytes) -> None:
        self.width = width
        self.height = height
        self.channels = channels
        self.pixels = pixels

    def pixel(self, x: int, y: int) -> tuple[int, ...]:
        offset = (y * self.width + x) * self.channels
        return tuple(self.pixels[offset:offset + self.channels])

    def grey(self) -> "Image":
        """The same image as one channel holding CIE L*, so the greyscale PNG of the grayscale
        check can be read as the lightness of a pixel straight off the byte."""
        if self.channels == 1:
            return self
        out = bytearray(self.width * self.height)
        for i in range(self.width * self.height):
            out[i] = min(255, max(0, int(round(lightness(self.pixel(i % self.width, i // self.width))))))
        return Image(self.width, self.height, 1, bytes(out))

    def crop_rect(self, rect: list[int]) -> "Image":
        x, y, w, h = rect
        out = bytearray()
        for row in range(y, y + h):
            if row < 0 or row >= self.height:
                continue
            for col in range(x, x + w):
                if col < 0 or col >= self.width:
                    continue
                out += bytes(self.pixel(col, row))
        return Image(w, h, self.channels, bytes(out))


def luma(rgb: tuple[int, int, int]) -> int:
    """The Rec. 601 luma of an RGB triple. Only the difference image uses it, to dim the pixels
    that did not change; the lightness check and the greyscale variant go through CIE L*, so that
    the numbers are the ones PLAN 2.1 talks about."""
    r, g, b = rgb[:3]
    return (r * 299 + g * 587 + b * 114) // 1000


def srgb_to_linear(channel: int) -> float:
    value = channel / 255.0
    if value <= 0.04045:
        return value / 12.92
    return ((value + 0.055) / 1.055) ** 2.4


def lightness(rgb: tuple[int, int, int]) -> float:
    """CIE 1976 L* of an sRGB triple, 0 for black and 100 for white. The luminance is the
    Rec. 709 weighted sum of the linear channels, which is what a greyscale conversion keeps."""
    y = (
        0.2126 * srgb_to_linear(rgb[0])
        + 0.7152 * srgb_to_linear(rgb[1])
        + 0.0722 * srgb_to_linear(rgb[2])
    )
    if y > 216.0 / 24389.0:
        return 116.0 * y ** (1.0 / 3.0) - 16.0
    return 903.3 * y


def read_png(path: str) -> Image:
    """Decode an 8 bit, non-interlaced PNG. Only what the app writes, on purpose."""
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit(f"{path}: not a PNG file")

    width = height = channels = 0
    bit_depth = color_type = interlace = 0
    idat = bytearray()
    offset = 8
    while offset + 8 <= len(data):
        (length,) = struct.unpack(">I", data[offset:offset + 4])
        chunk_type = data[offset + 4:offset + 8]
        body = data[offset + 8:offset + 8 + length]
        offset += 12 + length
        if chunk_type == b"IHDR":
            width, height, bit_depth, color_type, _compression, _filter, interlace = struct.unpack(
                ">IIBBBBB", body
            )
        elif chunk_type == b"IDAT":
            idat += body
        elif chunk_type == b"IEND":
            break

    if bit_depth != 8:
        sys.exit(f"{path}: {bit_depth} bit PNG, only 8 bit is supported")
    if interlace != 0:
        sys.exit(f"{path}: interlaced PNG, only non-interlaced is supported")
    channels = {0: 1, 2: 3, 4: 2, 6: 4}.get(color_type, 0)
    if channels == 0:
        sys.exit(f"{path}: unsupported PNG colour type {color_type}")

    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    pixels = bytearray(height * stride)
    previous = bytearray(stride)
    pos = 0
    for row in range(height):
        filter_type = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos + stride])
        pos += stride
        unfilter(line, filter_type, previous, channels)
        pixels[row * stride:(row + 1) * stride] = line
        previous = line
    return Image(width, height, channels, bytes(pixels))


def unfilter(line: bytearray, filter_type: int, previous: bytearray, channels: int) -> None:
    """Undo one PNG scanline filter in place (RFC 2083 section 6)."""
    if filter_type == 0:
        return
    for i in range(len(line)):
        left = line[i - channels] if i >= channels else 0
        up = previous[i]
        up_left = previous[i - channels] if i >= channels else 0
        if filter_type == 1:
            line[i] = (line[i] + left) & 0xFF
        elif filter_type == 2:
            line[i] = (line[i] + up) & 0xFF
        elif filter_type == 3:
            line[i] = (line[i] + (left + up) // 2) & 0xFF
        elif filter_type == 4:
            line[i] = (line[i] + paeth(left, up, up_left)) & 0xFF
        else:
            sys.exit(f"unsupported PNG filter type {filter_type}")


def paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def write_png_gray(path: str, image: Image) -> None:
    """Write an 8 bit greyscale, non-interlaced PNG."""
    raw = bytearray()
    for row in range(image.height):
        raw += b"\x00"
        raw += image.pixels[row * image.width:(row + 1) * image.width]

    def chunk(chunk_type: bytes, body: bytes) -> bytes:
        return (
            struct.pack(">I", len(body))
            + chunk_type
            + body
            + struct.pack(">I", zlib.crc32(chunk_type + body) & 0xFFFFFFFF)
        )

    header = struct.pack(">IIBBBBB", image.width, image.height, 8, 0, 0, 0, 0)
    with open(path, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n")
        handle.write(chunk(b"IHDR", header))
        handle.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        handle.write(chunk(b"IEND", b""))


def percentiles(values: list[float], fractions: list[float]) -> list[float]:
    if not values:
        return [0.0 for _ in fractions]
    ordered = sorted(values)
    out = []
    for fraction in fractions:
        index = min(len(ordered) - 1, max(0, int(round(fraction * (len(ordered) - 1)))))
        out.append(ordered[index])
    return out


def command_compare(args: argparse.Namespace) -> int:
    render = read_png(args.render)
    reference = read_png(args.reference)
    if (render.width, render.height) != (reference.width, reference.height):
        print(f"size differs: render {render.width}x{render.height}, "
              f"reference {reference.width}x{reference.height}")
        return 1
    if render.channels != reference.channels:
        print(f"channels differ: render {render.channels}, reference {reference.channels}")
        return 1

    total = render.width * render.height
    changed = 0
    max_delta = 0
    delta_sum = 0
    min_x, min_y, max_x, max_y = render.width, render.height, -1, -1
    diff = bytearray(total) if args.diff_out else None
    for y in range(render.height):
        for x in range(render.width):
            a = render.pixel(x, y)
            b = reference.pixel(x, y)
            delta = max(abs(a[i] - b[i]) for i in range(len(a)))
            if delta > args.channel_tolerance:
                changed += 1
                delta_sum += delta
                max_delta = max(max_delta, delta)
                min_x, min_y = min(min_x, x), min(min_y, y)
                max_x, max_y = max(max_x, x), max(max_y, y)
                if diff is not None:
                    diff[y * render.width + x] = 255
            elif diff is not None:
                diff[y * render.width + x] = luma(a) // 10
    if diff is not None:
        write_png_gray(args.diff_out, Image(render.width, render.height, 1, bytes(diff)))

    changed_pct = 100.0 * changed / total if total else 0.0
    box = "none"
    if max_x >= 0:
        box = f"x {min_x}..{max_x}, y {min_y}..{max_y}"
    mean = delta_sum / changed if changed else 0.0
    print(f"pixels: {total} ({render.width}x{render.height}x{render.channels})")
    print(f"changed: {changed} ({changed_pct:.4f} %, tolerance {args.tolerance_pct} %)")
    print(f"max channel delta: {max_delta}, mean over changed: {mean:.1f}")
    print(f"bounding box of the change: {box}")
    if changed and changed_pct <= args.tolerance_pct:
        print("within tolerance")
    return 0 if changed_pct <= args.tolerance_pct else 1


def load_sidecar(path: str) -> dict:
    with open(path, encoding="utf-8") as handle:
        document = json.load(handle)
    if "lightness" not in document:
        sys.exit(f"{path}: not a render sidecar (no lightness object)")
    return document


def read_probe_file(path: str) -> dict:
    with open(path, encoding="utf-8") as handle:
        document = json.load(handle)
    if "probes" not in document:
        sys.exit(f"{path}: not a probe file (no probes object)")
    return document["probes"]


def command_lightness(args: argparse.Namespace) -> int:
    document = load_sidecar(args.sidecar)
    roles = document["lightness"]
    failed = 0
    print(f"sidecar: {args.sidecar}"
          f" (view {document.get('view', 'unknown')}, "
          f"{document.get('width', '?')}x{document.get('height', '?')})")
    for name in ROLES:
        if name not in roles:
            sys.exit(f"{args.sidecar}: no lightness for role '{name}'")
    for name in ROLES:
        entry = roles[name]
        print(f"  {name:17s} {entry.get('token', '?'):18s} "
              f"L* {entry['L']:6.2f}  rgb {entry.get('rgb', '?')}")

    for first, second in role_pairs():
        delta = abs(roles[first]["L"] - roles[second]["L"])
        verdict = "ok" if delta >= args.threshold else "TOO CLOSE"
        if delta < args.threshold:
            failed += 1
        print(f"  dL* {first:17s} {second:17s} {delta:6.2f} "
              f"(threshold {args.threshold}) {verdict}")

    if args.probes:
        render_path = args.render
        if not render_path and args.sidecar.endswith(".json"):
            render_path = args.sidecar[: -len(".json")]
        render_path = render_path or args.sidecar
        render = read_png(render_path)
        probes = read_probe_file(args.probes)
        measured = {}
        for name in ROLES:
            if name not in probes:
                sys.exit(f"{args.probes}: no probe rectangle for role '{name}'")
            rect = probes[name]
            if not isinstance(rect, list) or len(rect) != 4:
                sys.exit(f"{args.probes}: probe '{name}' is not [x, y, width, height]")
            x, y, w, h = rect
            inside = (x >= 0 and y >= 0 and w >= 1 and h >= 1
                      and x + w <= render.width and y + h <= render.height)
            if not inside:
                sys.exit(f"{args.probes}: probe '{name}' {rect} is not inside "
                         f"{render.width}x{render.height}")
            crop = render.crop_rect(rect)
            values = [lightness(crop.pixel(i % crop.width, i // crop.width)[:3])
                      for i in range(crop.width * crop.height)]
            median = sorted(values)[len(values) // 2]
            measured[name] = median
            print(f"  probe {name:17s} {str(rect):22s} median L* {median:6.2f} "
                  f"(token {roles[name]['L']:6.2f}, shading "
                  f"{median - roles[name]['L']:+6.2f})")
        for first, second in role_pairs():
            delta = abs(measured[first] - measured[second])
            verdict = "ok" if delta >= args.threshold else "TOO CLOSE"
            if delta < args.threshold:
                failed += 1
            print(f"  probe dL* {first:17s} {second:17s} {delta:6.2f} "
                  f"(threshold {args.threshold}) {verdict}")
    return 0 if failed == 0 else 1


def command_grayscale(args: argparse.Namespace) -> int:
    image = read_png(args.render)
    grey = image.grey()
    write_png_gray(args.output, grey)
    values = [float(byte) for byte in grey.pixels]
    p1, p5, p50, p95, p99 = percentiles(values, [0.01, 0.05, 0.50, 0.95, 0.99])
    print(f"wrote {args.output} ({grey.width}x{grey.height} greyscale, L* encoded)")
    print(f"L* percentiles: p1 {p1:.2f}, p5 {p5:.2f}, p50 {p50:.2f}, "
          f"p95 {p95:.2f}, p99 {p99:.2f}")
    return 0


def command_check(args: argparse.Namespace) -> int:
    compare_args = argparse.Namespace(
        render=args.render,
        reference=args.reference,
        tolerance_pct=args.tolerance_pct,
        channel_tolerance=args.channel_tolerance,
        diff_out=args.diff_out,
    )
    print("== compare ==")
    failed = command_compare(compare_args)
    if args.sidecar:
        print("== lightness ==")
        lightness_args = argparse.Namespace(
            sidecar=args.sidecar,
            threshold=args.threshold,
            probes=args.probes,
            render=args.render,
        )
        failed += command_lightness(lightness_args)
    if args.grayscale:
        print("== grayscale ==")
        grayscale_args = argparse.Namespace(render=args.render, output=args.grayscale)
        command_grayscale(grayscale_args)
    print("FAILED" if failed else "PASSED")
    return 0 if failed == 0 else 1


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    subparsers = parser.add_subparsers(dest="command", required=True)

    compare = subparsers.add_parser("compare", help="per pixel diff of a render and a reference")
    compare.add_argument("render", help="the render to check")
    compare.add_argument("reference", help="the stored reference")
    compare.add_argument("--tolerance-pct", type=float, default=DEFAULT_TOLERANCE_PCT,
                         help=f"percentage of changed pixels that still passes "
                              f"(default {DEFAULT_TOLERANCE_PCT})")
    compare.add_argument("--channel-tolerance", type=int, default=DEFAULT_CHANNEL_TOLERANCE,
                         help=f"per channel delta in 0..255 that does not count as changed "
                              f"(default {DEFAULT_CHANNEL_TOLERANCE})")
    compare.add_argument("--diff-out", help="write a grayscale difference image here")
    compare.set_defaults(func=command_compare)

    lightness_parser = subparsers.add_parser(
        "lightness", help="check that the roles differ in lightness (PLAN 2.1, G3)")
    lightness_parser.add_argument("sidecar", help="the sidecar JSON next to the render")
    lightness_parser.add_argument("--threshold", type=float, default=DEFAULT_THRESHOLD,
                                  help=f"smallest allowed dL* between two roles "
                                       f"(default {DEFAULT_THRESHOLD})")
    lightness_parser.add_argument("--probes", help="probe file, to check the rendered pixels too")
    lightness_parser.add_argument("--render", help="the render the probe rectangles are in, "
                                                   "defaults to the sidecar without .json")
    lightness_parser.set_defaults(func=command_lightness)

    grayscale = subparsers.add_parser("grayscale", help="write the L* greyscale of a render")
    grayscale.add_argument("render", help="the render to convert")
    grayscale.add_argument("output", help="the greyscale PNG to write")
    grayscale.set_defaults(func=command_grayscale)

    check = subparsers.add_parser("check", help="compare plus lightness, for CI")
    check.add_argument("--render", required=True, help="the render to check")
    check.add_argument("--reference", required=True, help="the stored reference")
    check.add_argument("--sidecar", help="the sidecar JSON of the render")
    check.add_argument("--probes", help="probe file, to check the rendered pixels too")
    check.add_argument("--threshold", type=float, default=DEFAULT_THRESHOLD,
                       help=f"smallest allowed dL* between two roles (default {DEFAULT_THRESHOLD})")
    check.add_argument("--tolerance-pct", type=float, default=DEFAULT_TOLERANCE_PCT,
                       help=f"percentage of changed pixels that still passes "
                            f"(default {DEFAULT_TOLERANCE_PCT})")
    check.add_argument("--channel-tolerance", type=int, default=DEFAULT_CHANNEL_TOLERANCE,
                       help=f"per channel delta in 0..255 that does not count as changed "
                            f"(default {DEFAULT_CHANNEL_TOLERANCE})")
    check.add_argument("--diff-out", help="write a grayscale difference image here")
    check.add_argument("--grayscale", help="write the L* greyscale of the render here")
    check.set_defaults(func=command_check)

    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
