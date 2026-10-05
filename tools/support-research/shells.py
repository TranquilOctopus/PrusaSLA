"""The shell structure of a supported scene, and what it says about the supports (M7.3e).

A slicer writes a supported file as a triangle soup of disjoint bodies: the model, and
every support, tree or raft of its own. Welding the vertices and labelling the faces by
adjacency therefore splits a supported STL into the model and its supports, with no
registration, no epsilon and no resampling - the model shell is simply the largest of
the shells, and every other shell is support. That replaces registering the plain model
against the whole scene (the supports dominate it, so the model was 2-42% of the
inliers) and it replaces the distance test of M7.3b.

Both halves of the measurement are then read off the shells directly, with no
registration error in them: the model shell and the support shells are in one file and
one frame, so the distance from a support shell to the model is exact, which is what a
contact tip is measured against.

Research rules (ROADMAP M7) hold for everything in here: only counts, diameters and
distances are returned, never a position, so nothing that identifies a model can be
committed or printed.
"""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np
import trimesh

from calibrate import (
    PLATE_GAP_MM,
    SHELL_CLUSTER_RADIUS_MM,
    SHELL_CONTACT_GAP_MM,
    SHELL_DIAMETER_CLASSES_MM,
    SHELL_WELD_TOLERANCE_MM,
    SLAB_HEIGHT_MM,
    cluster_groups,
    connected_components,
    distances_to_model,
    weld,
)

# How close a support shell has to come to the model shell to count as touching it, and
# how near two of its touching vertices have to be to be one contact tip. Both come
# from calibrate, where the record they end up in is written. The gap is tight on
# purpose, unlike the 1.5 mm of the distance method of M7.4c: the two shells are in one
# file and one frame, so the only thing the gap has to cover is a support that stops a
# hair short of the surface.
CONTACT_GAP_MM = SHELL_CONTACT_GAP_MM
CLUSTER_RADIUS_MM = SHELL_CLUSTER_RADIUS_MM

# The tip diameters the classes of, in mm, each tip going to the nearest one. These are
# the preset sizes an SLA printer offers, so a measured tip is counted as the size that
# was picked rather than as a number nobody can act on.
DIAMETER_CLASSES_MM = SHELL_DIAMETER_CLASSES_MM


