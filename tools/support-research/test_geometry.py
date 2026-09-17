"""Unit tests for the M7.3a geometry kernel (MeshDistanceQuery).

Analytic ground-truth cases: face interior, edge, vertex, multiple faces,
area-weighted sampling, and face-id validity. Run before trusting any
end-to-end registration.
"""
from __future__ import annotations

import numpy as np
import trimesh

from register import MeshDistanceQuery, _sample_surface


def _single_triangle() -> trimesh.Trimesh:
    vertices = np.array(
        [[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]], dtype=np.float64
    )
    faces = np.array([[0, 1, 2]])
    return trimesh.Trimesh(vertices=vertices, faces=faces, process=False)


def test_interior_projection() -> None:
    mesh = _single_triangle()
    dq = MeshDistanceQuery(mesh)
    q = np.array([[0.2, 0.2, 0.5]])
    closest, dist, face_id = dq.query(q, k=1)
    assert np.allclose(closest, [[0.2, 0.2, 0.0]], atol=1e-9)
    assert np.allclose(dist, [0.5], atol=1e-9)
    assert (face_id == 0).all()


def test_edge_and_vertex_regions() -> None:
    mesh = _single_triangle()
    dq = MeshDistanceQuery(mesh)
    q = np.array(
        [
            [0.5, 0.5, -1.0],
            [-1.0, -1.0, 0.0],
            [2.0, 0.5, 0.1],
        ]
    )
    expected_closest = np.array(
        [
            [0.5, 0.5, 0.7 - 0.7],
            [0.0, 0.0, 0.0],
            [1.0, 0.0, 0.0],
        ]
    )
    expected_dist = np.array([1.0, np.sqrt(2.0), np.sqrt(1.26)])
    closest, dist, _ = dq.query(q, 1)
    assert np.allclose(closest, expected_closest, atol=1e-9)
    assert np.allclose(dist, expected_dist, atol=1e-9)


def test_multiple_faces_and_k() -> None:
    box = trimesh.creation.box(extents=[2.0, 2.0, 2.0])
    dq = MeshDistanceQuery(box)
    q = np.array(
        [
            [0.0, 0.0, 3.0],
            [0.0, 0.0, 0.0],
            [5.0, 5.0, 5.0],
        ]
    )
    closest, dist, face_id = dq.query(q, k=8)
    assert np.allclose(dist[0], 2.0, atol=1e-9)       # top face at z=+1
    assert np.isclose(dist[1], 1.0, atol=1e-9)        # inside: nearest wall
    assert np.allclose(dist[2], np.sqrt(48.0), atol=1e-9)  # corner: |(5,5,5)-(1,1,1)| = 4√3
    assert face_id.min() >= 0 and face_id.max() < len(box.faces)
    # Self-consistency: closest is actually at the reported distance.
    computed = np.linalg.norm(closest - q, axis=1)
    assert np.allclose(dist, computed, atol=1e-9)


def test_sample_surface_is_area_weighted() -> None:
    # Two thin plates: a big 10x10 plate and a small 1x1 plate, sharing
    # normal direction so a naive uniform-in-face sampler would give 50%
    # to each, but area weighting gives the big plate 100/101 of samples.
    thin = trimesh.creation.box(extents=[10.0, 10.0, 0.1])
    thin.apply_translation([0.0, 0.0, 10.0])
    small = trimesh.creation.box(extents=[1.0, 1.0, 0.1])
    two = trimesh.util.concatenate([thin, small])
    rng = np.random.default_rng(3)
    pts = _sample_surface(two, 20000, rng)
    frac_on_thin = float((pts[:, 2] > 5.0).mean())
    expected = 2.0 * (10.0 * 10.0) / (2.0 * (10.0 * 10.0) + 2.0 * (1.0 * 1.0))
    assert abs(frac_on_thin - expected) < 0.02, (frac_on_thin, expected)


def test_query_returns_valid_face_ids() -> None:
    box = trimesh.creation.box(extents=[2.0, 2.0, 2.0])
    dq = MeshDistanceQuery(box)
    rng = np.random.default_rng(11)
    q = rng.uniform(-3.0, 3.0, size=(200, 3))
    _, _, face_id = dq.query(q, k=8)
    assert face_id.shape == (200,)
    assert face_id.min() >= 0 and face_id.max() < len(box.faces)


if __name__ == "__main__":
    test_interior_projection()
    test_edge_and_vertex_regions()
    test_multiple_faces_and_k()
    test_sample_surface_is_area_weighted()
    test_query_returns_valid_face_ids()
    print("All MeshDistanceQuery unit tests passed")
