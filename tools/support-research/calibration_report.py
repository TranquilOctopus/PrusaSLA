"""Aggregate tables of the expert support calibration (M7.4c).

Merges what calibrate.py measured (local-samples/supports/out/calibration.json, one
record per pair) with what the generator measured (the log of the hidden
tests/sla_print/sla_calibration_support_tests.cpp run, one `[CalibSupport]` line per
pair) and writes Markdown tables by category to stdout.

Only aggregates go out: a category, a count and a distribution. No id, no file name,
no path, no per-pair number - the research rules of ROADMAP M7 allow nothing else to
leave the machine, and the tables are what gets committed to
doc/sla-fork/supports/calibration.md.

Usage:
    python tools/support-research/calibration_report.py \
        --calibration local-samples/supports/out/calibration.json \
        --log .agent-scratch/calibration.log > calibration.md
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CALIBRATION = REPO_ROOT / "local-samples" / "supports" / "out" / "calibration.json"

# The prefix the C++ test prints its line with.
LOG_TAG = "[CalibSupport]"

# An up-axis tilt below this is printed as the modeller's own "up", the band above it
# as tipped on purpose. Kept in step with calibrate.py, which writes both as flags.
FACE_UP_TILT_DEG = 45.0
TIPPED_BAND_DEG = (30.0, 60.0)

# The row every category is also counted in.
ALL_CATEGORY = "all"

# The numbers the test reports, and how many of them we keep. A negative value is the
# test's "not measured", the oriented STL being absent.
TEST_FIELDS = ("points_as_loaded", "points_expert_orientation", "islands", "height_mm", "height_expert_mm")

EXIT_OK = 0
EXIT_CANNOT_RUN = 2


@dataclass
class Category:
    """Every aggregate of one category. Nothing here can be traced back to a pair."""

    name: str
    pairs: int = 0
    measured: int = 0
    expert: list[float] = field(default_factory=list)
    as_loaded: list[float] = field(default_factory=list)
    expert_orientation: list[float] = field(default_factory=list)
    ratio_as_loaded: list[float] = field(default_factory=list)
    ratio_expert_orientation: list[float] = field(default_factory=list)
    height: list[float] = field(default_factory=list)
    tilt: list[float] = field(default_factory=list)
    face_up: int = 0
    tipped: int = 0
    flat_on_plate: int = 0
    tilt_known: int = 0
    rms: list[float] = field(default_factory=list)
    p95: list[float] = field(default_factory=list)
    structures: list[float] = field(default_factory=list)
    structures_on_plate: list[float] = field(default_factory=list)
    bases: list[float] = field(default_factory=list)


def parse_log(text: str) -> dict[str, dict[str, float]]:
    """The `[CalibSupport]` lines of a test log, keyed by id.

    A line is `key=value` pairs after the tag, so a new column cannot break an older
    log and an unknown key is kept out of the way instead of being guessed at.
    """
    measured: dict[str, dict[str, float]] = {}
    for raw in text.splitlines():
        position = raw.find(LOG_TAG)
        if position < 0:
            continue

        fields: dict[str, float] = {}
        pair_id = ""
        for item in raw[position + len(LOG_TAG) :].split(","):
            key, separator, value = item.partition("=")
            key = key.strip()
            if not separator:
                continue
            value = value.strip()
            if key == "id":
                pair_id = value
            elif key == "category":
                continue
            elif key in TEST_FIELDS:
                try:
                    fields[key] = float(value)
                except ValueError:
                    continue
        if pair_id:
            measured[pair_id] = fields
    return measured


def load_pairs(path: Path) -> list[dict]:
    """The per-pair records calibrate.py wrote."""
    payload = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(payload, dict) or not isinstance(payload.get("pairs"), list):
        raise ValueError("calibration.json holds no list of pairs")
    return [record for record in payload["pairs"] if isinstance(record, dict) and record.get("id")]


def collect(records: list[dict], measured: dict[str, dict[str, float]]) -> list[Category]:
    """One Category per manifest category, plus one for the whole dataset."""
    categories: dict[str, Category] = {}

    def bucket(name: str) -> Category:
        return categories.setdefault(name, Category(name))

    for record in records:
        name = str(record.get("category", "uncategorized") or "uncategorized").strip().lower()
        line = measured.get(str(record["id"]), {})
        for target in (bucket(name), bucket(ALL_CATEGORY)):
            _add(target, record, line)

    # "all" last: it is the dataset, not a category of it.
    return sorted(categories.values(), key=lambda item: (item.name == ALL_CATEGORY, item.name))


def _add(category: Category, record: dict, line: dict[str, float]) -> None:
    category.pairs += 1

    expert = _number(record.get("expert_support_count"))
    structures = _number(record.get("support_structures"))
    structures_on_plate = _number(record.get("support_structures_on_plate"))
    bases = _number(record.get("support_base_structures"))
    rms = _number(record.get("registration_rms_mm"))
    p95 = _number(record.get("registration_p95_mm"))
    tilt = _number(record.get("up_axis_tilt_deg"))
    height = _number(record.get("model_height_mm"))
    as_loaded = _positive(line.get("points_as_loaded"))
    oriented = _positive(line.get("points_expert_orientation"))

    for values, value in (
        (category.expert, expert),
        (category.structures, structures),
        (category.structures_on_plate, structures_on_plate),
        (category.bases, bases),
        (category.rms, rms),
        (category.p95, p95),
    ):
        if value is not None:
            values.append(value)

    if height is not None:
        category.height.append(height)

    if tilt is not None:
        category.tilt.append(tilt)
        category.tilt_known += 1
        if tilt < FACE_UP_TILT_DEG:
            category.face_up += 1
        if TIPPED_BAND_DEG[0] <= tilt <= TIPPED_BAND_DEG[1]:
            category.tipped += 1

    if record.get("flat_area_faces_plate") is True:
        category.flat_on_plate += 1

    if as_loaded is None and oriented is None:
        return
    category.measured += 1

    if as_loaded is not None:
        category.as_loaded.append(as_loaded)
    if oriented is not None:
        category.expert_orientation.append(oriented)

    # The ratio only means something where both sides of it were measured.
    if expert is not None and expert > 0:
        if as_loaded is not None:
            category.ratio_as_loaded.append(as_loaded / expert)
        if oriented is not None:
            category.ratio_expert_orientation.append(oriented / expert)


def _number(value) -> float | None:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    return float(value) if math.isfinite(float(value)) else None


def _positive(value) -> float | None:
    """A number the generator reported; the test's "not measured" is negative."""
    number = _number(value)
    return number if number is not None and number >= 0 else None


