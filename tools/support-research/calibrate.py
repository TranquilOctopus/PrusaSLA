"""Measure the expert supports of the research dataset against our generator (M7.4c).

Reads the gitignored calibration manifest of the research dataset, splits every
supported file into its shells, registers every pair of a plain model and the model
shell of the same model with the expert's supports, and records per pair only numbers:

  * the model size in the expert print orientation,
  * the registration residual (RMS, p95, inlier fraction),
  * how far the modelling-up axis (+Z of the plain STL) had to be tipped over to
    reach the print orientation, and which way it points,
  * whether the model's largest flat area faces the plate,
  * the expert's support tips, structures, bases and tip diameters, counted off the
    support shells (M7.3e).

It also writes the plain model rotated into the expert print orientation, so the
generator can be measured in the same orientation (tests/sla_print/
sla_calibration_support_tests.cpp does that, reading SLA_CALIB_MANIFEST).

Everything this script writes lives under `local-samples/supports/out/`, which is
gitignored: `calibration.json` per pair, `oriented/<id>.stl` per pair (removed by
--clean). calibration_report.py turns the JSON into aggregate tables by category;
those tables, and only those, are ever committed.

Research rules (ROADMAP M7): the meshes stay where they are, no mesh is ever
opened by an agent, no file name or path is printed or put into the JSON, and the
console gets counts only.

The measurement functions of this module - the manifest reader, the tip counting, the
up-axis tilt and the largest flat area - need numpy only, so test_m74c.py covers them
without trimesh and scipy. Everything that reads a mesh, splits a scene or registers a
pair imports them inside the function that needs them.

Usage:
    python tools/support-research/calibrate.py [--ids cal001,cal002] [--category head]
"""

from __future__ import annotations

import argparse
import json
import math
import shutil
import sys
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

# The dataset and its outputs, relative to the repository root.
REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MANIFEST = REPO_ROOT / "local-samples" / "supports" / "manifest_calib.yaml"
DEFAULT_OUT = REPO_ROOT / "local-samples" / "supports" / "out"

# Everything measured, every pair, no failures.
EXIT_OK = 0
# The run was made, but at least one pair could not be measured.
EXIT_FAILED_PAIRS = 1
# The run could not be made at all: no manifest, no usable pair.
EXIT_CANNOT_RUN = 2

# Distance from the model surface within which a support vertex counts as a tip, and
# the radius within which two of them are the same tip. Both are generous on
# purpose: registration is off by 0.5-0.9 mm (the known M7 issue), so a tight
# threshold would count a tree's branches as fewer tips than it has.
TIP_GAP_MM = 1.5
CLUSTER_RADIUS_MM = 1.5

# Faces of the supported mesh farther than this from the transformed model are
# support, the epsilon of M7.3b. The shell split of M7.3e replaced it: a supported file
# already says which faces are support by holding them in shells of their own.
SEPARATION_EPSILON_MM = 0.3

# Vertices are welded within this distance before components are counted. Far below
# any mesh resolution, far above the rounding of a written STL.
WELD_TOLERANCE_MM = 1e-4

# A support structure this flat and this wide is a raft or a base pad, not a tree:
# it reaches the plate without carrying the model.
SLAB_HEIGHT_MM = 2.0

# How far above the ground a structure still counts as standing on the plate.
PLATE_GAP_MM = 0.05

# The shell split of a supported file (M7.3e). The weld tolerance is how close two
# vertices have to be to be one corner: 1 um is a fiftieth of the resolution the meshes
# are exported at and a hundred times the rounding of a written STL, so a body's own
# corners always merge and two bodies never do. The gap and the cluster radius are how
# near a support shell has to come to the model shell, and how near two of its touching
# vertices have to be, to be one contact tip. Both are tight on purpose, unlike
# TIP_GAP_MM above: the two shells are in one file and one frame, so there is no
# registration error left to cover. The classes are the tip diameters a printer offers,
# so a measured tip is counted as the size that was picked.
SHELL_WELD_TOLERANCE_MM = 1e-3
SHELL_CONTACT_GAP_MM = 0.5
SHELL_CLUSTER_RADIUS_MM = 0.5
SHELL_DIAMETER_CLASSES_MM = (0.1, 0.2, 0.3, 0.4, 0.6)

# Up-axis tilts below this count as "printed the way it was modelled" (face up), and
# the band above it as "tipped over on purpose". Reported to the caller as shares.
FACE_UP_TILT_DEG = 45.0
TIPPED_BAND_DEG = (30.0, 60.0)


class ManifestError(ValueError):
    """The calibration manifest cannot be used. The message names ids, never paths."""


