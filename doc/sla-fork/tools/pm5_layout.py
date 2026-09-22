#!/usr/bin/env python3
"""Structural summary of a Photon Workshop .pm5 file, and a layout comparison of two files.

    python doc/sla-fork/tools/pm5_layout.py FILE
    python doc/sla-fork/tools/pm5_layout.py --compare REFERENCE CANDIDATE

The summary covers layout only: block order, declared lengths, field types and the constants that
should match between any two .pm5 files. Values that legitimately differ between prints (resolution
when not an M5, layer count, layer data, exposure, the model's bounding box) are reported but not
compared. See doc/sla-fork/formats/pm5.md for what each field means.

Exit status for --compare: 0 when the layouts match, 1 when they differ.
"""
from __future__ import annotations

import struct
import sys

SLOTS = ["header", "software", "preview", "colour_table", "layerdef", "extra", "machine",
         "first_layer", "model"]
NAMED = {"header": "HEADER", "preview": "PREVIEW", "layerdef": "LAYERDEF", "extra": "EXTRA",
         "machine": "MACHINE", "model": "MODEL"}


def read(path: str) -> dict:
    data = open(path, "rb").read()
    u32 = lambda o: struct.unpack_from("<I", data, o)[0]
    f32 = lambda o: struct.unpack_from("<f", data, o)[0]
    cstr = lambda o, n: data[o:o + n].split(b"\0")[0].decode("ascii", "replace")

    s: dict = {"size": len(data), "magic": data[:12], "version": u32(12), "area_num": u32(16)}
    addr = {name: u32(20 + 4 * i) for i, name in enumerate(SLOTS)}
    s["address_order_in_file"] = [n for n, _ in sorted(addr.items(), key=lambda kv: kv[1])]
    s["names"] = {slot: cstr(addr[slot], 12) for slot in NAMED}
    s["declared_len"] = {slot: u32(addr[slot] + 12) for slot in NAMED}

    h = addr["header"] + 16
    s["header"] = {
        "pixel_um": f32(h), "layer_height": f32(h + 4), "antialias": u32(h + 40),
        "res": (u32(h + 44), u32(h + 48)), "tail_u32": (u32(h + 80), u32(h + 84), u32(h + 88)),
    }
    p = addr["preview"] + 16
    s["preview"] = {"w": u32(p), "mid": u32(p + 4), "h": u32(p + 8)}
    c = addr["colour_table"]
    s["colour_table"] = data[c:c + 28].hex(" ")

    ld = addr["layerdef"] + 16
    count = u32(ld)
    s["layer_count"] = count
    s["layerdef_entry_bytes"] = (s["declared_len"]["layerdef"] - 4) // count if count else 0
    s["first_layer_matches_slot"] = count > 0 and u32(ld + 4) == addr["first_layer"]

    m = addr["machine"] + 16
    s["machine"] = {"name": cstr(m, 96), "image_format": cstr(m + 96, 16),
                    "u32s": (u32(m + 112), u32(m + 116)), "version": u32(m + 132)}
    s["software_name"] = cstr(addr["software"], 32)
    return s


# Fields that must be identical in any two .pm5 files for the printer to read them the same way.
MUST_MATCH = ["magic", "version", "area_num", "address_order_in_file", "names", "colour_table",
              "layerdef_entry_bytes", "first_layer_matches_slot"]


def compare(ref: dict, cand: dict) -> list[str]:
    problems = [f"{k}: reference {ref[k]!r}, candidate {cand[k]!r}"
                for k in MUST_MATCH if ref[k] != cand[k]]
    for slot in ("header", "extra", "machine", "model"):
        if ref["declared_len"][slot] != cand["declared_len"][slot]:
            problems.append(f"declared length of {slot}: reference {ref['declared_len'][slot]}, "
                            f"candidate {cand['declared_len'][slot]}")
    for key in ("antialias", "tail_u32"):
        if ref["header"][key] != cand["header"][key]:
            problems.append(f"header {key}: reference {ref['header'][key]}, candidate {cand['header'][key]}")
    for key in ("w", "mid", "h"):
        if ref["preview"][key] != cand["preview"][key]:
            problems.append(f"preview {key}: reference {ref['preview'][key]}, candidate {cand['preview'][key]}")
    for key in ("image_format", "u32s", "version"):
        if ref["machine"][key] != cand["machine"][key]:
            problems.append(f"machine {key}: reference {ref['machine'][key]!r}, "
                            f"candidate {cand['machine'][key]!r}")
    return problems


def main(argv: list[str]) -> int:
    if len(argv) == 2:
        for k, v in read(argv[1]).items():
            print(f"{k}: {v}")
        return 0
    if len(argv) == 4 and argv[1] == "--compare":
        problems = compare(read(argv[2]), read(argv[3]))
        for p in problems:
            print("DIFFERS", p)
        print("layouts match" if not problems else f"{len(problems)} difference(s)")
        return 1 if problems else 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