def median_iqr(values: list[float]) -> str:
    """The median and the interquartile range of a list, as one cell.

    A count is printed as a count: a whole number in the input is a whole number out.
    """
    if not values:
        return "-"
    array = np.asarray(values, dtype=np.float64)
    places = 0 if bool(np.all(array == np.round(array))) else 1
    median = float(np.median(array))
    low = float(np.percentile(array, 25))
    high = float(np.percentile(array, 75))
    return f"{median:.{places}f} ({low:.{places}f}-{high:.{places}f})"


def share(count: int, total: int) -> str:
    """How many of `total`, in percent, or a dash when nothing was measured."""
    if total <= 0:
        return "-"
    return f"{100.0 * count / total:.0f}%"


def _table(title: str, header: list[str], rows: list[list[str]]) -> list[str]:
    lines = [f"### {title}", "", "| " + " | ".join(header) + " |",
             "| " + " | ".join("---" for _ in header) + " |"]
    lines.extend("| " + " | ".join(row) + " |" for row in rows)
    return lines + [""]


def render(categories: list[Category]) -> str:
    """The whole report, Markdown, aggregates only."""
    density = []
    orientation = []
    registration = []
    for category in categories:
        density.append(
            [
                category.name,
                str(category.pairs),
                str(category.measured),
                median_iqr(category.expert),
                median_iqr(category.as_loaded),
                median_iqr(category.expert_orientation),
                median_iqr(category.ratio_as_loaded),
                median_iqr(category.ratio_expert_orientation),
            ]
        )
        orientation.append(
            [
                category.name,
                str(category.pairs),
                median_iqr(category.height),
                median_iqr(category.tilt),
                share(category.face_up, category.tilt_known),
                share(category.tipped, category.tilt_known),
                share(category.flat_on_plate, category.pairs),
            ]
        )
        registration.append(
            [
                category.name,
                str(category.pairs),
                median_iqr(category.rms),
                median_iqr(category.p95),
                median_iqr(category.structures),
                median_iqr(category.structures_on_plate),
                median_iqr(category.bases),
            ]
        )

    lines = ["# Expert support calibration (M7.4c)", ""]
    lines += [
        "What the expert did with the same models, and what our generator does with",
        "them in the same print orientation. Aggregates by category only: no pair, no",
        "model, no file is named anywhere in this file.",
        "",
        "Cells are `median (25th-75th percentile)`. A ratio of 1.0 means we place as",
        "many support points as the expert put tips under the model; below 1.0 we are",
        "sparser, above 1.0 denser. `pairs` counts the pairs of the category, `measured`",
        "the ones our generator was run on.",
        "",
    ]
    lines += _table(
        "Support density",
        ["category", "pairs", "measured", "expert tips", "our points, as loaded",
         "our points, expert orientation", "ratio, as loaded", "ratio, expert orientation"],
        density,
    )
    lines += _table(
        "Orientation",
        ["category", "pairs", "model height [mm]", "up-axis tilt [deg]",
         f"up-axis up (tilt < {FACE_UP_TILT_DEG:.0f} deg)", "up-axis 30-60 deg",
         "largest flat area on the plate"],
        orientation,
    )
    lines += _table(
        "Structure and registration residual",
        ["category", "pairs", "registration RMS [mm]", "registration p95 [mm]",
         "support structures", "structures on the plate", "bases or rafts"],
        registration,
    )
    lines += [
        "The tilt is the angle between the plain model's own up axis (+Z of the",
        "unsupported STL) and print-up in the orientation the expert printed it, so it",
        "is the orientation goal measured on the expert's own choice rather than ours.",
        "'Up-axis up' is what a miniature printed face up and neck down looks like; the",
        "30-60 deg band is a model tipped over on purpose.",
        "",
        "The residual is the known M7.3 caveat: registration is off by 0.5-0.9 mm RMS,",
        "which is wider than the contact gap, so a tip count is a count of clusters",
        "within 1.5 mm of the model and never of exact contact positions.",
        "",
    ]
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Aggregate tables of the M7.4c calibration.")
    parser.add_argument("--calibration", type=Path, default=DEFAULT_CALIBRATION,
                        help="calibration.json written by calibrate.py")
    parser.add_argument("--log", type=Path, required=True,
                        help="the log of the sla_calibration_support_tests run")
    args = parser.parse_args(argv)

    if not args.calibration.is_file():
        print("calibration_report: calibration.json is missing (run calibrate.py first)",
              file=sys.stderr)
        return EXIT_CANNOT_RUN
    if not args.log.is_file():
        print("calibration_report: the test log is missing (run the hidden test first)",
              file=sys.stderr)
        return EXIT_CANNOT_RUN

    try:
        records = load_pairs(args.calibration)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"calibration_report: {type(error).__name__}", file=sys.stderr)
        return EXIT_CANNOT_RUN

    measured = parse_log(args.log.read_text(encoding="utf-8", errors="replace"))
    categories = collect(records, measured)
    if not categories:
        print("calibration_report: the calibration file holds no pair", file=sys.stderr)
        return EXIT_CANNOT_RUN

    print(render(categories))
    joined = sum(1 for record in records if str(record["id"]) in measured)
    print(
        f"calibration_report: pairs={len(records)}, measured={joined}, "
        f"categories={len(categories)}"
    )
    return EXIT_OK


if __name__ == "__main__":
    raise SystemExit(main())
