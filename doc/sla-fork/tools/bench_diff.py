#!/usr/bin/env python3
"""Compare two SLA benchmark metrics files (tests/sla_print/sla_benchmark_tests.cpp).

    python doc/sla-fork/tools/bench_diff.py BEFORE.json AFTER.json
    python doc/sla-fork/tools/bench_diff.py BEFORE.json AFTER.json --changed

Models are keyed on their `id`, which is what the harness puts in the metrics file: the corpus is
not redistributable, so the files themselves are never named. Prints one row per model and metric
with the before value, the after value, the delta and the delta in percent, then the same for the
summary. Timings vary between runs, so they are noise in a percent column; read them as "faster"
or "slower", not as a regression of a tenth of a percent. Only numbers are compared: the id and
the category are printed as labels.

`layer_hash` is a hash of the sliced layers and is the one row that must not change: a different
hash means the same model sliced to different geometry, which is a real change and not a run to
run wobble. It is not a number, so it is compared as a string and reported separately, at the top
of the output and in a row of its own.

Models that are only in one file are listed as added or removed. --changed hides the rows whose
value did not change, which is what a pull request comment wants. A changed layer hash is always
reported, --changed or not, and the exit code is 1 when one changed, so CI (M0.14) can fail on a
geometry change it did not expect.
"""
from __future__ import annotations

import argparse
import json
import sys

# The metrics the harness writes, in the order of the JSON, with their unit for the header.
METRICS = [
    ("triangles", ""),
    ("support_points", ""),
    ("support_tree_triangles", ""),
    ("support_tree_volume_mm3", "mm3"),
    ("pad_volume_mm3", "mm3"),
    ("layer_count", ""),
    ("islands_detected", ""),
    ("islands_without_support_point", ""),
    ("t_points_ms", "ms"),
    ("t_tree_pad_ms", "ms"),
    ("t_slice_ms", "ms"),
    ("t_total_ms", "ms"),
    ("peak_working_set_bytes", "B"),
]
KEYS = [key for key, _ in METRICS]

# The one key that has to match exactly. Not in METRICS: it is a hex string, not a number.
HASH_KEY = "layer_hash"

# How much a value may differ before it counts as changed. Timings never match exactly.
EPSILON = {"t_points_ms": 0.5, "t_tree_pad_ms": 0.5, "t_slice_ms": 0.5, "t_total_ms": 0.5}


def load(path: str) -> dict:
    with open(path, encoding="utf-8") as handle:
        document = json.load(handle)
    if "models" not in document or "summary" not in document:
        sys.exit(f"{path}: not a benchmark metrics file (no models/summary)")
    return document


def models_by_id(document: dict) -> dict:
    return {model["id"]: model for model in document["models"]}


def fmt(value, key: str) -> str:
    if value is None:
        return "-"
    if isinstance(value, bool):
        return str(value)
    if isinstance(value, str):
        return value
    if isinstance(value, int):
        return f"{value:,}"
    if abs(value) >= 1000:
        return f"{value:,.1f}"
    return f"{value:.3f}".rstrip("0").rstrip(".") or "0"


def delta(before, after, key: str):
    """(delta, percent) for two values, or (None, None) when they are not both numbers."""
    if not isinstance(before, (int, float)) or not isinstance(after, (int, float)):
        return None, None
    change = after - before
    epsilon = EPSILON.get(key, 0.0)
    if abs(change) <= epsilon:
        return 0.0, 0.0
    return change, (change / before * 100.0 if before else None)


def row(label: str, key: str, before, after, rows: list) -> None:
    change, percent = delta(before, after, key)
    sign = "+" if change and change > 0 else ""
    change_text = "-" if change is None else f"{sign}{fmt(change, key)}"
    if percent is None:
        # Nothing to compare against, so the value appeared or disappeared.
        percent_text = "new" if change else "-"
    else:
        percent_text = f"{sign}{percent:.2f}%"
    rows.append((label, key, fmt(before, key), fmt(after, key), change_text, percent_text))


def table(rows: list, units: dict) -> None:
    if not rows:
        return
    header = ["model", "metric", "before", "after", "delta", "delta %"]
    cells = []
    for r in rows:
        unit = f" [{units[r[1]]}]" if units.get(r[1]) else ""
        cells.append([r[0], r[1] + unit, r[2], r[3], r[4], r[5]])
    widths = [max([len(header[i])] + [len(c[i]) for c in cells]) for i in range(len(header))]
    print("  ".join(header[i].ljust(widths[i]) if i < 2 else header[i].rjust(widths[i])
                    for i in range(len(header))))
    print("  ".join("-" * width for width in widths))
    for c in cells:
        print("  ".join(c[i].ljust(widths[i]) if i < 2 else c[i].rjust(widths[i])
                        for i in range(len(c))))


def hash_row(model_id: str, before, after) -> tuple:
    """The layer hash row: equal is unchanged, otherwise the two values and a CHANGED flag."""
    if before == after:
        return (model_id, HASH_KEY, fmt(before, HASH_KEY), fmt(after, HASH_KEY), "=", "")
    return (model_id, HASH_KEY, fmt(before, HASH_KEY), fmt(after, HASH_KEY), "CHANGED", "")


def compare(before_doc: dict, after_doc: dict, only_changed: bool) -> int:
    units = dict(METRICS)
    before_models = models_by_id(before_doc)
    after_models = models_by_id(after_doc)

    if before_doc.get("schema") != after_doc.get("schema"):
        print(f"note: schema {before_doc.get('schema')} -> {after_doc.get('schema')}, "
              f"the two files were written by different versions of the harness")

    rows = []
    changed_hashes = []
    for model_id in sorted(set(before_models) | set(after_models)):
        if model_id not in before_models:
            print(f"added: {model_id}")
            continue
        if model_id not in after_models:
            print(f"removed: {model_id}")
            continue
        before = before_models[model_id]
        after = after_models[model_id]
        if before.get("error") or after.get("error"):
            print(f"error: {model_id}: {before.get('error') or after.get('error')}")

        hrow = hash_row(model_id, before.get(HASH_KEY), after.get(HASH_KEY))
        if hrow[4] == "CHANGED":
            changed_hashes.append(model_id)
        rows.append(hrow)

        model_rows = []
        for key in KEYS:
            row(model_id, key, before.get(key), after.get(key), model_rows)
        rows.extend(r for r in model_rows if not only_changed or r[4] not in ("-", "0"))

    if changed_hashes:
        print(f"LAYER HASH CHANGED for {len(changed_hashes)} model(s): "
              f"{', '.join(changed_hashes)}")
        print("the same commit must give the same hash, so this is a change in the sliced geometry")
    else:
        print("layer hashes match")

    print()
    table(rows, units)

    print()
    summary_rows = []
    for key in ["model_count"] + KEYS:
        row("summary", key, before_doc["summary"].get(key), after_doc["summary"].get(key),
            summary_rows)
    table([r for r in summary_rows if not only_changed or r[4] not in ("-", "0")], units)

    return len(changed_hashes)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("before")
    parser.add_argument("after")
    parser.add_argument("--changed", action="store_true", help="only print the rows that changed")
    args = parser.parse_args(argv)

    changed = compare(load(args.before), load(args.after), args.changed)
    return 1 if changed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
