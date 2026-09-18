#!/usr/bin/env python3
"""Mine support rules from the extracted contact data (M7.4).

Reads all contacts.json files from local-samples/supports/out/ and computes
aggregate statistics for rule derivation. Outputs only aggregates, no per-model data.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import numpy as np

from contacts import ContactReport


def load_all_reports(out_root: Path) -> list[tuple[str, dict, ContactReport]]:
    """Load all contact reports and their summaries."""
    results = []
    for summary_path in sorted(out_root.glob("*/summary.json")):
        pair_dir = summary_path.parent
        contacts_path = pair_dir / "contacts.json"
        if not contacts_path.exists():
            continue
        with summary_path.open("r", encoding="utf-8") as f:
            summary = json.load(f)
        report = ContactReport()
        report.contacts = []
        data = json.loads(contacts_path.read_text(encoding="utf-8"))
        report.plate_z = data["plate_z"]
        report.components = data["components"]
        from contacts import Contact
        for c in data["contacts"]:
            report.contacts.append(Contact(**c))
        results.append((summary["pair_id"], summary, report))
    return results


def mine_rules(out_root: Path) -> dict[str, Any]:
    """Compute aggregate statistics for rule derivation."""
    reports = load_all_reports(out_root)
    if not reports:
        return {"error": "No reports found"}

    # Collect all contacts across pairs
    all_contacts = []
    all_summaries = []
    for pair_id, summary, report in reports:
        all_summaries.append(summary)
        for c in report.contacts:
            c_dict = {
                "pair_id": pair_id,
                "category": summary["category"],
                "diameter": c.diameter,
                "penetration": c.penetration,
                "height_above_plate": c.height_above_plate,
                "overhang_deg": c.overhang_deg,
                "mean_curvature": c.mean_curvature,
                "serves_local_minimum": c.serves_local_minimum,
                "vertex_count": c.vertex_count,
                "support_component": c.support_component,
            }
            all_contacts.append(c_dict)

    if not all_contacts:
        return {"error": "No contacts found"}

    # Convert to arrays for vectorized stats
    diameters = np.array([c["diameter"] for c in all_contacts])
    penetrations = np.array([c["penetration"] for c in all_contacts])
    heights = np.array([c["height_above_plate"] for c in all_contacts])
    overhangs = np.array([c["overhang_deg"] for c in all_contacts])
    curvatures = np.array([c["mean_curvature"] for c in all_contacts])
    is_minimum = np.array([c["serves_local_minimum"] for c in all_contacts])
    vertex_counts = np.array([c["vertex_count"] for c in all_contacts])

    # Per-pair contact counts
    contact_counts = [len(r.contacts) for _, _, r in reports]
    total_pairs = len(reports)

    # Pair-level registration quality
    inlier_fractions = [s["registration"]["inlier_fraction"] for s in all_summaries]
    rms_values = [s["registration"]["rms_mm"] for s in all_summaries]
    tilts = [s["recovered_tilt_deg"] for s in all_summaries]

    rules = {
        "dataset_summary": {
            "pairs_processed": total_pairs,
            "total_contacts": len(all_contacts),
            "contacts_per_pair": {
                "mean": float(np.mean(contact_counts)),
                "median": float(np.median(contact_counts)),
                "min": int(np.min(contact_counts)),
                "max": int(np.max(contact_counts)),
                "std": float(np.std(contact_counts)),
            },
            "registration_quality": {
                "inlier_fraction": {
                    "mean": float(np.mean(inlier_fractions)),
                    "median": float(np.median(inlier_fractions)),
                    "min": float(np.min(inlier_fractions)),
                    "max": float(np.max(inlier_fractions)),
                    "below_09_count": int(np.sum(np.array(inlier_fractions) < 0.9)),
                    "below_08_count": int(np.sum(np.array(inlier_fractions) < 0.8)),
                    "below_07_count": int(np.sum(np.array(inlier_fractions) < 0.7)),
                },
                "rms_mm": {
                    "mean": float(np.mean(rms_values)),
                    "median": float(np.median(rms_values)),
                    "max": float(np.max(rms_values)),
                },
                "tilt_deg": {
                    "mean": float(np.mean(tilts)),
                    "median": float(np.median(tilts)),
                    "range": [float(np.min(tilts)), float(np.max(tilts))],
                },
            },
        },
        "contact_diameter_mm": {
            "mean": float(np.mean(diameters)),
            "median": float(np.median(diameters)),
            "std": float(np.std(diameters)),
            "min": float(np.min(diameters)),
            "max": float(np.max(diameters)),
            "p25": float(np.percentile(diameters, 25)),
            "p75": float(np.percentile(diameters, 75)),
            "p95": float(np.percentile(diameters, 95)),
        },
        "contact_penetration_mm": {
            "mean": float(np.mean(penetrations)),
            "median": float(np.median(penetrations)),
            "std": float(np.std(penetrations)),
            "max": float(np.max(penetrations)),
            "p95": float(np.percentile(penetrations, 95)),
        },
        "height_above_plate_mm": {
            "mean": float(np.mean(heights)),
            "median": float(np.median(heights)),
            "min": float(np.min(heights)),
            "max": float(np.max(heights)),
        },
        "overhang_angle_deg": {
            "mean": float(np.mean(overhangs)),
            "median": float(np.median(overhangs)),
            "std": float(np.std(overhangs)),
            "min": float(np.min(overhangs)),
            "max": float(np.max(overhangs)),
            "p95": float(np.percentile(overhangs, 95)),
        },
        "mean_curvature": {
            "mean": float(np.mean(curvatures)),
            "median": float(np.median(curvatures)),
            "std": float(np.std(curvatures)),
            "min": float(np.min(curvatures)),
            "max": float(np.max(curvatures)),
        },
        "local_minimum_coverage": {
            "fraction_serving_minimum": float(np.mean(is_minimum)),
            "count_serving_minimum": int(np.sum(is_minimum)),
            "total_contacts": len(all_contacts),
        },
        "tip_vertex_count": {
            "mean": float(np.mean(vertex_counts)),
            "median": float(np.median(vertex_counts)),
            "min": int(np.min(vertex_counts)),
            "max": int(np.max(vertex_counts)),
        },
        "support_structure": {
            "total_components": sum(len(r.components) for _, _, r in reports),
            "components_per_pair": {
                "mean": float(np.mean([len(r.components) for _, _, r in reports])),
            },
            "contacts_per_component": {
                "mean": float(np.mean([c["contacts"] for _, _, r in reports for c in r.components])) if any(len(r.components) for _, _, r in reports) else 0,
            },
            "components_touching_plate_fraction": float(
                np.mean([sum(1 for c in r.components if c["touches_plate"]) / len(r.components) for _, _, r in reports if r.components])
            ) if any(len(r.components) for _, _, r in reports) else 0,
        },
    }

    # Diameter vs overhang correlation (binned)
    overhang_bins = np.arange(0, 91, 15)
    diameter_by_overhang = {}
    for i in range(len(overhang_bins) - 1):
        lo, hi = overhang_bins[i], overhang_bins[i + 1]
        mask = (overhangs >= lo) & (overhangs < hi)
        if mask.any():
            d = diameters[mask]
            diameter_by_overhang[f"{lo}-{hi}deg"] = {
                "count": int(mask.sum()),
                "mean_diameter": float(np.mean(d)),
                "median_diameter": float(np.median(d)),
            }
    rules["diameter_by_overhang_bin"] = diameter_by_overhang

    # Diameter vs height above plate (binned)
    height_bins = np.arange(0, max(heights) + 5, 5)
    diameter_by_height = {}
    for i in range(len(height_bins) - 1):
        lo, hi = height_bins[i], height_bins[i + 1]
        mask = (heights >= lo) & (heights < hi)
        if mask.any():
            d = diameters[mask]
            diameter_by_height[f"{lo:.0f}-{hi:.0f}mm"] = {
                "count": int(mask.sum()),
                "mean_diameter": float(np.mean(d)),
                "median_diameter": float(np.median(d)),
            }
    rules["diameter_by_height_bin"] = diameter_by_height

    # Category breakdown
    categories = {}
    for c in all_contacts:
        cat = c["category"]
        if cat not in categories:
            categories[cat] = []
        categories[cat].append(c)
    category_stats = {}
    for cat, contacts in categories.items():
        d = np.array([c["diameter"] for c in contacts])
        o = np.array([c["overhang_deg"] for c in contacts])
        h = np.array([c["height_above_plate"] for c in contacts])
        category_stats[cat] = {
            "contact_count": len(contacts),
            "diameter_mean": float(np.mean(d)),
            "diameter_median": float(np.median(d)),
            "overhang_mean": float(np.mean(o)),
            "height_mean": float(np.mean(h)),
        }
    rules["by_category"] = category_stats

    return rules


def main() -> int:
    out_root = Path("local-samples/supports/out")
    if not out_root.exists():
        print(f"Output directory not found: {out_root}")
        return 1

    rules = mine_rules(out_root)
    if "error" in rules:
        print(rules["error"])
        return 1

    # Print summary
    ds = rules["dataset_summary"]
    print(f"Pairs processed: {ds['pairs_processed']}")
    print(f"Total contacts: {ds['total_contacts']}")
    print(f"Contacts/pair: {ds['contacts_per_pair']['mean']:.1f} (median {ds['contacts_per_pair']['median']:.1f})")
    print()
    print(f"Registration inlier: mean={ds['registration_quality']['inlier_fraction']['mean']:.3f}, "
          f"below 0.9: {ds['registration_quality']['inlier_fraction']['below_09_count']}/{ds['pairs_processed']}")
    print()
    print(f"Contact diameter: {rules['contact_diameter_mm']['mean']:.2f}±{rules['contact_diameter_mm']['std']:.2f} mm "
          f"(median {rules['contact_diameter_mm']['median']:.2f}, p95 {rules['contact_diameter_mm']['p95']:.2f})")
    print(f"Contact penetration: {rules['contact_penetration_mm']['mean']:.3f} mm "
          f"(median {rules['contact_penetration_mm']['median']:.3f}, p95 {rules['contact_penetration_mm']['p95']:.3f})")
    print(f"Overhang angle: {rules['overhang_angle_deg']['mean']:.1f}±{rules['overhang_angle_deg']['std']:.1f}° "
          f"(median {rules['overhang_angle_deg']['median']:.1f}°)")
    print(f"Local minimum coverage: {rules['local_minimum_coverage']['fraction_serving_minimum']:.1%} "
          f"({rules['local_minimum_coverage']['count_serving_minimum']}/{rules['local_minimum_coverage']['total_contacts']})")
    print()
    print("Diameter by overhang bin:")
    for bin_name, stats in rules["diameter_by_overhang_bin"].items():
        print(f"  {bin_name}: n={stats['count']}, mean={stats['mean_diameter']:.2f}mm, median={stats['median_diameter']:.2f}mm")
    print()
    print("Diameter by height bin:")
    for bin_name, stats in rules["diameter_by_height_bin"].items():
        print(f"  {bin_name}: n={stats['count']}, mean={stats['mean_diameter']:.2f}mm, median={stats['median_diameter']:.2f}mm")
    print()
    print("By category:")
    for cat, stats in rules["by_category"].items():
        print(f"  {cat}: n={stats['contact_count']}, diam={stats['diameter_mean']:.2f}mm, overhang={stats['overhang_mean']:.1f}°, height={stats['height_mean']:.1f}mm")

    # Write full rules JSON for derived-rules.md generation
    import yaml
    out_path = out_root / "mined_rules.yaml"
    out_path.write_text(yaml.dump(rules, sort_keys=False), encoding="utf-8")
    print(f"\nFull rules written to {out_path}")
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main())