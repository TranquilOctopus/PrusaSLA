"""Describe each support contact on a supported mesh (M7.3c).

After registration (register.py) and separation (separate.py), the support
faces are known. This module turns them into one record per contact: where the
support touches the model, how big the tip is, how deep it sits, what the
surface looks like there, and which support structure it belongs to.

All lengths are millimetres. Positions and normals are reported in the scene
(print) frame, which is the frame of the supported mesh, and in the model frame
so they can be compared against a model-space ground truth.
"""

from __future__ import annotations

import json
from dataclasses import asdict, dataclass, field
from pathlib import Path

import numpy as np
import trimesh
from scipy.spatial import cKDTree

from separate import _sample_surface_dense, _transform_centroids_to_model_frame, _validate_common


@dataclass
class Contact:
    """One support-to-model contact."""

    position_scene: list[float]
    position_model: list[float]
    normal_scene: list[float]
    normal_model: list[float]
    diameter: float
    penetration: float
    height_above_plate: float
    overhang_deg: float
    mean_curvature: float
    serves_local_minimum: bool
    support_component: int
    vertex_count: int


@dataclass
class ContactReport:
    """Contacts plus the structure they belong to."""

    contacts: list[Contact] = field(default_factory=list)
    components: list[dict] = field(default_factory=list)
    plate_z: float = 0.0

    def to_json(self, path: str | Path) -> Path:
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        payload = {
            "plate_z": self.plate_z,
            "components": self.components,
            "contacts": [asdict(c) for c in self.contacts],
        }
        path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
        return path


def _cluster(points: np.ndarray, radius: float) -> np.ndarray:
    """Group points into clusters; two points join when within radius."""
    tree = cKDTree(points)
    labels = np.full(len(points), -1, dtype=np.int64)
    label = 0
    for seed in range(len(points)):
        if labels[seed] != -1:
            continue
        queue = [seed]
        labels[seed] = label
        while queue:
            for neighbour in tree.query_ball_point(points[queue.pop()], radius):
                if labels[neighbour] == -1:
                    labels[neighbour] = label
                    queue.append(neighbour)
        label += 1
    return labels


def _component_of_face(mesh: trimesh.Trimesh) -> np.ndarray:
    """Connected-component index per face, via face adjacency."""
    components = trimesh.graph.connected_components(
        mesh.face_adjacency, nodes=np.arange(len(mesh.faces))
    )
    out = np.zeros(len(mesh.faces), dtype=np.int64)
    for index, faces in enumerate(components):
        out[faces] = index
    return out


def describe_contacts(
    model: trimesh.Trimesh,
    supported_scene: trimesh.Trimesh,
    model_to_scene: np.ndarray,
    support_mask: np.ndarray,
    contact_gap: float = 0.35,
    cluster_radius: float = 1.5,
    minimum_radius: float = 2.0,
    curvature_radius: float = 1.0,
    width_band: float = 1.5,
) -> ContactReport:
    """Build a contact record for every place a support touches the model.

    Args:
        support_mask: per scene face, True where the face belongs to a support
            (from :func:`separate.separate_supports_scaled`).
        contact_gap: support vertices within this distance of the model surface
            count as touching it.
        cluster_radius: touching vertices closer than this form one contact.
        minimum_radius: radius used to decide whether a contact sits at a local
            low point of the model.
        curvature_radius: ball radius for the discrete mean curvature measure.
        width_band: how far below the surface the tip width is measured. A tip
            that only touches at its point still has a width just beneath it.
    """
    transform = _validate_common(model, supported_scene, model_to_scene, contact_gap)
    support_mask = np.asarray(support_mask, dtype=bool)
    if support_mask.shape != (len(supported_scene.faces),):
        raise ValueError("Support mask must have one entry per scene face")
    if not np.isfinite([cluster_radius, minimum_radius, curvature_radius, width_band]).all() or min(
        cluster_radius, minimum_radius, curvature_radius, width_band
    ) <= 0:
        raise ValueError("Radii must be finite and positive")

    plate_z = float(supported_scene.vertices[:, 2].min()) if len(supported_scene.vertices) else 0.0
    report = ContactReport(plate_z=plate_z)
    if not support_mask.any():
        return report

    supports = supported_scene.submesh([np.flatnonzero(support_mask)], append=True, repair=False)
    face_component = _component_of_face(supports)
    vertex_component = np.zeros(len(supports.vertices), dtype=np.int64)
    vertex_component[supports.faces.reshape(-1)] = np.repeat(face_component, 3)

    # Support vertices close to the model surface, found against surface samples
    # (upper bound), then confirmed exactly.
    samples = _sample_surface_dense(model, min(max(len(model.faces) * 4, 5_000), 200_000),
                                    np.random.default_rng(0))
    sample_tree = cKDTree(trimesh.transform_points(samples, transform))
    approx, _ = sample_tree.query(supports.vertices, k=1, workers=1)
    band = contact_gap + width_band
    candidates = np.flatnonzero(approx <= band + cluster_radius)
    if not len(candidates):
        return report
    in_model_frame = _transform_centroids_to_model_frame(supports.vertices[candidates], transform)
    closest, exact, faces = trimesh.proximity.closest_point(model, in_model_frame)
    near_surface = exact <= band
    touching = exact <= contact_gap
    if not touching.any():
        return report

    rotation = transform[:3, :3]
    seed_indices = np.flatnonzero(touching)
    labels = _cluster(supports.vertices[candidates[seed_indices]], cluster_radius)

    # Build a KD-tree over all support vertices for efficient tip-width queries.
    support_tree = cKDTree(supports.vertices)

    for label in range(labels.max() + 1):
        seeds = seed_indices[labels == label]
        surface_model = closest[seeds]
        position_model = surface_model.mean(axis=0)
        normal_model = model.face_normals[faces[seeds]].mean(axis=0)
        norm = np.linalg.norm(normal_model)
        if norm == 0:
            continue
        normal_model = normal_model / norm
        position_scene = trimesh.transform_points(position_model[None], transform)[0]
        normal_scene = rotation @ normal_model