def face_components(
    vertices: np.ndarray, faces: np.ndarray, tolerance: float = SHELL_WELD_TOLERANCE_MM
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """The welded vertices, the faces that use them, and the shell of every face.

    An STL is a triangle soup: every face stores its own three corners, so two faces
    of one wall share a position and not a vertex index, and adjacency has to be
    taken from the positions. Welding first is what makes the shells countable.

    Returns the vertex array with one entry per distinct position, the faces indexed
    into it, and the shell of every face. Shell labels come out in the order the
    shells are first met.
    """
    points = np.asarray(vertices, dtype=np.float64)
    corners = np.asarray(faces, dtype=np.int64)
    if points.ndim != 2 or points.shape[1] != 3:
        raise ValueError("Vertices must be an n by 3 array")
    if corners.ndim != 2 or corners.shape[1] != 3:
        raise ValueError("Faces must be an n by 3 array")
    if not len(corners):
        raise ValueError("A scene with no faces has no shells")
    if corners.min() < 0 or corners.max() >= len(points):
        raise ValueError("A face indexes a vertex that does not exist")

    welded = weld(points, tolerance)
    welded_vertices = np.zeros((int(welded.max()) + 1, 3), dtype=np.float64)
    welded_vertices[welded] = points
    welded_faces = welded[corners]

    # One shell per welded vertex; a face belongs to the shell of any of its three.
    shells = connected_components(
        len(welded_vertices),
        welded_faces[:, [0, 1, 2]].ravel(),
        welded_faces[:, [1, 2, 0]].ravel(),
    )
    return welded_vertices, welded_faces, shells[welded_faces[:, 0]]


def split_supported_scene(
    scene: trimesh.Trimesh, tolerance: float = SHELL_WELD_TOLERANCE_MM
) -> tuple[trimesh.Trimesh, list[trimesh.Trimesh]]:
    """The model shell of a supported scene, and every other shell of it.

    The model is the shell with the most faces, which is how it is told apart without
    a registration: the supports of a miniature are thin and small next to the model,
    and the scene is the union of bodies that do not touch. Ties go to the shell that
    was met first, so the split of one file is always the same split.

    The support shells come back biggest first, each keeping its own faces and only
    the vertices those faces use.
    """
    welded_vertices, welded_faces, labels = face_components(
        np.asarray(scene.vertices, dtype=np.float64),
        np.asarray(scene.faces, dtype=np.int64),
        tolerance,
    )

    order = np.argsort(-np.bincount(labels), kind="stable")
    shells = [_shell(welded_vertices, welded_faces[labels == label]) for label in order]
    return shells[0], shells[1:]


def _shell(vertices: np.ndarray, faces: np.ndarray) -> trimesh.Trimesh:
    """One shell as a mesh of its own, with no vertex that none of its faces uses."""
    mesh = trimesh.Trimesh(vertices=vertices, faces=faces, process=False)
    mesh.remove_unreferenced_vertices()
    return mesh


@dataclass
class ShellContact:
    """What one support shell of the scene says about the model above it."""

    tips: int = 0
    tip_diameters_mm: list[float] = field(default_factory=list)
    height_mm: float = 0.0
    thickness_mm: float = 0.0
    span_mm: float = 0.0
    reaches_plate: bool = False
    raft: bool = False


@dataclass
class SceneSupportStats:
    """Everything the shells of one supported scene add up to."""

    shells: int = 1
    model_faces: int = 0
    model_face_fraction: float = 1.0
    support_faces: int = 0
    tips: int = 0
    tip_diameters_mm: list[float] = field(default_factory=list)
    diameter_classes: dict[str, int] = field(default_factory=dict)
    structures: int = 0
    structures_on_plate: int = 0
    rafts: int = 0
    support_height_max_mm: float = 0.0
    contacts: list[ShellContact] = field(default_factory=list)

    def as_record(self) -> dict:
        """The part of this that goes into one calibration record: counts and numbers."""
        return {
            "scene_shells": int(self.shells),
            "scene_model_faces": int(self.model_faces),
            "scene_model_face_fraction": float(self.model_face_fraction),
            "support_faces": int(self.support_faces),
            "shell_tips": int(self.tips),
            "shell_tips_by_diameter_mm": dict(self.diameter_classes),
            "shell_tip_diameter_median_mm": float(np.median(self.tip_diameters_mm))
            if self.tip_diameters_mm
            else 0.0,
            "shell_structures": int(self.structures),
            "shell_structures_on_plate": int(self.structures_on_plate),
            "shell_rafts": int(self.rafts),
            "shell_height_max_mm": float(self.support_height_max_mm),
        }


def diameter_class(diameter_mm: float, classes: tuple[float, ...] = DIAMETER_CLASSES_MM) -> str:
    """The class a measured tip diameter falls into, as its key in the class counts."""
    if not classes:
        raise ValueError("There has to be at least one diameter class")
    nearest = min(classes, key=lambda value: (abs(value - diameter_mm), value))
    return f"{nearest:.1f}"


def shell_contact(
    points: np.ndarray,
    distance_to_model: np.ndarray,
    plate_z: float,
    contact_gap: float = CONTACT_GAP_MM,
    cluster_radius: float = CLUSTER_RADIUS_MM,
    slab_height: float = SLAB_HEIGHT_MM,
    plate_gap: float = PLATE_GAP_MM,
) -> ShellContact:
    """One support shell: how many contacts it makes, how big, and what it stands on.

    A contact is a cluster of the shell's vertices that come within `contact_gap` of
    the model shell, so a branching tree counts its tips and not its trunk, and two
    trees whose tips are further apart than `cluster_radius` count twice. A cluster's
    diameter is its largest extent, which for the small disc a tip ends in is the
    contact head itself.

    A shell that reaches the plate carries the model. One that reaches it while
    staying flat and wide is a raft, not a tree: its top face is under a model that
    sits on it, so counting it as a tip would put one support on the plate under every
    model. One that stops above the plate stands on a raft, and is not on the plate.
    """
    spots = np.asarray(points, dtype=np.float64)
    distance = np.asarray(distance_to_model, dtype=np.float64).ravel()
    if spots.ndim != 2 or spots.shape[1] != 3:
        raise ValueError("Vertices must be an n by 3 array")
    if distance.shape != (len(spots),):
        raise ValueError("One distance per support vertex is expected")
    if contact_gap <= 0 or cluster_radius <= 0 or slab_height <= 0:
        raise ValueError("The gap, the cluster radius and the slab height must be positive")
    if not len(spots):
        return ShellContact()

    low = spots.min(axis=0)
    high = spots.max(axis=0)
    thickness = float(high[2] - low[2])
    span = float(max(high[0] - low[0], high[1] - low[1]))
    on_plate = bool(low[2] <= plate_z + plate_gap)

    contact = ShellContact(
        height_mm=float(low[2] - plate_z),
        thickness_mm=thickness,
        span_mm=span,
        reaches_plate=on_plate,
        raft=bool(on_plate and thickness <= slab_height and span >= 2.0 * thickness),
    )

    near = spots[distance <= contact_gap]
    for group in cluster_groups(near, cluster_radius):
        contact.tip_diameters_mm.append(float(np.max(group.max(axis=0) - group.min(axis=0))))
    contact.tips = len(contact.tip_diameters_mm)
    return contact


def measure_shells(
    model_shell: trimesh.Trimesh,
    support_shells: list[trimesh.Trimesh],
    rng: np.random.Generator,
    contact_gap: float = CONTACT_GAP_MM,
    cluster_radius: float = CLUSTER_RADIUS_MM,
    plate_z: float | None = None,
    slab_height: float = SLAB_HEIGHT_MM,
    plate_gap: float = PLATE_GAP_MM,
    classes: tuple[float, ...] = DIAMETER_CLASSES_MM,
) -> SceneSupportStats:
    """Every number the shells of one supported scene add up to.

    The distances are taken from the support shells to the model shell in place, both
    of them out of the same file, so no transform is involved and no registration
    error can reach a contact: this is what the distance method of M7.4c could not
    say while the registration was 0.5-0.9 mm off.

    Args:
        model_shell: the largest shell of the scene, as split_supported_scene gives it.
        support_shells: every other shell of the scene, the supports.
        rng: the seed of the surface sampling the distance query uses.
        contact_gap: how close a support vertex has to come to the model to contact it.
        cluster_radius: two contact vertices further apart than this are two tips.
        plate_z: the height of the plate, the lowest point of the scene. Defaults to
            the lowest point of the shells given.
        slab_height: a shell not thicker than this may be a raft.
        plate_gap: how far above the ground still counts as standing on it.
        classes: the tip diameters the measured ones are counted into.
    """
    model_faces = int(len(model_shell.faces))
    support_faces = int(sum(len(shell.faces) for shell in support_shells))
    stats = SceneSupportStats(
        shells=1 + len(support_shells),
        model_faces=model_faces,
        support_faces=support_faces,
        model_face_fraction=model_faces / (model_faces + support_faces)
        if model_faces + support_faces
        else 1.0,
        diameter_classes={f"{value:.1f}": 0 for value in classes},
    )
    if not support_shells:
        return stats
    if any(not len(shell.faces) for shell in support_shells):
        raise ValueError("A support shell of the scene holds no faces")

    points = [np.asarray(shell.vertices, dtype=np.float64) for shell in support_shells]
    # The plate is the lowest point of the whole scene, which is a support: a model
    # that stands on the plate on its own has no support to be measured against.
    lowest = [float(np.asarray(model_shell.vertices, dtype=np.float64)[:, 2].min())]
    lowest.extend(float(item[:, 2].min()) for item in points)
    plate = float(plate_z) if plate_z is not None else min(lowest)

    # One query for the whole scene: the dense samples of the model surface and the
    # exact band are built once, not once per structure.
    distance = distances_to_model(
        np.vstack(points), model_shell, np.eye(4), contact_gap, rng
    )

    offset = 0
    for item in points:
        contact = shell_contact(
            item,
            distance[offset : offset + len(item)],
            plate,
            contact_gap=contact_gap,
            cluster_radius=cluster_radius,
            slab_height=slab_height,
            plate_gap=plate_gap,
        )
        offset += len(item)

        stats.contacts.append(contact)
        stats.structures += 1
        stats.support_height_max_mm = max(stats.support_height_max_mm, contact.height_mm)
        if contact.raft:
            stats.rafts += 1
        elif contact.reaches_plate:
            stats.structures_on_plate += 1

        for diameter in contact.tip_diameters_mm:
            key = diameter_class(diameter, classes)
            stats.diameter_classes[key] = stats.diameter_classes.get(key, 0) + 1
        stats.tips += contact.tips
        stats.tip_diameters_mm.extend(contact.tip_diameters_mm)

    return stats
