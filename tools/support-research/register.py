"""Rigid registration of a plain model into a supported scene (M7.3a, M7.3d).

The supported file is the model plus a support forest plus a raft, rotated into
print orientation and often remeshed, so the principal axes of the whole scene
say nothing about the model's pose: the coarse start has to survive the extra
geometry instead of assuming it away. The search here sweeps rotations that are
spread over SO(3), lets every rotation propose its own translations by voting,
and keeps the poses with the smallest trimmed model-to-scene distance. Trimmed
point-to-plane ICP then converges the survivors, ending on exact triangle
distances, so the reported residual is the real surface deviation and not the
sampling noise of a point cloud.
"""
from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np
import trimesh
from scipy.spatial import cKDTree
from scipy.spatial.transform import Rotation

_EPS = 1e-12

# Global search: how many rotations, how many votes, how coarse the vote bin.
ROTATION_COUNT = 512
LEVEL1_KEEP = 32
LEVEL2_SWEEP = 6
LEVEL2_KEEP = 8
LEVEL3_SWEEP = 4
LEVEL3_POOL = 32
LEVEL4_POOL = 24
VOTE_POINTS = 192
SCORE_POINTS = 384
VOTE_BIN_MM = 1.2
FINE_BIN_MM = 0.5
REFINE_ANGLE_DEG = 12.0
FINE_ANGLE_DEG = 3.0
REFINE_STEPS = 7
FINE_STEPS = 5
CLOUD_SPACING_MM = 0.3
CLOUD_LIMIT = 120_000
COVERAGE_MM = 2.0
COVERAGE_SAMPLES = 4000
POSE_DEDUPE_DEG = 1.0
POSE_DEDUPE_MM = 0.5

# ICP: coarse point cloud first, exact triangles last.
CLOUD_SCALES = 4
EXACT_SCALES = 2
EXACT_SAMPLES = 1000
EXACT_ITERATIONS = 12
COARSE_END_FACTOR = 3.0
MIN_CORRESPONDENCES = 20
BACKTRACK_SLACK = 1.02
STALL_RATIO = 5e-4
STAGE_IMPROVEMENT = 1e-3

WIDE_THRESHOLD_MM = 0.5


@dataclass
class RegistrationResult:
    transform: np.ndarray
    rms: float
    p95: float
    inlier_fraction: float
    iterations: int
    converged: bool
    candidate_errors: list[float] = field(default_factory=list)
    inlier_fraction_wide: float = 0.0
    wide_threshold: float = WIDE_THRESHOLD_MM
    coarse_score: float = float("inf")


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


def _surface_cloud(
    mesh: trimesh.Trimesh,
    rng: np.random.Generator,
    spacing: float = CLOUD_SPACING_MM,
    limit: int = CLOUD_LIMIT,
) -> tuple[np.ndarray, np.ndarray]:
    """Points and normals spaced by `spacing` mm, or all of them up to `limit`.

    One sample every `spacing` mm keeps the nearest-sample distance small
    enough to score a pose with, without touching every vertex of a mesh with
    two million triangles.
    """
    area = float(np.asarray(mesh.area_faces, dtype=np.float64).sum())
    if area <= 0.0:
        raise ValueError("Mesh has no area to sample")
    count = int(min(limit, max(4000.0, area / (spacing * spacing))))
    return _sample_surface_with_normals(mesh, count, rng)


def _raft_free_centroid(points: np.ndarray) -> np.ndarray:
    """Centre of the scene without the bottom slab, i.e. without the raft."""
    height = points[:, 2]
    span = float(height.max() - height.min())
    keep = height > height.min() + 0.05 * span if span > 0.0 else np.ones(len(points), bool)
    return points[keep].mean(axis=0)


def _halton(count: int, base: int, skip: int = 1) -> np.ndarray:
    """The first `count` Halton numbers of `base`, a low-discrepancy sequence."""
    index = np.arange(skip, skip + count, dtype=np.float64)
    numbers = np.zeros(count, dtype=np.float64)
    fraction = 1.0
    remaining = index.copy()
    while np.any(remaining >= 1.0):
        fraction /= base
        numbers += fraction * (remaining % base)
        remaining = np.floor(remaining / base)
    return numbers


