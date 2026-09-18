#!/usr/bin/env python3
"""Synthetic test for mine_rules.py (M7.4).

Creates a small synthetic dataset with known contacts and verifies
that mine_rules.py computes the correct aggregate statistics.
"""

from __future__ import annotations

import json
import tempfile
from pathlib import Path

import numpy as np

import mine_rules


def create_synthetic_dataset(out_root: Path, n_pairs: int = 3, contacts_per_pair: int = 5) -> list[dict]:
    """Create synthetic contacts.json and summary.json files with known values."""
    np.random.seed(42)
    summaries = []

    for i in range(n_pairs):
        pair_id = f"synthetic-pair-{i}"
        pair_dir = out_root / pair_id
        pair_dir.mkdir(parents=True, exist_ok=True)

        # Known contact parameters
        diameters = np.full(contacts_per_pair, 1.2)  # 1.2 mm diameter
        penetrations = np.zeros(contacts_per_pair)
        heights = np.arange(contacts_per_pair, dtype=float) * 2.0  # 0, 2, 4, 6, 8 mm
        overhangs = np.array([10.0, 30.0, 50.0, 70.0, 90.0])  # varied overhangs
        curvatures = np.full(contacts_per_pair, 0.1)
        is_minimum = np.array([True, False, False, True, False])
        vertex_counts = np.full(contacts_per_pair, 20)

        contacts = []
        for j in range(contacts_per_pair):
            contacts.append({
                "position_scene": [float(j * 10), 0.0, heights[j]],
                "position_model": [float(j * 10), 0.0, heights[j]],
                "normal_scene": [0.0, 0.0, -1.0],
                "normal_model": [0.0, 0.0, -1.0],
                "diameter": float(diameters[j]),
                "penetration": float(penetrations[j]),
                "height_above_plate": float(heights[j]),
                "overhang_deg": float(overhangs[j]),
                "mean_curvature": float(curvatures[j]),
                "serves_local_minimum": bool(is_minimum[j]),
                "support_component": 0,
                "vertex_count": int(vertex_counts[j]),
            })

        report = {
            "plate_z": 0.0,
            "components": [
                {
                    "component": 0,
                    "faces": 100,
                    "contacts": contacts_per_pair,
                    "touches_plate": True,
                    "min_z": 0.0,
                    "max_z": float(heights.max()),
                }
            ],
            "contacts": contacts,
        }

        (pair_dir / "contacts.json").write_text(json.dumps(report, indent=2), encoding="utf-8")

        summary = {
            "pair_id": pair_id,
            "model": "Synthetic Model",
            "part": f"Part {i}",
            "category": "test",
            "triangles_supported": 1000,
            "triangles_unsupported": 500,
            "registration": {
                "rms_mm": 0.1,
                "p95_mm": 0.2,
                "inlier_fraction": 0.95,
                "iterations": 10,
                "converged": True,
                "candidate_errors_mm": [0.1],
                "final_threshold_mm": 0.3,
                "min_inlier_fraction": 0.9,
            },
            "recovered_tilt_deg": 10.0,
            "support_face_count": 100,
            "model_face_count": 500,
            "contact_count": contacts_per_pair,
            "timing_seconds": {
                "load": 0.1,
                "registration": 1.0,
                "separation": 1.0,
                "contacts": 1.0,
                "total": 3.1,
            },
        }
        (pair_dir / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
        summaries.append(summary)

    return summaries


def test_mine_rules_synthetic():
    """Test mine_rules.py on synthetic data with known ground truth."""
    with tempfile.TemporaryDirectory() as tmpdir:
        out_root = Path(tmpdir) / "out"
        out_root.mkdir(parents=True)

        create_synthetic_dataset(out_root, n_pairs=3, contacts_per_pair=5)

        # Run mining
        rules = mine_rules.mine_rules(out_root)

        # Verify dataset summary
        ds = rules["dataset_summary"]
        assert ds["pairs_processed"] == 3
        assert ds["total_contacts"] == 15
        assert ds["contacts_per_pair"]["mean"] == 5.0
        assert ds["contacts_per_pair"]["median"] == 5.0

        # Verify registration quality (all 0.95 inlier)
        rq = ds["registration_quality"]
        assert abs(rq["inlier_fraction"]["mean"] - 0.95) < 1e-10
        assert rq["inlier_fraction"]["below_09_count"] == 0

        # Verify contact diameter (all 1.2 mm)
        cd = rules["contact_diameter_mm"]
        assert abs(cd["mean"] - 1.2) < 1e-10
        assert abs(cd["median"] - 1.2) < 1e-10
        assert cd["std"] < 1e-10
        assert cd["min"] == 1.2
        assert cd["max"] == 1.2

        # Verify penetration (all zero)
        cp = rules["contact_penetration_mm"]
        assert cp["mean"] == 0.0
        assert cp["median"] == 0.0
        assert cp["max"] == 0.0

        # Verify height above plate
        ha = rules["height_above_plate_mm"]
        expected_heights = np.array([0.0, 2.0, 4.0, 6.0, 8.0] * 3)
        assert abs(ha["mean"] - np.mean(expected_heights)) < 1e-6
        assert abs(ha["median"] - np.median(expected_heights)) < 1e-6
        assert ha["min"] == 0.0
        assert ha["max"] == 8.0

        # Verify overhang angles
        oa = rules["overhang_angle_deg"]
        expected_overhangs = np.array([10.0, 30.0, 50.0, 70.0, 90.0] * 3)
        assert abs(oa["mean"] - np.mean(expected_overhangs)) < 1e-6
        assert abs(oa["median"] - np.median(expected_overhangs)) < 1e-6

        # Verify local minimum coverage (2/5 per pair = 6/15 total = 40%)
        lm = rules["local_minimum_coverage"]
        assert lm["total_contacts"] == 15
        assert lm["count_serving_minimum"] == 6
        assert abs(lm["fraction_serving_minimum"] - 0.4) < 1e-6

        # Verify vertex count
        vc = rules["tip_vertex_count"]
        assert vc["mean"] == 20.0
        assert vc["median"] == 20.0

        # Verify diameter by overhang bin
        dbo = rules["diameter_by_overhang_bin"]
        # Each bin has exactly 3 contacts (one per pair), all diameter 1.2
        for bin_name, stats in dbo.items():
            assert stats["count"] == 3
            assert abs(stats["mean_diameter"] - 1.2) < 1e-10
            assert abs(stats["median_diameter"] - 1.2) < 1e-10

        # Verify diameter by height bin
        dbh = rules["diameter_by_height_bin"]
        for bin_name, stats in dbh.items():
            assert abs(stats["mean_diameter"] - 1.2) < 1e-10

        # Verify by category
        bc = rules["by_category"]
        assert "test" in bc
        assert bc["test"]["contact_count"] == 15
        assert abs(bc["test"]["diameter_mean"] - 1.2) < 1e-10
        assert abs(bc["test"]["overhang_mean"] - np.mean(expected_overhangs)) < 1e-6

        # Verify support structure
        ss = rules["support_structure"]
        assert ss["total_components"] == 3
        assert ss["components_per_pair"]["mean"] == 1.0
        assert ss["components_touching_plate_fraction"] == 1.0

        print("All synthetic tests passed!")


if __name__ == "__main__":
    test_mine_rules_synthetic()