# --------------------------------------------------------------------------------
# The manifest
# --------------------------------------------------------------------------------


def _strip_comment(line: str) -> str:
    """Drop a YAML comment: a '#' outside quotes that starts a word."""
    quote = ""
    for index, char in enumerate(line):
        if quote:
            if char == quote:
                quote = ""
        elif char in "'\"":
            quote = char
        elif char == "#" and (index == 0 or line[index - 1].isspace()):
            return line[:index]
    return line


def _scalar(text: str) -> str:
    """One scalar value: trimmed, with one layer of matching quotes taken off."""
    value = text.strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in "'\"":
        value = value[1:-1]
    return value.strip()


def parse_pairs(text: str) -> list[dict[str, str]]:
    """The `pairs:` list of the calibration manifest, as a list of dictionaries.

    A hand-rolled reader for one shape: a top level `pairs:` key, then one list item
    per pair, then one `key: value` per line. It is small because the manifest is
    fixed and small, and small because pyyaml is not one of the dependencies of this
    folder. Keys the reader does not know (source, part, license, notes) are kept as
    they are, so nothing of the manifest is lost, and only the `pairs:` section is
    read: any other top level key and its block are skipped.
    """
    pairs: list[dict[str, str]] = []
    current: dict[str, str] | None = None
    in_pairs = False

    for raw in text.splitlines():
        line = _strip_comment(raw)
        if not line.strip():
            continue

        body = line.strip()
        if not line[0].isspace():
            # A top level key: the only one this reader follows is `pairs:`.
            in_pairs = body.endswith(":") and body[:-1].strip() == "pairs"
            current = None
            continue

        if not in_pairs:
            continue

        if body.startswith("-"):
            current = {}
            pairs.append(current)
            body = body[1:].strip()

        if current is None:
            continue

        key, separator, value = body.partition(":")
        if separator:
            current[key.strip()] = _scalar(value)

    return pairs


def validate_pairs(pairs: list[dict[str, str]]) -> list[dict[str, str]]:
    """Every pair has to name itself and hold the plain model, or it cannot be run."""
    for pair in pairs:
        pair_id = pair.get("id", "").strip()
        if not pair_id:
            raise ManifestError("a pair of the manifest has no id")
        if not pair.get("unsupported_stl", "").strip():
            raise ManifestError(f"pair {pair_id} has no unsupported_stl")
        if not pair.get("supported_stl", "").strip():
            raise ManifestError(f"pair {pair_id} has no supported_stl")
    return pairs


def select_pairs(
    pairs: list[dict[str, str]], ids: str | None = None, category: str | None = None
) -> list[dict[str, str]]:
    """The pairs --ids (comma separated) and --category select; both may be unset."""
    wanted_ids = (
        {item.strip().lower() for item in ids.split(",") if item.strip()} if ids else None
    )
    wanted_category = category.strip().lower() if category else None

    selected = []
    for pair in pairs:
        if wanted_ids is not None and pair.get("id", "").strip().lower() not in wanted_ids:
            continue
        if wanted_category is not None:
            if pair.get("category", "").strip().lower() != wanted_category:
                continue
        selected.append(pair)
    return selected


def resolve_path(folder: Path, path: str) -> Path:
    """A manifest path, absolute as written or relative to the manifest's folder."""
    candidate = Path(_scalar(path))
    return candidate if candidate.is_absolute() else folder / candidate


# --------------------------------------------------------------------------------
# Measurements that need numpy only
# --------------------------------------------------------------------------------


def up_axis(transform: np.ndarray) -> tuple[float, np.ndarray]:
    """The tilt of the modelling-up axis, and that axis in the print frame.

    The plain file's +Z is the axis the modeller called "up". The recovered rotation
    carries it into the print frame of the supported mesh, so the angle between it and
    print-up (+Z of the print frame) is how far the model was tipped over to be
    printed, and the direction says which way it was tipped. Same number as
    register.print_tilt(), with the direction it does not return.
    """
    matrix = np.asarray(transform, dtype=np.float64)
    rotation = matrix[:3, :3] if matrix.shape == (4, 4) else matrix
    if rotation.shape != (3, 3):
        raise ValueError("A 3 by 3 rotation or a 4 by 4 transform is expected")

    axis = rotation @ np.array([0.0, 0.0, 1.0])
    norm = float(np.linalg.norm(axis))
    if norm == 0.0:
        raise ValueError("The rotation maps the up axis onto nothing")
    axis = axis / norm

    tilt = float(np.degrees(np.arccos(float(np.clip(axis[2], -1.0, 1.0)))))
    return tilt, axis