def _quaternion_to_matrix(quaternions: np.ndarray) -> np.ndarray:
    """Proper rotation matrices from unit quaternions laid out as (x, y, z, w)."""
    x, y, z, w = (quaternions[:, index] for index in range(4))
    matrices = np.empty((len(quaternions), 3, 3), dtype=np.float64)
    matrices[:, 0, 0] = 1.0 - 2.0 * (y * y + z * z)
    matrices[:, 0, 1] = 2.0 * (x * y - z * w)
    matrices[:, 0, 2] = 2.0 * (x * z + y * w)
    matrices[:, 1, 0] = 2.0 * (x * y + z * w)
    matrices[:, 1, 1] = 1.0 - 2.0 * (x * x + z * z)
    matrices[:, 1, 2] = 2.0 * (y * z - x * w)
    matrices[:, 2, 0] = 2.0 * (x * z - y * w)
    matrices[:, 2, 1] = 2.0 * (y * z + x * w)
    matrices[:, 2, 2] = 1.0 - 2.0 * (x * x + y * y)
    return matrices


def _so3_sweep(count: int = ROTATION_COUNT) -> np.ndarray:
    """Rotations spread over the whole of SO(3): Shoemake's map of a Halton cube.

    A Halton point set is far better spread than random sampling, so a few
    hundred rotations already put one within ~10 degrees of any rotation.
    """
    u1 = _halton(count, 2, skip=7)
    u2 = _halton(count, 3, skip=5)
    u3 = _halton(count, 5, skip=3)
    outer = np.sqrt(1.0 - u1)
    inner = np.sqrt(u1)
    return _quaternion_to_matrix(
        np.column_stack(
            [
                outer * np.sin(2.0 * np.pi * u2),
                outer * np.cos(2.0 * np.pi * u2),
                inner * np.sin(2.0 * np.pi * u3),
                inner * np.cos(2.0 * np.pi * u3),
            ]
        )
    )


def _axis_aligned_rotations() -> np.ndarray:
    """The 24 proper rotations that map the coordinate axes onto each other."""
    from itertools import permutations, product

    matrices = []
    for perm in permutations(range(3)):
        for signs in product((1.0, -1.0), repeat=3):
            matrix = np.zeros((3, 3))
            for row, column in enumerate(perm):
                matrix[row, column] = signs[row]
            if np.linalg.det(matrix) > 0:
                matrices.append(matrix)
    return np.array(matrices)


def _principal_axes_rotations(
    model_points: np.ndarray, scene_points: np.ndarray
) -> np.ndarray:
    """The four flip-disambiguated principal-axes starts of the first version.

    Wrong as a *ranked* search, since the supports and the raft own the
    scene's axes, but cheap and sometimes right, so they stay in the sweep.
    """
    _, model_frame = np.linalg.eigh(np.cov(model_points.T))
    _, scene_frame = np.linalg.eigh(np.cov(scene_points.T))
    model_frame[:, 0] *= np.linalg.det(model_frame)
    scene_frame[:, 0] *= np.linalg.det(scene_frame)
    return np.array(
        [
            scene_frame @ np.diag(signs) @ model_frame.T
            for signs in ((1, 1, 1), (1, -1, -1), (-1, 1, -1), (-1, -1, 1))
        ]
    )


def _local_sweep(rotation: np.ndarray, angle_deg: float, steps: int) -> np.ndarray:
    """Small proper rotations about the world axes, all applied around `rotation`."""
    angles = np.deg2rad(np.linspace(-angle_deg, angle_deg, steps))
    about_x = [Rotation.from_rotvec([angle, 0.0, 0.0]).as_matrix() for angle in angles]
    about_y = [Rotation.from_rotvec([0.0, angle, 0.0]).as_matrix() for angle in angles]
    return np.array([first @ second @ rotation for first in about_x for second in about_y])


def _trimmed_mean(distances: np.ndarray, keep: float = 0.4) -> np.ndarray:
    """Mean of the smallest `keep` fraction of every row.

    Trimming is what lets the model-side score survive a scene that is mostly
    support: the points that found no surface are dropped instead of counted.
    """
    values = np.atleast_2d(np.asarray(distances, dtype=np.float64))
    count = max(1, min(values.shape[1], int(round(keep * values.shape[1]))))
    return np.partition(values, count - 1, axis=1)[:, :count].mean(axis=1)


