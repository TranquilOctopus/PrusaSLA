from __future__ import annotations

from dataclasses import dataclass

import numpy as np
import trimesh
from scipy.spatial import cKDTree


@dataclass
class SeparationResult:
    centroid_distances: np.ndarray
    support_mask: np.ndarray

    @property
    def model_mask(self) -> np.ndarray:
        return ~self.support_mask


def _sample_surface_dense(
    mesh: trimesh.Trimesh, target_count: int, rng: np.random.Generator
) -> np.ndarray:
    """Sample points on mesh surface with area-weighted distribution."""
    weights = np.asarray(mesh.area_faces, dtype=np.float64)
    total_area = weights.sum()
    if total_area <= 0:
        raise ValueError("Mesh must have positive surface area")
    weights = weights / total_area
    face_idx = rng.choice(len(weights), size=target_count, p=weights)
    tri = mesh.triangles[face_idx]
    uv = rng.random((target_count, 2))
    flip = uv.sum(axis=1) > 1.0
    uv[flip] = 1.0 - uv[flip]
    points = (
        tri[:, 0]
        + uv[:, :1] * (tri[:, 1] - tri[:, 0])
        + uv[:, 1:] * (tri[:, 2] - tri[:, 0])
    )
    return points


def _transform_centroids_to_model_frame(
    centroids: np.ndarray, transform: np.ndarray
) -> np.ndarray:
    """Transform scene centroids to model frame using inverse of rigid transform."""
    rotation = transform[:3, :3]
    translation = transform[:3, 3]
    return (centroids - translation) @ rotation


def separate_supports(
    model: trimesh.Trimesh,
    supported_scene: trimesh.Trimesh,
    model_to_scene: np.ndarray,
    epsilon: float = 0.3,
    batch_size: int = 32,
) -> SeparationResult:
    """Exact separation: compute exact closest-point distances for all face centroids."""
    if (
        isinstance(batch_size, (bool, np.bool_))
        or not isinstance(batch_size, (int, np.integer))
        or batch_size < 1
    ):
        raise ValueError("Batch size must be a positive integer")
    transform = _validate_common(model, supported_scene, model_to_scene, epsilon)

    distances = _exact_distances(
        model,
        supported_scene,
        transform,
        np.arange(len(supported_scene.faces)),
        batch_size,
    )
    return SeparationResult(
        centroid_distances=distances,
        support_mask=distances > epsilon,
    )


def _validate_common(
    model: trimesh.Trimesh,
    supported_scene: trimesh.Trimesh,
    model_to_scene: np.ndarray,
    epsilon: float,
) -> np.ndarray:
    """Shared input validation; returns the transform as a float array."""
    if not np.isfinite(epsilon) or epsilon < 0:
        raise ValueError("Epsilon must be finite and nonnegative")
    transform = np.asarray(model_to_scene, dtype=np.float64)
    if transform.shape != (4, 4) or not np.isfinite(transform).all():
        raise ValueError("Transform must be a finite 4 by 4 matrix")
    rotation = transform[:3, :3]
    if (
        not np.allclose(transform[3], [0, 0, 0, 1], atol=1e-8, rtol=0)
        or not np.allclose(rotation.T @ rotation, np.eye(3), atol=1e-8, rtol=0)
        or not np.isclose(np.linalg.det(rotation), 1.0, atol=1e-8, rtol=0)
    ):
        raise ValueError("Transform must contain a proper rigid rotation and translation")
    if not len(model.faces):
        raise ValueError("Model must contain triangles")
    for mesh in (model, supported_scene):
        if not np.isfinite(mesh.vertices).all():
            raise ValueError("Mesh vertices must be finite")
    return transform


def _exact_distances(
    model: trimesh.Trimesh,
    supported_scene: trimesh.Trimesh,
    transform: np.ndarray,
    face_indices: np.ndarray,
    batch_size: int,
) -> np.ndarray:
    """Exact centroid-to-model-surface distances for the given scene faces."""
    out = np.empty(len(face_indices), dtype=np.float64)
    for start in range(0, len(face_indices), batch_size):
        batch = face_indices[start : start + batch_size]
        centroids = supported_scene.vertices[supported_scene.faces[batch]].mean(axis=1)
        points = _transform_centroids_to_model_frame(centroids, transform)
        _, distances, _ = trimesh.proximity.closest_point(model, points)
        if not np.isfinite(distances).all():
            raise ValueError("Surface distance query returned nonfinite distances")
        out[start : start + len(batch)] = distances
    return out