def weld(vertices: np.ndarray, tolerance: float = WELD_TOLERANCE_MM) -> np.ndarray:
    """Map every vertex onto the index of the first vertex at its position.

    An STL is a triangle soup: each face stores its own three vertices, so the same
    corner is stored once per face that uses it and face adjacency cannot be taken
    from the vertex indices. Rounding the positions onto a grid coarser than the mesh
    resolution and far finer than its tolerance (1e-4 mm is 0.1 um) merges them.
    """
    if tolerance <= 0:
        raise ValueError("Weld tolerance must be positive")

    points = np.asarray(vertices, dtype=np.float64)
    if points.ndim != 2 or points.shape[1] != 3:
        raise ValueError("Vertices must be an n by 3 array")

    keys = np.round(points / tolerance).astype(np.int64)
    _, first = np.unique(keys, axis=0, return_inverse=True)
    return np.asarray(first, dtype=np.int64).ravel()


def _scatter_min(keys: np.ndarray, values: np.ndarray, size: int) -> np.ndarray:
    """The smallest `values` per key, as a new array of `size` entries."""
    out = np.full(size, np.iinfo(np.int64).max, dtype=np.int64)
    if not len(keys):
        return out

    order = np.argsort(keys, kind="stable")
    sorted_keys = keys[order]
    starts = np.empty(len(sorted_keys), dtype=bool)
    starts[0] = True
    starts[1:] = sorted_keys[1:] != sorted_keys[:-1]
    first = np.flatnonzero(starts)
    out[sorted_keys[first]] = np.minimum.reduceat(values[order], first)
    return out


def connected_components(count: int, edges_a: np.ndarray, edges_b: np.ndarray) -> np.ndarray:
    """Label the nodes of an undirected graph, one label per connected part.

    Hooking and pointer jumping: every round a node adopts the smallest label of its
    neighbourhood and then follows the labels it points at until they stop shrinking.
    Labels only ever fall, so it terminates, and it converges in a handful of rounds
    even on a long chain. Every round is numpy, so this is testable without trimesh.

    Returns labels 0..parts-1, in the order the parts are first met.
    """
    if count < 0:
        raise ValueError("The node count must not be negative")

    ends_a = np.asarray(edges_a, dtype=np.int64).ravel()
    ends_b = np.asarray(edges_b, dtype=np.int64).ravel()
    if ends_a.shape != ends_b.shape:
        raise ValueError("Both ends of an edge need one entry per edge")

    labels = np.arange(count, dtype=np.int64)
    if count == 0 or not len(ends_a):
        return labels
    lowest = min(int(ends_a.min()), int(ends_b.min()))
    highest = max(int(ends_a.max()), int(ends_b.max()))
    if lowest < 0 or highest >= count:
        raise ValueError("Edge ends have to be node indices")

    while True:
        hooked = np.minimum(
            np.minimum(
                _scatter_min(ends_a, labels[ends_b], count),
                _scatter_min(ends_b, labels[ends_a], count),
            ),
            labels,
        )
        # Pointer jumping: follow the labels until every node is its own parent.
        while True:
            jumped = hooked[hooked]
            if np.array_equal(jumped, hooked):
                break
            hooked = jumped

        if np.array_equal(hooked, labels):
            break
        labels = hooked

    _, dense = np.unique(labels, return_inverse=True)
    return np.asarray(dense, dtype=np.int64).ravel()


def vertex_labels(vertices: np.ndarray, faces: np.ndarray) -> np.ndarray:
    """The connected component of every vertex of a triangle soup.

    Weld first: an STL stores each corner once per face, so two faces of one wall
    share a position but not a vertex index. Returns one label per vertex of the
    input, the duplicated ones included, since they weld onto the same label.
    """
    welded = weld(vertices)
    corners = welded[np.asarray(faces, dtype=np.int64).reshape(-1, 3)]
    labels = connected_components(
        int(welded.max()) + 1,
        corners[:, [0, 1, 2]].ravel(),
        corners[:, [1, 2, 0]].ravel(),
    )
    return labels[welded]