def _vote_peaks(
    rotated: np.ndarray,
    tree: cKDTree,
    points: np.ndarray,
    bin_size: float = VOTE_BIN_MM,
    count: int = 2,
) -> list[np.ndarray]:
    """Translations the scene proposes for one already rotated model.

    Every rotated model point votes with the vector to its nearest scene
    point. The right translation collects most of the votes into one cell of
    a `bin_size` grid, so the peaks of that histogram are the hypotheses.
    """
    _, index = tree.query(rotated, workers=-1)
    votes = points[np.asarray(index, dtype=np.int64)] - rotated
    lower = votes.min(axis=0) - bin_size
    cells = np.floor((votes - lower) / bin_size).astype(np.int64)
    span = cells.max(axis=0) + 1
    flat = (cells[:, 0] * span[1] + cells[:, 1]) * span[2] + cells[:, 2]
    unique, weights = np.unique(flat, return_counts=True)
    peaks: list[np.ndarray] = []
    taken: list[np.ndarray] = []
    for cell_id, _ in sorted(zip(unique.tolist(), weights.tolist()), key=lambda item: -item[1]):
        cell = np.array(np.unravel_index(cell_id, span), dtype=np.float64)
        if any(np.abs(cell - other).max() < 2.0 for other in taken):
            continue
        taken.append(cell)
        peaks.append(votes[flat == cell_id].mean(axis=0))
        if len(peaks) == count:
            break
    return peaks


def _score_poses(tree: cKDTree, rotated: np.ndarray, hypotheses: np.ndarray) -> np.ndarray:
    """Trimmed model-to-scene distance of every candidate translation."""
    poses = np.asarray(hypotheses, dtype=np.float64).reshape(-1, 1, 3)
    moved = rotated[None, :, :] + poses
    distances, _ = tree.query(moved.reshape(-1, 3), workers=-1)
    return _trimmed_mean(distances.reshape(len(poses), len(rotated)))


def _refine_translation(
    tree: cKDTree,
    rotated: np.ndarray,
    translation: np.ndarray,
    step: float = VOTE_BIN_MM,
    rounds: int = 2,
    width: int = 3,
) -> tuple[np.ndarray, float]:
    """Grid refinement of one translation, halving the step every round."""
    axis = np.arange(width) - (width - 1) / 2.0
    grid = np.stack(np.meshgrid(axis, axis, axis, indexing="ij"), axis=-1).reshape(-1, 3) * step
    best = np.asarray(translation, dtype=np.float64).reshape(3)
    best_score = float(_score_poses(tree, rotated, best[None])[0])
    for _ in range(rounds):
        scores = _score_poses(tree, rotated, best + grid)
        index = int(np.argmin(scores))
        if float(scores[index]) < best_score:
            best = best + grid[index]
            best_score = float(scores[index])
        step *= 0.5
    return best, best_score


def _coverage(
    rotated: np.ndarray, translation: np.ndarray, scene_points: np.ndarray
) -> float:
    """Share of the scene that lies within COVERAGE_MM of the moved model.

    A pose that only tucks the model between the support columns scores well
    on the model side and badly here, which is what separates it from the pose
    that actually sits on the model's own surface.
    """
    distances, _ = cKDTree(rotated + translation).query(scene_points, workers=-1)
    return float(np.mean(distances <= COVERAGE_MM))


def _angle_between(first: np.ndarray, second: np.ndarray) -> float:
    product = first.T @ second
    return float(np.degrees(np.arccos(np.clip((np.trace(product) - 1.0) / 2.0, -1.0, 1.0))))


def _unique_poses(
    ranked: list[tuple[float, float, np.ndarray, np.ndarray]], keep: int
) -> list[tuple[np.ndarray, np.ndarray]]:
    """The best `keep` poses, skipping the ones that repeat an earlier pose."""
    chosen: list[tuple[np.ndarray, np.ndarray]] = []
    for _, _, rotation, translation in ranked:
        if any(
            _angle_between(rotation, other) < POSE_DEDUPE_DEG
            and float(np.linalg.norm(translation - other_translation)) < POSE_DEDUPE_MM
            for other, other_translation in chosen
        ):
            continue
        chosen.append((rotation, translation))
        if len(chosen) == keep:
            break
    return chosen


def _as_transform(rotation: np.ndarray, translation: np.ndarray) -> np.ndarray:
    transform = np.eye(4)
    transform[:3, :3] = rotation
    transform[:3, 3] = translation
    return transform


def _pose_distance(query: ICPQuery, points: np.ndarray, transform: np.ndarray) -> float:
    """Trimmed distance from the model's own samples to the scene cloud."""
    moved = points @ transform[:3, :3].T + transform[:3, 3]
    return float(_trimmed_mean(query.tree.query(moved, workers=-1)[0])[0])


