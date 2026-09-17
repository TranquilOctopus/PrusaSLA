#!/usr/bin/env python
"""Benchmark scaled support separation on a large synthetic scene.

Run with the project venv, e.g.::

    .venv\\Scripts\\python benchmark_scaled_separation.py --faces 300000

Reports wall time and peak memory for the scaled method, and checks it against
the exact method on a random subset of faces (the exact method over a whole
scene of this size takes hours, which is the reason the scaled one exists).
Everything is generated in memory; no dataset file is ever read.
"""

from __future__ import annotations

import argparse
import gc
import time
import tracemalloc

import numpy as np
import trimesh

from separate import separate_supports, separate_supports_scaled
from synth import apply_print_transform, build_model, build_supports

try:  # optional: real process memory, tracemalloc only sees Python allocations
    import psutil

    def _rss_mb() -> float:
        return psutil.Process().memory_info().rss / 1e6
except ImportError:  # pragma: no cover - depends on the environment
    def _rss_mb() -> float:
        return float("nan")


def grow(mesh: trimesh.Trimesh, target_faces: int) -> trimesh.Trimesh:
    """Subdivide until the mesh has at least target_faces, freeing each step."""
    while len(mesh.faces) < target_faces:
        finer = mesh.subdivide()
        del mesh
        gc.collect()
        mesh = finer
    return mesh


def build_scene(target_faces: int, rng: np.random.Generator):
    model = grow(build_model(rng), target_faces)
    supports, _ = build_supports(model, rng)
    moved_model, transform = apply_print_transform(model, rng)
    moved_supports, _ = apply_print_transform(supports, rng)
    del supports
    gc.collect()
    scene = trimesh.util.concatenate([moved_model, moved_supports])
    model_faces = len(moved_model.faces)
    del moved_model, moved_supports
    gc.collect()
    return model, scene, transform, model_faces


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--faces", type=int, default=100_000, help="target model faces")
    parser.add_argument("--epsilon", type=float, default=0.3, help="separation threshold in mm")
    parser.add_argument("--check-faces", type=int, default=400, help="faces verified exactly")
    parser.add_argument("--seed", type=int, default=2026)
    args = parser.parse_args()

    rng = np.random.default_rng(args.seed)
    model, scene, transform, model_faces = build_scene(args.faces, rng)
    print(
        f"model {len(model.faces):,} faces; scene {len(scene.faces):,} faces "
        f"({model_faces:,} model + {len(scene.faces) - model_faces:,} support)"
    )

    gc.collect()
    tracemalloc.start()
    rss_before = _rss_mb()
    started = time.perf_counter()
    result = separate_supports_scaled(
        model, scene, transform, epsilon=args.epsilon, rng=np.random.default_rng(args.seed)
    )
    elapsed = time.perf_counter() - started
    _, peak_python = tracemalloc.get_traced_memory()
    tracemalloc.stop()
    print(
        f"scaled: {elapsed:.1f} s, peak Python allocation {peak_python / 1e6:.0f} MB, "
        f"process RSS {rss_before:.0f} -> {_rss_mb():.0f} MB"
    )

    known_support = np.arange(len(scene.faces)) >= model_faces
    agreement = float((result.support_mask == known_support).mean())
    print(f"agreement with known split: {agreement * 100:.3f}%")

    subset = np.sort(
        np.random.default_rng(args.seed).choice(
            len(scene.faces), size=min(args.check_faces, len(scene.faces)), replace=False
        )
    )
    subset_scene = scene.submesh([subset], append=True, repair=False)
    exact = separate_supports(model, subset_scene, transform, epsilon=args.epsilon)
    matches = int((exact.support_mask == result.support_mask[subset]).sum())
    print(f"exact check on {len(subset)} random faces: {matches}/{len(subset)} identical")
    return 0 if agreement == 1.0 and matches == len(subset) else 1


if __name__ == "__main__":
    raise SystemExit(main())