def cluster_groups(points: np.ndarray, radius: float) -> list[np.ndarray]:
    """The clusters the points form, two of them one when within `radius`.

    A uniform grid of cells of the radius, so only the 27 cells around a point are
    looked at. Plain dicts and a union-find, because the near-surface vertices of a
    support mesh are a handful of tens of thousands, not millions.

    Every cluster comes back with its own points, so a caller that counts them and a
    caller that also measures one are the same walk over the same data.
    """
    if radius <= 0:
        raise ValueError("The cluster radius must be positive")

    spots = np.asarray(points, dtype=np.float64)
    if spots.ndim != 2 or spots.shape[1] != 3:
        raise ValueError("Points must be an n by 3 array")
    if not len(spots):
        return []

    cells = np.floor(spots / radius).astype(np.int64)
    buckets: dict[tuple[int, int, int], list[int]] = {}
    for index, cell in enumerate(map(tuple, cells)):
        buckets.setdefault(cell, []).append(index)

    parent = list(range(len(spots)))

    def find(node: int) -> int:
        while parent[node] != node:
            parent[node] = parent[parent[node]]
            node = parent[node]
        return node

    for index, (cx, cy, cz) in enumerate(map(tuple, cells)):
        nearby: list[int] = []
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    nearby.extend(buckets.get((cx + dx, cy + dy, cz + dz), ()))
        if not nearby:
            continue

        # The 27 cells around a point hold points up to two radii away, so the exact
        # distance decides, not the cell.
        candidates = np.asarray(nearby, dtype=np.int64)
        offsets = spots[candidates] - spots[index]
        within = candidates[(offsets * offsets).sum(axis=1) <= radius * radius]
        for other in within:
            # Every unordered pair once: the smaller index is skipped.
            if other <= index:
                continue
            left, right = find(index), find(int(other))
            if left != right:
                parent[max(left, right)] = min(left, right)

    groups: dict[int, list[int]] = {}
    for node in range(len(spots)):
        groups.setdefault(find(node), []).append(node)
    return [spots[np.asarray(members, dtype=np.int64)] for members in groups.values()]


def cluster_count(points: np.ndarray, radius: float) -> int:
    """How many clusters the points form, two of them one when within `radius`."""
    return len(cluster_groups(points, radius))


@dataclass
class SupportCounts:
    """What the expert put under one model."""

    tips: int = 0
    structures: int = 0
    structures_on_plate: int = 0
    base_structures: int = 0


def count_support_tips(
    vertices: np.ndarray,
    vertex_component: np.ndarray,
    distance_to_model: np.ndarray,
    surface_gap: float = TIP_GAP_MM,
    cluster_radius: float = CLUSTER_RADIUS_MM,
    plate_z: float = 0.0,
    plate_gap: float = PLATE_GAP_MM,
    slab_height: float = SLAB_HEIGHT_MM,
) -> SupportCounts:
    """Count the tips, the structures and the bases of a support mesh.

    This is the method of M7.4c, on support faces found by distance from the registered
    model. calibrate.py no longer calls it: the shell split of M7.3e counts its contacts
    without a registration error to cover, so the generous `surface_gap` of the old
    method and the components of a distance mask are not needed. It stays for the tests
    that cover it.

    A tip is a cluster of support vertices that come within `surface_gap` of the
    model surface, so a branching tree with three tips under the model counts three,
    and a tree whose trunk runs past the model without touching it adds nothing.
    Clusters are built per structure, so two trees touching the model within
    `cluster_radius` of each other still count twice.

    A structure that reaches the plate carries the model; one that reaches it while
    staying flat and wide is a raft or a base pad and is counted as a base instead.
    Its top face is within a millimetre of a model that sits on it, so counting it
    as a tip would put one support point on the plate under every model. A structure
    that stands on a raft instead of on the plate still counts as carrying the model.

    Args:
        vertices: support mesh vertices, in the print frame.
        vertex_component: the connected component of every vertex.
        distance_to_model: distance of every vertex to the transformed model surface.
        surface_gap: how close a vertex has to come to the model to be a tip.
        cluster_radius: two touching vertices further apart than this are two tips.
        plate_z: the height of the plate, the lowest point of the whole scene.
        plate_gap: how far above the ground still counts as standing on it.
        slab_height: a structure not taller than this may be a raft.
    """
    spots = np.asarray(vertices, dtype=np.float64)
    component = np.asarray(vertex_component, dtype=np.int64).ravel()
    distance = np.asarray(distance_to_model, dtype=np.float64).ravel()
    if spots.ndim != 2 or spots.shape[1] != 3:
        raise ValueError("Vertices must be an n by 3 array")
    if component.shape != (len(spots),) or distance.shape != (len(spots),):
        raise ValueError("One component and one distance per support vertex is expected")
    if surface_gap <= 0 or cluster_radius <= 0 or slab_height <= 0:
        raise ValueError("The gap, the cluster radius and the slab height must be positive")

    if not len(spots):
        return SupportCounts()

    structures = int(component.max()) + 1
    low = np.full((structures, 3), np.inf)
    high = np.full((structures, 3), -np.inf)
    np.minimum.at(low, component, spots)
    np.maximum.at(high, component, spots)

    height = high[:, 2] - low[:, 2]
    span = np.maximum(high[:, 0] - low[:, 0], high[:, 1] - low[:, 1])
    is_slab = (height <= slab_height) & (span >= 2.0 * height)

    # What a structure stands on: the plate, or the top of a raft if there is one.
    slabs = np.flatnonzero(is_slab)
    ground = float(high[slabs, 2].max()) if len(slabs) else plate_z
    ground = max(ground, plate_z)

    counts = SupportCounts()
    counts.structures = structures
    counts.base_structures = int(is_slab.sum())
    counts.structures_on_plate = int(((low[:, 2] <= ground + plate_gap) & ~is_slab).sum())

    near = np.flatnonzero(distance <= surface_gap)
    near_component = component[near]
    tips = 0
    for index in np.unique(near_component):
        touching = spots[near[near_component == index]]
        tips += cluster_count(touching, cluster_radius)
    counts.tips = tips
    return counts