def coarse_alignment(
    model: trimesh.Trimesh,
    scene: trimesh.Trimesh,
    rng: np.random.Generator,
    samples: int = 4000,
    rotations: int = ROTATION_COUNT,
    keep: int = LEVEL2_KEEP,
    cloud: tuple[np.ndarray, np.ndarray] | None = None,
) -> list[np.ndarray]:
    """Poses that put the plain model on the supported scene, best first.

    The scene carries supports and a raft, so its principal axes are the raft's
    and matching them cannot be trusted (M7.3d). Instead a sweep of rotations
    spread over SO(3) is scored by voting: each rotation proposes the
    translations its nearest scene points imply, and the poses with the smallest
    trimmed distance survive into a local rotation and translation refinement.
    """
    if samples < VOTE_POINTS or rotations < 8 or keep < 1:
        raise ValueError("Invalid coarse search limits")
    model_points = _sample_surface(model, samples, rng)
    scene_points = cloud[0] if cloud is not None else _surface_cloud(scene, rng)[0]
    tree = cKDTree(scene_points)
    vote_points = model_points[:VOTE_POINTS]
    score_points = model_points[:SCORE_POINTS]
    centroid = _raft_free_centroid(scene_points)

    sweep = np.concatenate(
        [
            _so3_sweep(rotations),
            _axis_aligned_rotations(),
            _principal_axes_rotations(model_points, scene_points),
        ]
    )
    ranked = []
    for rotation in sweep:
        rotated_vote = vote_points @ rotation.T
        hypotheses = _vote_peaks(rotated_vote, tree, scene_points)
        hypotheses.append(centroid - rotated_vote.mean(axis=0))
        scores = _score_poses(tree, score_points @ rotation.T, hypotheses)
        index = int(np.argmin(scores))
        ranked.append((float(scores[index]), rotation, hypotheses[index]))
    ranked.sort(key=lambda item: item[0])

    refined = []
    for _, rotation, translation in ranked[:LEVEL1_KEEP]:
        moved, score = _refine_translation(tree, score_points @ rotation.T, translation)
        refined.append((score, rotation, moved))
    refined.sort(key=lambda item: item[0])

    swept = []
    for _, rotation, _ in refined[:LEVEL2_SWEEP]:
        for local in _local_sweep(rotation, REFINE_ANGLE_DEG, REFINE_STEPS):
            rotated_vote = vote_points @ local.T
            hypotheses = _vote_peaks(rotated_vote, tree, scene_points, FINE_BIN_MM, 1)
            scores = _score_poses(tree, score_points @ local.T, hypotheses)
            index = int(np.argmin(scores))
            swept.append((float(scores[index]), local, hypotheses[index]))
    swept.sort(key=lambda item: item[0])

    fine = []
    for _, rotation, _ in swept[:LEVEL3_SWEEP]:
        for local in _local_sweep(rotation, FINE_ANGLE_DEG, FINE_STEPS):
            rotated_vote = vote_points @ local.T
            hypotheses = _vote_peaks(rotated_vote, tree, scene_points, FINE_BIN_MM, 1)
            scores = _score_poses(tree, score_points @ local.T, hypotheses)
            index = int(np.argmin(scores))
            fine.append((float(scores[index]), local, hypotheses[index]))
    fine.sort(key=lambda item: item[0])

    coverage_points = scene_points[:: max(1, len(scene_points) // COVERAGE_SAMPLES)]
    pool = refined[:LEVEL1_KEEP // 2] + swept[:LEVEL3_POOL] + fine[:LEVEL4_POOL]
    scored = []
    for _, rotation, translation in pool:
        rotated = model_points @ rotation.T
        full = float(_trimmed_mean(tree.query(rotated + translation, workers=-1)[0])[0])
        scored.append((full, -_coverage(rotated, translation, coverage_points), rotation, translation))
    scored.sort(key=lambda item: (item[0], item[1]))
    return [
        _as_transform(rotation, translation)
        for rotation, translation in _unique_poses(scored, keep)
    ]


class ICPQuery:
    """Fast point-to-plane ICP queries using pre-sampled surface with normals."""

    def __init__(self, mesh: trimesh.Trimesh, sample_count: int = 20000, rng: np.random.Generator | None = None):
        rng = rng if rng is not None else np.random.default_rng(0)
        self.points, self.normals = _sample_surface_with_normals(mesh, sample_count, rng)
        self.tree = cKDTree(self.points)

    @classmethod
    def from_samples(cls, points: np.ndarray, normals: np.ndarray) -> ICPQuery:
        """The same query over a cloud somebody else already sampled."""
        instance = cls.__new__(cls)
        instance.points = np.asarray(points, dtype=np.float64)
        instance.normals = np.asarray(normals, dtype=np.float64)
        instance.tree = cKDTree(instance.points)
        return instance

    def query(self, points: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
        """Returns (closest_points, distances, point_normals) for each query point."""
        points = np.asarray(points, dtype=np.float64).reshape(-1, 3)
        _, idx = self.tree.query(points, k=1, workers=-1)
        closest = self.points[idx]
        distances = np.linalg.norm(points - closest, axis=1)
        normals = self.normals[idx]
        return closest, distances, normals


class ExactQuery:
    """The same interface as ICPQuery, but triangle-exact.

    The last ICP stages and every reported residual go through here: with a
    sampled cloud the nearest-neighbour distance never reaches zero, so the
    cloud alone would report the sampling spacing as if it were surface
    deviation.
    """

    def __init__(self, mesh: trimesh.Trimesh):
        self.normals = np.asarray(mesh.face_normals, dtype=np.float64)
        self.engine = MeshDistanceQuery(mesh)

    def query(self, points: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
        points = np.asarray(points, dtype=np.float64).reshape(-1, 3)
        closest, distances, face_id = self.engine.query(points)
        return closest, distances, self.normals[face_id]


def _solve_increment(normals: np.ndarray, src: np.ndarray, tgt: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """The small point-to-plane step around the centroid of the correspondences."""
    center = src.mean(axis=0)
    design = np.hstack([np.cross(src - center, normals), normals])
    rhs = np.sum((tgt - src) * normals, axis=1)
    solution, *_ = np.linalg.lstsq(design, rhs, rcond=None)
    return solution, center


def _compose_increment(solution: np.ndarray, center: np.ndarray, alpha: float) -> np.ndarray:
    rotation = Rotation.from_rotvec(solution[:3] * alpha).as_matrix()
    transform = np.eye(4)
    transform[:3, :3] = rotation
    transform[:3, 3] = center + alpha * solution[3:] - rotation @ center
    return transform


def _icp_stage(
    transform: np.ndarray,
    sources: np.ndarray,
    query: ICPQuery | ExactQuery,
    threshold: float,
    iterations: int,
    trim_fraction: float,
    tolerance: float,
) -> tuple[np.ndarray, bool]:
    """One threshold stage of trimmed point-to-plane ICP.

    A step that makes the correspondence worse is cut in half and retried, so
    a pose that started far away slides towards the model instead of jumping
    away from it, and the stage reports convergence once it stops improving.
    """
    converged = False
    first_score: float | None = None
    before = 0.0
    for iteration in range(iterations):
        moved = sources @ transform[:3, :3].T + transform[:3, 3]
        closest, distances, normals = query.query(moved)
        inliers = np.flatnonzero(distances <= threshold)
        if len(inliers) < MIN_CORRESPONDENCES:
            break
        keep = max(MIN_CORRESPONDENCES, int(trim_fraction * len(inliers)))
        selected = inliers[np.argpartition(distances[inliers], keep - 1)[:keep]]
        before = float(distances[selected].mean())
        first_score = before if first_score is None else first_score
        solution, center = _solve_increment(
            normals[selected], moved[selected], closest[selected]
        )
        after = float("inf")
        for alpha in (1.0, 0.5, 0.25):
            increment = _compose_increment(solution, center, alpha)
            trial = sources[selected] @ increment[:3, :3].T + increment[:3, 3]
            after = float(query.query(trial)[1].mean())
            if after <= before * BACKTRACK_SLACK + _EPS:
                break
        step = float(
            np.linalg.norm(
                (sources[selected] @ increment[:3, :3].T + increment[:3, 3])
                - moved[selected],
                axis=1,
            ).max()
        )
        transform = increment @ transform
        if step < tolerance or abs(before - after) <= STALL_RATIO * before:
            converged = True
            break
    else:
        converged = abs(first_score - before) <= STAGE_IMPROVEMENT * first_score
    return transform, converged


def register(
    model: trimesh.Trimesh,
    supported_scene: trimesh.Trimesh,
    rng: np.random.Generator,
    starts: int = LEVEL2_KEEP,
    iterations: int = 60,
    sample_count: int = 1500,
    trim_fraction: float = 0.85,
    start_threshold: float | None = None,
    final_threshold: float = 0.3,
    tolerance: float = 1e-5,
    min_inlier_fraction: float = 0.9,
    rotations: int = ROTATION_COUNT,
    coarse_keep: int = LEVEL2_KEEP,
    validation_samples: int = 2000,
    wide_threshold: float = WIDE_THRESHOLD_MM,
    exact_iterations: int = EXACT_ITERATIONS,
) -> RegistrationResult:
    """The transform that puts `model` onto `supported_scene`, and its residual.

    The residual is measured with exact triangle distances, so `rms`, `p95` and
    `inlier_fraction` describe the surface itself and not how densely the scene
    was sampled; `inlier_fraction_wide` repeats the fraction at the wider
    `wide_threshold`, which says how far off a mesh that no longer matches
    within the tolerance still is.
    """
    if not 1 <= starts <= 32 or iterations < 1 or sample_count < 20:
        raise ValueError("Invalid registration work limits")
    if not 0 < trim_fraction <= 1 or not 0.9 <= min_inlier_fraction <= 1:
        raise ValueError("Invalid registration fractions")
    if final_threshold <= 0 or tolerance <= 0:
        raise ValueError("Threshold and tolerance must be positive")
    if wide_threshold < final_threshold:
        raise ValueError("The wide inlier threshold must not be under the final one")
    if validation_samples < 20 or exact_iterations < 1 or coarse_keep < 1:
        raise ValueError("Invalid registration validation limits")
    if start_threshold is None:
        start_threshold = float(np.linalg.norm(supported_scene.extents) / 4.0)
    start_threshold = max(start_threshold, final_threshold)

    cloud = _surface_cloud(supported_scene, rng)
    candidates = coarse_alignment(
        model, supported_scene, rng, keep=coarse_keep, rotations=rotations, cloud=cloud
    )
    coarse = ICPQuery.from_samples(*cloud)
    exact = ExactQuery(supported_scene)
    src = _sample_surface(model, sample_count, rng)
    exact_src = src[:EXACT_SAMPLES]
    validation = _sample_surface(model, validation_samples, rng)

    coarse_end = max(COARSE_END_FACTOR * final_threshold, final_threshold)
    cloud_thresholds = np.geomspace(start_threshold, coarse_end, CLOUD_SCALES)
    exact_thresholds = np.geomspace(coarse_end, final_threshold, EXACT_SCALES + 1)[1:]
    per_cloud = max(1, iterations // CLOUD_SCALES)
    per_exact = max(1, exact_iterations // EXACT_SCALES)

    results = []
    for initial in candidates[:starts]:
        transform = initial.copy()
        converged = False
        for threshold in cloud_thresholds:
            transform, _ = _icp_stage(
                transform, src, coarse, float(threshold), per_cloud, trim_fraction, tolerance
            )
        for position, threshold in enumerate(exact_thresholds):
            transform, converged = _icp_stage(
                transform, exact_src, exact, float(threshold), per_exact, trim_fraction, tolerance
            )
            if position < len(exact_thresholds) - 1:
                converged = False
        moved = validation @ transform[:3, :3].T + transform[:3, 3]
        _, distances, _ = exact.query(moved)
        stats = _residuals(distances, final_threshold, wide_threshold)
        results.append(
            RegistrationResult(
                transform=transform,
                **stats,
                wide_threshold=wide_threshold,
                iterations=per_cloud * CLOUD_SCALES + per_exact * EXACT_SCALES,
                converged=converged,
                coarse_score=_pose_distance(coarse, src, transform),
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


def _residuals(
    distances: np.ndarray, threshold: float, wide_threshold: float | None = None
) -> dict[str, float]:
    keep = distances <= threshold
    stats = {
        "rms": float(np.sqrt(np.mean(distances[keep] ** 2))) if keep.any() else np.inf,
        "p95": float(np.percentile(distances[keep], 95)) if keep.any() else np.inf,
        "inlier_fraction": float(keep.mean()),
    }
    if wide_threshold is not None:
        stats["inlier_fraction_wide"] = float(np.mean(distances <= wide_threshold))
    return stats


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
    wide_threshold: float = WIDE_THRESHOLD_MM,
) -> dict[str, float]:
    rng = rng if rng is not None else np.random.default_rng(7)
    points = _sample_surface(model, samples, rng)
    moved = trimesh.transform_points(points, transform)
    exact_query = MeshDistanceQuery(supported_scene)
    _, distances, _ = exact_query.query(moved)
    return _residuals(distances, threshold, wide_threshold)