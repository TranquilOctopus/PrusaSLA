from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np
import trimesh
from scipy.spatial import cKDTree
from scipy.spatial.transform import Rotation

_EPS = 1e-12


@dataclass
class RegistrationResult:
    transform: np.ndarray
    rms: float
    p95: float
    inlier_fraction: float
    iterations: int
    converged: bool
    candidate_errors: list[float] = field(default_factory=list)


class RegistrationError(ValueError):
    def __init__(self, result: RegistrationResult):
        self.result = result
        super().__init__(
            f"Registration failed: inlier_fraction={result.inlier_fraction:.4f}, "
            f"rms={result.rms:.4f} mm, p95={result.p95:.4f} mm"
        )


def _sample_surface(
    mesh: trimesh.Trimesh, count: int, rng: np.random.Generator
) -> np.ndarray:
    weights = np.asarray(mesh.area_faces, dtype=np.float64)
    weights = weights / weights.sum()
    tri = mesh.triangles[rng.choice(len(weights), size=count, p=weights)]
    uv = rng.random((count, 2))
    flip = uv.sum(axis=1) > 1.0
    uv[flip] = 1.0 - uv[flip]
    return (
        tri[:, 0]
        + uv[:, :1] * (tri[:, 1] - tri[:, 0])
        + uv[:, 1:] * (tri[:, 2] - tri[:, 0])
    )


def _sample_surface_with_normals(
    mesh: trimesh.Trimesh, count: int, rng: np.random.Generator
) -> tuple[np.ndarray, np.ndarray]:
    weights = np.asarray(mesh.area_faces, dtype=np.float64)
    weights = weights / weights.sum()
    face_idx = rng.choice(len(weights), size=count, p=weights)
    tri = mesh.triangles[face_idx]
    uv = rng.random((count, 2))
    flip = uv.sum(axis=1) > 1.0
    uv[flip] = 1.0 - uv[flip]
    points = (
        tri[:, 0]
        + uv[:, :1] * (tri[:, 1] - tri[:, 0])
        + uv[:, 1:] * (tri[:, 2] - tri[:, 0])
    )
    normals = mesh.face_normals[face_idx]
    return points, normals