@dataclass
class FlatArea:
    """The largest flat region of a model, as a measure of how it sits on the plate."""

    area_mm2: float = 0.0
    fraction: float = 0.0
    normal: list[float] = field(default_factory=lambda: [0.0, 0.0, 1.0])
    faces: int = 0
    faces_plate: bool = False


def largest_flat_area(
    vertices: np.ndarray,
    faces: np.ndarray,
    plate_normal: tuple[float, float, float] = (0.0, 0.0, -1.0),
    direction_bins: float = 4.0,
    merge_deg: float = 20.0,
    plate_deg: float = 30.0,
    min_fraction: float = 0.005,
    max_groups: int = 8,
) -> FlatArea:
    """The largest connected planar patch of a mesh, and whether it faces the plate.

    A miniature carries no single flat face, and a remeshed model carries thousands
    of small ones, so the patch is found the way a person reads it: group the faces
    by the direction they face, weld the vertices, and take the largest connected
    patch of one group. Groups whose mean direction is within `merge_deg` of each
    other are read as one, so a planar patch that straddles a direction bin is not
    cut in half. `faces_plate` is then the answer to the question the calibration
    asks: does the model's largest flat area look at the plate (a base, a rock, a
    ground piece) or at the sky (a bust cut off at the shoulders, a head)?

    Args:
        vertices: mesh vertices, in the frame the patch is measured in.
        faces: mesh triangles.
        plate_normal: the direction a patch has to face to count as on the plate.
        direction_bins: how many steps a normal is binned into per axis.
        merge_deg: groups closer than this in direction are read as one.
        plate_deg: how far off the plate direction still counts as facing the plate.
        min_fraction: a group smaller than this share of the area is not a base.
        max_groups: how many direction groups are examined at most.
    """
    if direction_bins <= 0 or merge_deg <= 0 or plate_deg <= 0 or max_groups < 1 or min_fraction < 0:
        raise ValueError("The bin count, the angles and the group limit must be positive")

    corners = np.asarray(faces, dtype=np.int64).reshape(-1, 3)
    triangles = np.asarray(vertices, dtype=np.float64)[corners]
    if not len(triangles):
        return FlatArea()

    normals = np.cross(triangles[:, 1] - triangles[:, 0], triangles[:, 2] - triangles[:, 0])
    lengths = np.linalg.norm(normals, axis=1)
    total_area = float(0.5 * lengths.sum())
    if total_area <= 0:
        return FlatArea()

    usable = lengths > 0
    unit = normals[usable] / lengths[usable][:, None]
    areas = 0.5 * lengths[usable]
    triangles = triangles[usable]

    keys = np.round(unit * direction_bins).astype(np.int64)
    _, group_of_face = np.unique(keys, axis=0, return_inverse=True)
    group_of_face = np.asarray(group_of_face, dtype=np.int64).ravel()
    groups = int(group_of_face.max()) + 1
    group_area = np.bincount(group_of_face, weights=areas, minlength=groups)

    # Only the groups that could be a base: the biggest ones, above the floor.
    ranked = np.argsort(-group_area)
    floor = min_fraction * total_area
    candidates = [int(index) for index in ranked[:max_groups] if group_area[index] >= floor]
    if not candidates:
        candidates = [int(ranked[0])]

    group_normal = np.zeros((groups, 3))
    for index in candidates:
        mean = unit[group_of_face == index].mean(axis=0)
        norm = float(np.linalg.norm(mean))
        if norm > 0:
            group_normal[index] = mean / norm

    # Merge the candidates that face the same way, transitively.
    merged: list[list[int]] = []
    for index in candidates:
        for group in merged:
            if angle_between(group_normal[group[0]], group_normal[index]) <= merge_deg:
                group.append(index)
                group_normal[group[0]] = normalize(group_normal[group].sum(axis=0))
                break
        else:
            merged.append([index])

    # Each face is listed as its own little triangle over its three corners, so its
    # corners form one connected part and two faces sharing a corner join: the labels
    # are per patch.
    face_count = len(triangles)
    corner_labels = vertex_labels(
        triangles.reshape(-1, 3), np.arange(3 * face_count, dtype=np.int64).reshape(face_count, 3)
    )
    labels = corner_labels.reshape(face_count, 3)[:, 0]

    plate = np.asarray(plate_normal, dtype=np.float64)
    plate = plate / float(np.linalg.norm(plate))
    cos_plate = math.cos(math.radians(plate_deg))

    # The largest patch of every candidate group; between patches of the same size the
    # one looking at the plate wins, because a model with a flat area on the plate is
    # the case the calibration is after.
    patches: list[FlatArea] = []
    for group in merged:
        in_group = np.isin(group_of_face, group)
        labels_in_group = labels[in_group]
        if not len(labels_in_group):
            continue
        parts = int(labels_in_group.max()) + 1
        part_area = np.bincount(labels_in_group, weights=areas[in_group], minlength=parts)
        for part in np.flatnonzero(part_area == part_area.max()):
            selected = in_group & (labels == part)
            if not selected.any():
                continue
            normal = normalize(unit[selected].mean(axis=0))
            area = float(areas[selected].sum())
            patches.append(
                FlatArea(
                    area_mm2=area,
                    fraction=area / total_area,
                    normal=[float(value) for value in normal],
                    faces=int(selected.sum()),
                    faces_plate=bool(float(normal @ plate) >= cos_plate),
                )
            )

    if not patches:
        return FlatArea()
    biggest = max(patch.area_mm2 for patch in patches)
    on_plate = [patch for patch in patches if patch.area_mm2 == biggest and patch.faces_plate]
    return (on_plate or [patch for patch in patches if patch.area_mm2 == biggest])[0]


