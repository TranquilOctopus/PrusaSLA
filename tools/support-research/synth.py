from __future__ import annotations

import numpy as np
import trimesh
from trimesh.transformations import rotation_matrix

TIP_RADIUS = 0.6
UNDERSIDE_Z = 5.0


def build_model(rng: np.random.Generator) -> trimesh.Trimesh:
    outline = np.array(
        [[-9, -4], [12, -4], [12, 1], [0, 1], [0, 5], [-9, 5]],
        dtype=np.float64,
    )
    bottom = np.column_stack([outline, np.full(6, UNDERSIDE_Z)])
    top = np.column_stack([outline, 14.0 + 0.12 * outline[:, 0] + 0.18 * outline[:, 1]])
    cap = np.array([[0, 1, 3], [1, 2, 3], [0, 3, 5], [3, 4, 5]])
    faces = [*cap[:, ::-1], *(cap + 6)]
    for i in range(6):
        j = (i + 1) % 6
        faces.extend([[i, j, j + 6], [i, j + 6, i + 6]])
    block = trimesh.Trimesh(vertices=np.vstack([bottom, top]), faces=faces)
    bump = trimesh.creation.icosphere(subdivisions=2, radius=2.4)
    bump.apply_translation([-5.5, 2.0, 13.7])
    return trimesh.util.concatenate([block, bump])


def build_supports(
    model: trimesh.Trimesh, rng: np.random.Generator
) -> tuple[trimesh.Trimesh, list[dict]]:
    parts = []
    contacts = []
    for x, y in ((-6.0, 2.5), (4.0, -2.0), (10.0, -1.0)):
        apex = np.array([x, y, UNDERSIDE_Z])
        tip = trimesh.creation.cone(radius=TIP_RADIUS, height=1.2, sections=16)
        tip.apply_translation(apex - [0.0, 0.0, 1.2])
        pillar_height = UNDERSIDE_Z - 1.2
        pillar = trimesh.creation.cylinder(
            radius=TIP_RADIUS, height=pillar_height, sections=16
        )
        pillar.apply_translation([x, y, pillar_height / 2.0])
        parts.extend([tip, pillar])
        contacts.append(
            {
                "position": tip.vertices[np.argmax(tip.vertices[:, 2])].tolist(),
                "normal": [0.0, 0.0, -1.0],
                "tip_radius": TIP_RADIUS,
            }
        )
    pad = trimesh.creation.box(extents=[23.0, 10.0, 0.8])
    pad.apply_translation([1.5, 0.5, -0.4])
    parts.append(pad)
    return trimesh.util.concatenate(parts), contacts


def apply_print_transform(
    mesh: trimesh.Trimesh, rng: np.random.Generator
) -> tuple[trimesh.Trimesh, np.ndarray]:
    tilt = rotation_matrix(np.deg2rad(35.0), [1.0, 0.0, 0.0])
    yaw = rotation_matrix(np.deg2rad(25.0), [0.0, 0.0, 1.0])
    transform = yaw @ tilt
    transform[:3, 3] = [12.0, -7.0, 5.0]
    moved = mesh.copy()
    moved.apply_transform(transform)
    return moved, transform


def remesh(mesh: trimesh.Trimesh, rng: np.random.Generator) -> trimesh.Trimesh:
    grid = 0.45
    cells = np.floor(mesh.vertices / grid).astype(np.int64)
    _, inverse, counts = np.unique(cells, axis=0, return_inverse=True, return_counts=True)
    vertices = np.zeros((len(counts), 3))
    np.add.at(vertices, inverse, mesh.vertices)
    vertices /= counts[:, None]
    decimated = trimesh.Trimesh(vertices=vertices, faces=inverse[mesh.faces], process=False)
    decimated.update_faces(decimated.nondegenerate_faces())
    decimated.update_faces(decimated.unique_faces())
    decimated.remove_unreferenced_vertices()
    return decimated
