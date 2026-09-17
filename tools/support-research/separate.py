from __future__ import annotations

from dataclasses import dataclass

import numpy as np
import trimesh


@dataclass
class SeparationResult:
    centroid_distances: np.ndarray
    support_mask: np.ndarray

    @property
    def model_mask(self) -> np.ndarray:
        return ~self.support_mask


def separate_supports(
    model: trimesh.Trimesh,
    supported_scene: trimesh.Trimesh,
    model_to_scene: np.ndarray,
    epsilon: float = 0.3,
    batch_size: int = 32,
) -> SeparationResult:
    if not np.isfinite(epsilon) or epsilon < 0:
        raise ValueError("Epsilon must be finite and nonnegative")
    if (
        isinstance(batch_size, (bool, np.bool_))
        or not isinstance(batch_size, (int, np.integer))
        or batch_size < 1
    ):
        raise ValueError("Batch size must be a positive integer")
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

    distances = np.empty(len(supported_scene.faces), dtype=np.float64)
    for start in range(0, len(distances), batch_size):
        faces = supported_scene.faces[start : start + batch_size]
        centroids = supported_scene.vertices[faces].mean(axis=1)
        points = (centroids - transform[:3, 3]) @ rotation
        _, batch_distances, _ = trimesh.proximity.closest_point(model, points)
        if not np.isfinite(batch_distances).all():
            raise ValueError("Surface distance query returned nonfinite distances")
        distances[start : start + len(faces)] = batch_distances
    return SeparationResult(
        centroid_distances=distances,
        support_mask=distances > epsilon,
    )