def normalize(vector: np.ndarray) -> np.ndarray:
    """A unit vector; a zero vector is left alone instead of becoming NaN."""
    array = np.asarray(vector, dtype=np.float64)
    norm = float(np.linalg.norm(array))
    return array if norm == 0.0 else array / norm


def angle_between(first: np.ndarray, second: np.ndarray) -> float:
    """The angle of two directions in degrees, 180 when either is a zero vector."""
    left, right = normalize(first), normalize(second)
    if not np.any(left) or not np.any(right):
        return 180.0
    return float(np.degrees(np.arccos(float(np.clip(float(left @ right), -1.0, 1.0)))))


# --------------------------------------------------------------------------------
# The pipeline (trimesh and scipy are imported where they are needed)
# --------------------------------------------------------------------------------


def load_mesh(path: Path):
    """A triangle mesh from disk, as stored: no welding, no repair, no scaling."""
    import trimesh

    loader = getattr(trimesh, "load_mesh", None) or trimesh.load
    mesh = loader(str(path), process=False)
    if not isinstance(mesh, trimesh.Trimesh):
        parts = [
            geometry
            for geometry in getattr(mesh, "geometry", {}).values()
            if isinstance(geometry, trimesh.Trimesh)
        ]
        if not parts:
            raise ValueError("the file holds no triangle mesh")
        mesh = parts[0] if len(parts) == 1 else trimesh.util.concatenate(parts)

    if not len(mesh.faces) or not len(mesh.vertices):
        raise ValueError("the mesh is empty")
    return mesh


def distances_to_model(
    support_vertices: np.ndarray,
    model,
    model_to_scene: np.ndarray,
    gap: float,
    rng: np.random.Generator,
    spacing: float = 0.15,
    max_samples: int = 2_000_000,
) -> np.ndarray:
    """The distance of every support vertex to the transformed model surface.

    The same two steps as separate_supports_scaled(), applied to vertices: a KD-tree
    over dense samples of the model surface answers almost every vertex, because the
    nearest sample can only be further than the surface, and only the band the gap
    leaves open is measured exactly. `trimesh.proximity.closest_point` on every vertex
    of a big support mesh would take minutes per pair.
    """
    import trimesh
    from scipy.spatial import cKDTree

    from separate import _sample_surface_dense, _transform_centroids_to_model_frame

    area = float(np.asarray(model.area_faces, dtype=np.float64).sum())
    if area <= 0:
        raise ValueError("the model has no surface area")
    count = int(min(max(int(math.ceil(area / (spacing * spacing))), 1_000), max_samples))
    samples = trimesh.transform_points(
        _sample_surface_dense(model, count, rng), model_to_scene
    )
    tree = cKDTree(samples)

    approx, _ = tree.query(np.asarray(support_vertices, dtype=np.float64), k=1, workers=1)
    probe = samples if len(samples) <= 20_000 else samples[rng.choice(len(samples), 20_000, replace=False)]
    neighbour, _ = tree.query(probe, k=2, workers=1)
    safety = max(float(neighbour[:, 1].mean()) * 4.0, spacing)

    distances = np.asarray(approx, dtype=np.float64).copy()
    band = np.flatnonzero(distances <= gap + safety)
    if len(band):
        in_model_frame = _transform_centroids_to_model_frame(
            np.asarray(support_vertices, dtype=np.float64)[band], model_to_scene
        )
        _, exact, _ = trimesh.proximity.closest_point(model, in_model_frame)
        if not np.isfinite(exact).all():
            raise ValueError("the surface distance query returned a nonfinite distance")
        distances[band] = exact
    return distances