class ICPQuery:
    """Fast point-to-plane ICP queries using pre-sampled surface with normals."""

    def __init__(
        self, mesh: trimesh.Trimesh, sample_count: int = 20000, rng: np.random.Generator | None = None
    ):
        rng = rng if rng is not None else np.random.default_rng(0)
        self.points, self.normals = _sample_surface_with_normals(mesh, sample_count, rng)
        self.tree = cKDTree(self.points)

    def query(self, points: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
        """Returns (closest_points, distances, point_normals) for each query point."""
        points = np.asarray(points, dtype=np.float64).reshape(-1, 3)
        _, idx = self.tree.query(points, k=1, workers=1)
        closest = self.points[idx]
        distances = np.linalg.norm(points - closest, axis=1)
        normals = self.normals[idx]
        return closest, distances, normals


def coarse_alignment(
    model: trimesh.Trimesh,
    scene: trimesh.Trimesh,
    rng: np.random.Generator,
    samples: int = 4000,
) -> list[np.ndarray]:
    model_pts = _sample_surface(model, samples, rng)
    scene_pts = _sample_surface(scene, samples, rng)
    _, vm = np.linalg.eigh(np.cov(model_pts.T))
    _, vs = np.linalg.eigh(np.cov(scene_pts.T))
    vm[:, 0] *= np.linalg.det(vm)
    vs[:, 0] *= np.linalg.det(vs)
    cm = model_pts.mean(axis=0)
    cs = scene_pts.mean(axis=0)
    tree = cKDTree(scene_pts)
    scored = []
    for signs in ((1, 1, 1), (1, -1, -1), (-1, 1, -1), (-1, -1, 1)):
        rotation = vs @ np.diag(signs) @ vm.T
        transform = np.eye(4)
        transform[:3, :3] = rotation
        transform[:3, 3] = cs - rotation @ cm
        moved = trimesh.transform_points(model_pts, transform)
        distances, _ = tree.query(moved, workers=1)
        scored.append((float(np.median(distances)), transform))
    scored.sort(key=lambda item: item[0])
    return [transform for _, transform in scored]


def _increment(normals: np.ndarray, src: np.ndarray, tgt: np.ndarray) -> np.ndarray:
    center = src.mean(axis=0)
    design = np.hstack([np.cross(src - center, normals), normals])
    rhs = np.sum((tgt - src) * normals, axis=1)
    solution, *_ = np.linalg.lstsq(design, rhs, rcond=None)
    rotation = Rotation.from_rotvec(solution[:3]).as_matrix()
    transform = np.eye(4)
    transform[:3, :3] = rotation
    transform[:3, 3] = center + solution[3:] - rotation @ center
    return transform


def register(
    model: trimesh.Trimesh,
    supported_scene: trimesh.Trimesh,
    rng: np.random.Generator,
    starts: int = 4,
    iterations: int = 60,
    sample_count: int = 1500,
    trim_fraction: float = 0.85,
    start_threshold: float | None = None,
    final_threshold: float = 0.3,
    tolerance: float = 1e-5,
    min_inlier_fraction: float = 0.9,
) -> RegistrationResult:
    if not 1 <= starts <= 4 or iterations < 1 or sample_count < 20:
        raise ValueError("Invalid registration work limits")
    if not 0 < trim_fraction <= 1 or not 0.9 <= min_inlier_fraction <= 1:
        raise ValueError("Invalid registration fractions")
    if final_threshold <= 0 or tolerance <= 0:
        raise ValueError("Threshold and tolerance must be positive")
    if start_threshold is None:
        start_threshold = float(np.linalg.norm(supported_scene.extents) / 4.0)
    start_threshold = max(start_threshold, final_threshold)

    candidates = coarse_alignment(model, supported_scene, rng)
    icp_query = ICPQuery(supported_scene, sample_count=20000, rng=rng)

    src = _sample_surface(model, sample_count, rng)
    validation = _sample_surface(model, 4000, rng)

    results = []
    for initial in candidates[:starts]:
        transform = initial.copy()
        converged = False
        for it in range(iterations):
            moved = trimesh.transform_points(src, transform)
            closest, distances, normals = icp_query.query(moved)
            fraction = min(1.0, it / max(iterations // 2, 1))
            threshold = start_threshold * (
                final_threshold / start_threshold
            ) ** fraction
            inliers = np.flatnonzero(distances <= threshold)
            if len(inliers) < 20:
                break
            count = max(20, int(trim_fraction * len(inliers)))
            selected = inliers[np.argsort(distances[inliers])[:count]]
            increment = _increment(
                normals[selected],
                moved[selected],
                closest[selected],
            )
            transform = increment @ transform
            step = trimesh.transform_points(moved[selected], increment)
            if np.max(np.linalg.norm(step - moved[selected], axis=1)) < tolerance:
                converged = True
                break

        moved = trimesh.transform_points(validation, transform)
        closest, distances, _ = icp_query.query(moved)
        stats = _residuals(distances, final_threshold)
        results.append(
            RegistrationResult(
                transform=transform,
                **stats,
                iterations=it + 1,
                converged=converged,
            )
        )

    best = min(
        results,
        key=lambda result: (
            result.inlier_fraction < min_inlier_fraction,
            -result.inlier_fraction,
            result.rms,
        ),
    )
    best.candidate_errors = [result.rms for result in results]
    if best.inlier_fraction < min_inlier_fraction:
        raise RegistrationError(best)
    return best


def print_tilt(transform: np.ndarray) -> float:
    axis = transform[:3, :3] @ np.array([0.0, 0.0, 1.0])
    axis /= np.linalg.norm(axis)
    return float(np.degrees(np.arccos(np.clip(axis[2], -1.0, 1.0))))


def _residuals(distances: np.ndarray, threshold: float) -> dict[str, float]:
    keep = distances <= threshold
    return {
        "rms": float(np.sqrt(np.mean(distances[keep] ** 2))) if keep.any() else np.inf,
        "p95": float(np.percentile(distances[keep], 95)) if keep.any() else np.inf,
        "inlier_fraction": float(keep.mean()),
    }


class MeshDistanceQuery:
    """Exact closest-point queries against a triangle soup via cKDTree.

    A target radius bounds each stored triangle's centroid-to-vertex
    distance: oversized input triangles are subdivided into a uniform
    midpoint grid so one candidate radius serves every triangle. Stage 1
    evaluates the k nearest centroids exactly; stage 2 extends the
    candidate set to best-distance + radius, which provably contains any
    triangle holding the true closest point, so the result equals the
    brute-force minimum. Returned face ids index the *input* mesh.
    """

    def __init__(self, mesh: trimesh.Trimesh, target_radius: float = 2.0):
        self.face_normals = np.asarray(mesh.face_normals, dtype=np.float64)
        base = np.asarray(mesh.triangles, dtype=np.float64)
        if not len(base):
            raise ValueError("Mesh must contain triangles")
        self.subdivision = 0
        while self._max_radius(base) > target_radius and self.subdivision < 6:
            base = self._subdivide_once(base)
            self.subdivision += 1
        self.triangles = base
        counts = 4**self.subdivision
        self.source_of = np.repeat(np.arange(len(mesh.faces)), counts)
        self.tree = cKDTree(self.triangles.mean(axis=1))
        self.max_centroid_radius = self._max_radius(self.triangles)

    @staticmethod
    def _max_radius(triangles: np.ndarray) -> float:
        centroids = triangles.mean(axis=1)
        return float(
            np.linalg.norm(triangles - centroids[:, None, :], axis=2).max()
        )

    @staticmethod
    def _subdivide_once(triangles: np.ndarray) -> np.ndarray:
        a, b, c = triangles[:, 0], triangles[:, 1], triangles[:, 2]
        ab = (a + b) / 2
        bc = (b + c) / 2
        ca = (c + a) / 2
        m = triangles.mean(axis=1)
        quarters = np.stack(
            [
                np.stack([a, ab, ca], axis=1),
                np.stack([ab, b, bc], axis=1),
                np.stack([ca, bc, c], axis=1),
                np.stack([ab, bc, ca], axis=1),
            ],
            axis=1,
        )
        return quarters.reshape(-1, 3, 3)

    def query(
        self, points: np.ndarray, k: int = 8
    ) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
        points = np.asarray(points, dtype=np.float64).reshape(-1, 3)
        closest = np.empty_like(points)
        distance = np.empty(len(points))
        face_id = np.empty(len(points), dtype=np.int64)
        k = min(max(k, 1), 8)
        for start in range(0, len(points), 32):
            batch = points[start : start + 32]
            n = len(batch)
            _, seed = self.tree.query(batch, k=k, workers=1)
            seed = seed.reshape(n, k)
            tri = self.triangles[seed]
            _, initial = self._closest_on_tri(
                batch[:, None, :], tri[:, :, 0], tri[:, :, 1], tri[:, :, 2]
            )
            radius = initial.min(axis=(1, 2)) + self.max_centroid_radius
            candidates = self.tree.query_ball_point(
                batch, r=radius + _EPS, workers=1
            )
            width = max(max(map(len, candidates)), 1)
            indices = np.repeat(seed[:, :1], width, axis=1)
            for row, ids in enumerate(candidates):
                indices[row, : len(ids)] = ids
            tri = self.triangles[indices]
            positions, distances = self._closest_on_tri(
                batch[:, None, :], tri[:, :, 0], tri[:, :, 1], tri[:, :, 2]
            )
            best = distances.reshape(n, -1).argmin(axis=1)
            rows = np.arange(n)
            closest[start : start + n] = positions.reshape(n, -1, 3)[rows, best]
            distance[start : start + n] = distances.reshape(n, -1)[rows, best]
            face_id[start : start + n] = self.source_of[
                indices[rows, best // 7]
            ]
        return closest, distance, face_id

    @staticmethod
    def _closest_on_tri(P: np.ndarray, A: np.ndarray, B: np.ndarray, C: np.ndarray):
        ab = B - A
        ac = C - A
        bc = C - B
        normal = np.cross(ab, ac)
        nn = np.sum(normal * normal, axis=-1)
        t = np.sum(normal * (A - P), axis=-1) / np.maximum(nn, _EPS)
        proj = P + t[..., None] * normal
        inside = (
            (np.sum(normal * np.cross(ab, proj - A), axis=-1) >= -_EPS)
            & (np.sum(normal * np.cross(bc, proj - B), axis=-1) >= -_EPS)
            & (np.sum(normal * np.cross(-ac, proj - C), axis=-1) >= -_EPS)
            & (nn > _EPS)
        )
        edges = []
        for origin, edge in ((A, ab), (A, ac), (B, bc)):
            fraction = np.sum((P - origin) * edge, axis=-1) / np.maximum(
                np.sum(edge * edge, axis=-1), _EPS
            )
            edges.append(origin + np.clip(fraction, 0.0, 1.0)[..., None] * edge)
        in_face = np.where(inside[..., None], proj, edges[0])
        candidates = np.stack([A, B, C, *edges, in_face], axis=2)
        distances = np.linalg.norm(candidates - P[:, :, None, :], axis=-1)
        return candidates, distances


def residual_stats(
    model: trimesh.Trimesh,
    supported_scene: trimesh.Trimesh,
    transform: np.ndarray,
    threshold: float = 0.3,
    samples: int = 4000,
    rng: np.random.Generator | None = None,
) -> dict[str, float]:
    rng = rng if rng is not None else np.random.default_rng(7)
    points = _sample_surface(model, samples, rng)
    moved = trimesh.transform_points(points, transform)
    exact_query = MeshDistanceQuery(supported_scene)
    _, distances, _ = exact_query.query(moved)
    return _residuals(distances, threshold)