# Tip width: support vertices within `reach` of the contact position,
        # converted to model frame, with depth along normal_model in [0, width_band].
        # This measures the tip geometry just below the contact (along the support
        # direction), independent of the contact_gap band used for seeding.
        reach = max(3.0 * cluster_radius, 2.0)
        nearby_idx = support_tree.query_ball_point(position_scene, reach)
        if not nearby_idx:
            # Fallback: use seed vertices only.
            nearby_vertices = supports.vertices[candidates[seeds]]
        else:
            nearby_vertices = supports.vertices[nearby_idx]
        in_model = _transform_centroids_to_model_frame(nearby_vertices, transform)
        offsets = in_model - position_model
        # Depth along the normal (positive in the support direction, outside the model).
        depth_along_normal = offsets @ normal_model
        # Keep vertices that are at or just outside the surface (depth >= 0) and within width_band.
        in_band = (depth_along_normal >= 0.0) & (depth_along_normal <= width_band)
        band_vertices = in_model[in_band]
        if len(band_vertices):
            band_offsets = band_vertices - position_model
            tangential = band_offsets - np.outer(band_offsets @ normal_model, normal_model)
            radii = np.linalg.norm(tangential, axis=1)
            diameter = float(2.0 * radii.max())
            # Penetration: how far support reaches into the model (opposite to normal).
            depth_into = -depth_along_normal
            penetration = float(max(depth_into[in_band].max(), 0.0))
        else:
            # Fallback to seed vertices (typically just the apex).
            seed_offsets = offsets
            tangential = seed_offsets - np.outer(seed_offsets @ normal_model, normal_model)
            radii = np.linalg.norm(tangential, axis=1)
            diameter = float(2.0 * radii.max()) if len(radii) else 0.0
            depth_into = - (seed_offsets @ normal_model)
            penetration = float(max(depth_into.max(), 0.0)) if len(depth_into) else 0.0

        overhang = float(np.degrees(np.arccos(np.clip(-normal_scene[2], -1.0, 1.0))))
        curvature = float(
            trimesh.curvature.discrete_mean_curvature_measure(
                model, position_model[None], curvature_radius
            )[0]
        )

        # A local low point: no model surface sits below this contact nearby.
        neighbours = sample_tree.query_ball_point(position_scene, minimum_radius)
        below = sample_tree.data[neighbours][:, 2] < position_scene[2] - 0.05 if neighbours else []
        serves_local_minimum = not bool(np.any(below))

        # vertex_count: number of support vertices in the tip measurement band
        vertex_count = int(len(band_vertices)) if len(band_vertices) else int(len(seeds))

        report.contacts.append(
            Contact(
                position_scene=position_scene.tolist(),
                position_model=position_model.tolist(),
                normal_scene=normal_scene.tolist(),
                normal_model=normal_model.tolist(),
                diameter=diameter,
                penetration=penetration,
                height_above_plate=float(position_scene[2] - plate_z),
                overhang_deg=overhang,
                mean_curvature=curvature,
                serves_local_minimum=serves_local_minimum,
                support_component=int(vertex_component[candidates[seeds[0]]]),
                vertex_count=vertex_count,
            )
        )

    for index in range(face_component.max() + 1 if len(face_component) else 0):
        faces_in = face_component == index
        vertices_in = supports.vertices[supports.faces[faces_in].reshape(-1)]
        report.components.append(
            {
                "component": index,
                "faces": int(faces_in.sum()),
                "contacts": sum(c.support_component == index for c in report.contacts),
                "touches_plate": bool(vertices_in[:, 2].min() <= plate_z + 0.05),
                "min_z": float(vertices_in[:, 2].min()),
                "max_z": float(vertices_in[:, 2].max()),
            }
        )
    return report


def write_debug_mesh(
    supported_scene: trimesh.Trimesh,
    support_mask: np.ndarray,
    report: ContactReport,
    path: str | Path,
    marker_radius: float = 0.8,
) -> Path:
    """Write a coloured mesh: model grey, supports teal, contacts amber spheres."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    scene = supported_scene.copy()
    colours = np.tile(np.array([170, 170, 170, 255], dtype=np.uint8), (len(scene.faces), 1))
    colours[np.asarray(support_mask, dtype=bool)] = [82, 121, 111, 255]
    scene.visual.face_colors = colours
    parts = [scene]
    for contact in report.contacts:
        marker = trimesh.creation.icosphere(subdivisions=1, radius=marker_radius)
        marker.apply_translation(contact.position_scene)
        marker.visual.face_colors = np.tile(
            np.array([227, 168, 87, 255], dtype=np.uint8), (len(marker.faces), 1)
        )
        parts.append(marker)
    trimesh.util.concatenate(parts).export(path)
    return path