def measure_pair(
    pair: dict[str, str],
    folder: Path,
    out_dir: Path,
    rng: np.random.Generator,
    final_threshold: float = 0.3,
    write_oriented: bool = True,
) -> dict:
    """Every number one pair contributes to the calibration.

    The supported file is split into its shells first (M7.3e), the plain model is
    registered against the model shell alone, and the supports are read off the other
    shells. Registering against the whole scene is what M7.4c did, and it failed: the
    supports dominate the scene, so the model was 2-42% of the inliers, and the
    registration was 0.5-0.9 mm off, which is why the contacts had to be counted with a
    1.5 mm gap. Splitting first makes the registration model against model and the
    contacts exact, so both are now measured instead of guessed.
    """
    import trimesh

    from register import register
    from shells import measure_shells, split_supported_scene

    pair_id = pair["id"].strip()
    model = load_mesh(resolve_path(folder, pair["unsupported_stl"]))
    scene = load_mesh(resolve_path(folder, pair["supported_stl"]))

    model_shell, support_shells = split_supported_scene(scene)
    registration = register(model, model_shell, rng, final_threshold=final_threshold)
    transform = np.asarray(registration.transform, dtype=np.float64)

    tilt, axis = up_axis(transform)
    moved = trimesh.transform_points(np.asarray(model.vertices, dtype=np.float64), transform)
    size = (moved.max(axis=0) - moved.min(axis=0)).tolist()
    flat = largest_flat_area(moved, np.asarray(model.faces, dtype=np.int64))

    plate_z = float(np.asarray(scene.vertices, dtype=np.float64)[:, 2].min())
    shells = measure_shells(model_shell, support_shells, rng, plate_z=plate_z)

    oriented = None
    if write_oriented:
        target = out_dir / "oriented" / f"{pair_id}.stl"
        target.parent.mkdir(parents=True, exist_ok=True)
        placed = model.copy()
        placed.apply_transform(transform)
        placed.export(str(target))
        oriented = f"oriented/{pair_id}.stl"

    return {
        "id": pair_id,
        "category": pair.get("category", "").strip().lower() or "uncategorized",
        "model_size_mm": [float(value) for value in size],
        "model_height_mm": float(size[2]),
        "registration_rms_mm": float(registration.rms),
        "registration_p95_mm": float(registration.p95),
        "registration_inlier_fraction": float(registration.inlier_fraction),
        "up_axis_tilt_deg": tilt,
        "up_axis_direction": [float(value) for value in axis],
        "up_axis_face_up": bool(tilt < FACE_UP_TILT_DEG),
        "up_axis_tipped": bool(TIPPED_BAND_DEG[0] <= tilt <= TIPPED_BAND_DEG[1]),
        "flat_area_fraction": float(flat.fraction),
        "flat_area_mm2": float(flat.area_mm2),
        "flat_area_faces_plate": bool(flat.faces_plate),
        # The counts of M7.4c, now read off the shells. calibration_report.py reads
        # these keys and needs no change.
        "expert_support_count": int(shells.tips),
        "support_structures": int(shells.structures),
        "support_structures_on_plate": int(shells.structures_on_plate),
        "support_base_structures": int(shells.rafts),
        "support_faces": int(shells.support_faces),
        "supports_separate": bool(support_shells),
        **shells.as_record(),
        "oriented_stl": oriented,
    }