def separate_supports_scaled(
    model: trimesh.Trimesh,
    supported_scene: trimesh.Trimesh,
    model_to_scene: np.ndarray,
    epsilon: float = 0.3,
    sample_spacing: float | None = None,
    max_samples: int = 2_000_000,
    chunk_size: int = 100_000,
    batch_size: int = 32,
    rng: np.random.Generator | None = None,
) -> SeparationResult:
    """Separate supports from model faces on meshes with millions of faces.

    Same classification as :func:`separate_supports`, but most faces are decided
    from a KD-tree over area-weighted samples of the model surface, and only an
    ambiguous band is measured exactly.

    The nearest sample distance ``d_s`` is an upper bound of the true surface
    distance ``d``: ``d <= d_s``. So ``d_s < epsilon`` already proves a face is
    model, with no margin needed. The other direction needs one: a face can sit
    up to the sample spacing further from the nearest sample than from the
    surface, so a face counts as support only when ``d_s > epsilon + safety``,
    where ``safety`` is derived from the achieved sample spacing. Everything in
    between is measured exactly.

    Args:
        sample_spacing: target distance between surface samples (mm). Defaults
            to ``epsilon / 2``, or 0.1 mm when epsilon is 0.
        max_samples: cap on samples, bounding memory (2M samples is ~48 MB).
        chunk_size: scene faces processed per KD-tree query chunk.
        batch_size: scene faces per exact closest-point query.
    """
    transform = _validate_common(model, supported_scene, model_to_scene, epsilon)
    if sample_spacing is not None and (not np.isfinite(sample_spacing) or sample_spacing <= 0):
        raise ValueError("Sample spacing must be finite and positive")
    for name, value in (("Max samples", max_samples), ("Chunk size", chunk_size), ("Batch size", batch_size)):
        if isinstance(value, (bool, np.bool_)) or not isinstance(value, (int, np.integer)) or value < 1:
            raise ValueError(f"{name} must be a positive integer")

    face_count = len(supported_scene.faces)
    if face_count == 0:
        return SeparationResult(
            centroid_distances=np.empty(0, dtype=np.float64),
            support_mask=np.empty(0, dtype=bool),
        )

    rng = rng if rng is not None else np.random.default_rng(0)
    spacing = sample_spacing if sample_spacing is not None else (epsilon / 2 if epsilon > 0 else 0.1)
    area = float(np.asarray(model.area_faces, dtype=np.float64).sum())
    if area <= 0:
        raise ValueError("Mesh must have positive surface area")
    # One sample per spacing^2 of surface, bounded by max_samples.
    sample_count = int(min(max(int(np.ceil(area / (spacing * spacing))), 1_000), max_samples))
    samples = _sample_surface_dense(model, sample_count, rng)
    tree = cKDTree(samples)

    # Safety margin from the achieved spacing: the mean nearest-neighbour
    # distance among samples, scaled up, and never below the requested spacing.
    probe = samples if len(samples) <= 20_000 else samples[rng.choice(len(samples), 20_000, replace=False)]
    neighbour_distances, _ = tree.query(probe, k=2, workers=1)
    safety = max(float(neighbour_distances[:, 1].mean()) * 4.0, spacing)

    approx = np.empty(face_count, dtype=np.float64)
    for start in range(0, face_count, chunk_size):
        faces = supported_scene.faces[start : start + chunk_size]
        centroids = supported_scene.vertices[faces].mean(axis=1)
        points = _transform_centroids_to_model_frame(centroids, transform)
        approx[start : start + len(faces)], _ = tree.query(points, k=1, workers=1)

    clearly_model = approx < epsilon
    clearly_support = approx > epsilon + safety
    ambiguous = np.flatnonzero(~(clearly_model | clearly_support))

    distances = approx
    if len(ambiguous):
        distances[ambiguous] = _exact_distances(
            model, supported_scene, transform, ambiguous, batch_size
        )
    return SeparationResult(
        centroid_distances=distances,
        support_mask=distances > epsilon,
    )
