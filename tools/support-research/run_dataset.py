#!/usr/bin/env python3
"""Run the full contact-extraction pipeline on every pair in the manifest.

For each pair:
1. Load unsupported and supported STL with trimesh
2. Run register() to align them
3. Run separate_supports_scaled() to classify model vs support faces
4. Run describe_contacts() to extract contact records
5. Write contacts.json and summary.json to out/<pair_id>/

Pairs are processed smallest-first (by supported face count) to keep memory low.
Failures are caught and reported; the script continues with the next pair.
"""

from __future__ import annotations

import argparse
import gc
import json
import sys
import time
from pathlib import Path

import numpy as np
import trimesh
import yaml

from register import register, print_tilt, residual_stats
from separate import separate_supports_scaled
from contacts import describe_contacts, ContactReport, write_debug_mesh


def load_manifest(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as f:
        return yaml.safe_load(f)


def load_mesh(path: str) -> trimesh.Trimesh:
    mesh = trimesh.load(path, force="mesh", process=False)
    if isinstance(mesh, trimesh.Scene):
        mesh = mesh.dump(concatenate=True)
    if not isinstance(mesh, trimesh.Trimesh):
        raise ValueError(f"Expected Trimesh, got {type(mesh)} from {path}")
    return mesh


def process_pair(
    pair: dict,
    out_root: Path,
    rng: np.random.Generator,
    skip_curvature: bool = False,
    reg_final_threshold: float = 0.3,
    reg_min_inlier: float = 0.9,
) -> tuple[bool, dict | None, str | None]:
    """Process one pair. Returns (success, summary_dict, error_message)."""
    pair_id = pair["id"]
    out_dir = out_root / pair_id
    out_dir.mkdir(parents=True, exist_ok=True)

    try:
        t_load = time.perf_counter()
        model = load_mesh(pair["unsupported_stl"])
        scene = load_mesh(pair["supported_stl"])
        load_time = time.perf_counter() - t_load

        t_reg = time.perf_counter()
        reg_result = register(
            model,
            scene,
            rng,
            final_threshold=reg_final_threshold,
            min_inlier_fraction=reg_min_inlier,
        )
        reg_time = time.perf_counter() - t_reg

        tilt_deg = print_tilt(reg_result.transform)

        t_sep = time.perf_counter()
        sep_result = separate_supports_scaled(
            model,
            scene,
            reg_result.transform,
            rng=rng,
        )
        sep_time = time.perf_counter() - t_sep

        t_contacts = time.perf_counter()
        contact_gap = 0.35
        report = describe_contacts(
            model,
            scene,
            reg_result.transform,
            sep_result.support_mask,
            contact_gap=contact_gap,
            curvature_radius=None if skip_curvature else 1.0,
        )
        contacts_time = time.perf_counter() - t_contacts

        report.to_json(out_dir / "contacts.json")

        summary = {
            "pair_id": pair_id,
            "model": pair["model"],
            "part": pair["part"],
            "category": pair["category"],
            "triangles_supported": len(scene.faces),
            "triangles_unsupported": len(model.faces),
            "registration": {
                "rms_mm": reg_result.rms,
                "p95_mm": reg_result.p95,
                "inlier_fraction": reg_result.inlier_fraction,
                "iterations": reg_result.iterations,
                "converged": reg_result.converged,
                "candidate_errors_mm": reg_result.candidate_errors,
                "final_threshold_mm": reg_final_threshold,
                "min_inlier_fraction": reg_min_inlier,
            },
            "recovered_tilt_deg": tilt_deg,
            "support_face_count": int(sep_result.support_mask.sum()),
            "model_face_count": int((~sep_result.support_mask).sum()),
            "contact_count": len(report.contacts),
            "timing_seconds": {
                "load": load_time,
                "registration": reg_time,
                "separation": sep_time,
                "contacts": contacts_time,
                "total": load_time + reg_time + sep_time + contacts_time,
            },
        }

        with (out_dir / "summary.json").open("w", encoding="utf-8") as f:
            json.dump(summary, f, indent=2)

        write_debug_mesh(scene, sep_result.support_mask, report, out_dir / "debug.stl")

        return True, summary, None

    except Exception as e:
        error_msg = f"{pair_id}: {type(e).__name__}: {e}"
        return False, None, error_msg


def main() -> int:
    parser = argparse.ArgumentParser(description="Run support contact extraction on dataset")
    parser.add_argument(
        "--manifest",
        default="local-samples/supports/manifest.yaml",
        help="Path to manifest.yaml",
    )
    parser.add_argument(
        "--out",
        default="local-samples/supports/out",
        help="Output directory",
    )
    parser.add_argument(
        "--skip-curvature",
        action="store_true",
        help="Skip curvature computation (faster, less accurate on large meshes)",
    )
    parser.add_argument(
        "--reg-final-threshold",
        type=float,
        default=1.0,
        help="Registration final distance threshold in mm (default 1.0 for real data)",
    )
    parser.add_argument(
        "--reg-min-inlier",
        type=float,
        default=0.9,
        help="Registration minimum inlier fraction (default 0.9)",
    )
    parser.add_argument(
        "--seed",
        type=int,
        default=42,
        help="Random seed for reproducibility",
    )
    parser.add_argument(
        "--limit",
        type=int,
        default=None,
        help="Process only the first N pairs (after sorting by size)",
    )
    parser.add_argument(
        "--only",
        type=str,
        default=None,
        help="Process only the pair with this exact ID",
    )
    args = parser.parse_args()

    manifest_path = Path(args.manifest)
    out_root = Path(args.out)

    if not manifest_path.exists():
        print(f"Manifest not found: {manifest_path}", file=sys.stderr)
        return 1

    manifest = load_manifest(manifest_path)
    pairs = manifest["pairs"]

    # Sort by supported triangle count (smallest first)
    pairs.sort(key=lambda p: p.get("triangles", {}).get("supported", 0))

    if args.only:
        pairs = [p for p in pairs if p["id"] == args.only]
        if not pairs:
            print(f"Pair not found: {args.only}", file=sys.stderr)
            return 1
    elif args.limit:
        pairs = pairs[: args.limit]

    rng = np.random.default_rng(args.seed)

    print(f"Processing {len(pairs)} pairs (smallest first)...")
    print(f"Output directory: {out_root}")
    print(f"Skip curvature: {args.skip_curvature}")
    print(f"Registration: final_threshold={args.reg_final_threshold}mm, min_inlier={args.reg_min_inlier}")
    print()

    start_total = time.perf_counter()
    results = []
    failures = []

    for i, pair in enumerate(pairs, 1):
        print(f"[{i}/{len(pairs)}] {pair['id']} ({pair['category']}, {pair.get('triangles', {}).get('supported', 0):,} supported faces)...")
        sys.stdout.flush()

        success, summary, error = process_pair(
            pair, out_root, rng, args.skip_curvature,
            reg_final_threshold=args.reg_final_threshold,
            reg_min_inlier=args.reg_min_inlier,
        )

        if success:
            results.append(summary)
            reg = summary["registration"]
            print(
                f"  OK: contacts={summary['contact_count']}, "
                f"rms={reg['rms_mm']:.3f}mm, p95={reg['p95_mm']:.3f}mm, "
                f"inlier={reg['inlier_fraction']:.3f}, "
                f"tilt={summary['recovered_tilt_deg']:.2f}°, "
                f"time={summary['timing_seconds']['total']:.1f}s"
            )
        else:
            failures.append(error)
            print(f"  FAILED: {error}")

        # Explicitly free meshes and run GC to keep memory low
        gc.collect()

    total_time = time.perf_counter() - start_total

    print()
    print("=" * 60)
    print(f"Pairs processed: {len(results)}/{len(pairs)}")
    print(f"Failures: {len(failures)}")
    print(f"Total runtime: {total_time:.1f}s")

    if failures:
        print("\nFailures:")
        for f in failures:
            print(f"  {f}")

    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())