def summarise(records: list[dict]) -> dict[str, object]:
    """The counts the summary line is made of."""
    classes: dict[str, int] = {}
    for record in records:
        for size, count in record.get("shell_tips_by_diameter_mm", {}).items():
            classes[size] = classes.get(size, 0) + int(count)
    return {
        "tips": sum(record["expert_support_count"] for record in records),
        "structures": sum(record["support_structures"] for record in records),
        "structures_on_plate": sum(record["support_structures_on_plate"] for record in records),
        "bases": sum(record["support_base_structures"] for record in records),
        # How many of the pairs were split into shells at all: a file whose supports are
        # welded to the model measures zero tips, and the summary has to say so.
        "separate": sum(1 for record in records if record.get("supports_separate")),
        "oriented": sum(1 for record in records if record["oriented_stl"]),
        "tip_classes": ",".join(
            f"{size}x{classes[size]}" for size in sorted(classes, key=float)
        ) or "-",
    }


def clean(out_dir: Path) -> int:
    """Remove the oriented copies this script wrote; how many files went."""
    oriented = out_dir / "oriented"
    if not oriented.is_dir():
        return 0
    removed = sum(1 for path in oriented.rglob("*") if path.is_file())
    shutil.rmtree(oriented)
    return removed


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Measure the expert supports of the M7 dataset.")
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST,
                        help="the calibration manifest (gitignored)")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT,
                        help="where calibration.json and the oriented STLs go (gitignored)")
    parser.add_argument("--ids", default=None, help="comma separated manifest ids to measure")
    parser.add_argument("--category", default=None, help="one category to measure")
    parser.add_argument("--threshold", type=float, default=0.3,
                        help="inlier distance registration converges to (mm)")
    parser.add_argument("--seed", type=int, default=7, help="seed of the surface sampling")
    parser.add_argument("--no-oriented", action="store_true",
                        help="measure only, write no oriented STL")
    parser.add_argument("--clean", action="store_true",
                        help="remove the oriented STLs and stop")
    args = parser.parse_args(argv)

    if args.clean:
        removed = clean(args.out)
        print(f"calibrate: cleaned={removed}")
        return EXIT_OK

    if not args.manifest.is_file():
        print("calibrate: the calibration manifest is missing (see M7.4c)", file=sys.stderr)
        return EXIT_CANNOT_RUN

    try:
        pairs = validate_pairs(select_pairs(
            parse_pairs(args.manifest.read_text(encoding="utf-8")), args.ids, args.category
        ))
    except ManifestError as error:
        print(f"calibrate: {error}", file=sys.stderr)
        return EXIT_CANNOT_RUN

    if not pairs:
        print("calibrate: no pair selected", file=sys.stderr)
        return EXIT_CANNOT_RUN

    folder = args.manifest.parent
    rng = np.random.default_rng(args.seed)
    records: list[dict] = []
    failed = 0

    for pair in pairs:
        pair_id = pair["id"].strip()
        category = pair.get("category", "").strip().lower() or "uncategorized"
        try:
            record = measure_pair(
                pair, folder, args.out, rng,
                final_threshold=args.threshold,
                write_oriented=not args.no_oriented,
            )
        except Exception as error:
            # A loader quotes the file it failed on, so only the kind of the failure
            # and the id go to the console; one bad pair must not stop the run.
            failed += 1
            print(f"calibrate: {pair_id} [{category}] failed: {type(error).__name__}")
            continue

        records.append(record)
        print(f"calibrate: {record['id']} [{record['category']}] measured")

    totals = summarise(records)
    args.out.mkdir(parents=True, exist_ok=True)
    payload = {
        "schema": 2,
        "tip_gap_mm": TIP_GAP_MM,
        "cluster_radius_mm": CLUSTER_RADIUS_MM,
        "shell_weld_tolerance_mm": SHELL_WELD_TOLERANCE_MM,
        "shell_contact_gap_mm": SHELL_CONTACT_GAP_MM,
        "shell_cluster_radius_mm": SHELL_CLUSTER_RADIUS_MM,
        "shell_diameter_classes_mm": list(SHELL_DIAMETER_CLASSES_MM),
        "face_up_tilt_deg": FACE_UP_TILT_DEG,
        "tipped_band_deg": list(TIPPED_BAND_DEG),
        "pairs": records,
        "summary": {"selected": len(pairs), "measured": len(records), "failed": failed, **totals},
    }
    (args.out / "calibration.json").write_text(json.dumps(payload, indent=2), encoding="utf-8")

    print(
        "calibrate: pairs={pairs}, measured={measured}, failed={failed}, split={separate}, "
        "tips={tips}, structures={structures}, structures_on_plate={structures_on_plate}, "
        "bases={bases}, tip_classes={tip_classes}, oriented={oriented}".format(
            pairs=len(pairs), measured=len(records), failed=failed, **totals
        )
    )
    return EXIT_FAILED_PAIRS if failed else EXIT_OK


if __name__ == "__main__":
    raise SystemExit(main())
