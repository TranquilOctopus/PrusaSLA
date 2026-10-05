from __future__ import annotations

import numpy as np
import trimesh
from trimesh.transformations import rotation_matrix

TIP_RADIUS = 0.6
UNDERSIDE_Z = 5.0

# The L outline the block is cut from: asymmetric in x and in y, and not convex, so a
# rotation that lines it up with a copy of itself is the only one that works.
OUTLINE = np.array(
    [[-9, -4], [12, -4], [12, 1], [0, 1], [0, 5], [-9, 5]], dtype=np.float64
)
# The four triangles that close the outline, an ear clipping of it: the L is not convex,
# so a fan from one corner would not do.
CAP_CORNERS = np.array([[0, 1, 3], [1, 2, 3], [0, 3, 5], [3, 4, 5]], dtype=np.int64)


def build_block(bands: int = 1) -> trimesh.Trimesh:
    """The L-block on its own: one closed shell, so a split of it finds one shell.

    `bands` splits the walls into that many rings, which is how a model gets more faces
    than a support without changing its shape.
    """
    if bands < 1:
        raise ValueError("A block has at least one band")

    rings = []
    for index in range(bands + 1):
        height = UNDERSIDE_Z + index * (9.0 / bands)
        # The top ring is tilted, so the block has no plane of symmetry to align on.
        tilt = 0.12 * OUTLINE[:, 0] + 0.18 * OUTLINE[:, 1] if index == bands else np.zeros(6)
        rings.append(np.column_stack([OUTLINE, np.full(6, height) + tilt]))
    vertices = np.vstack(rings)

    faces = []
    for index in range(bands):
        low, high = 6 * index, 6 * (index + 1)
        for corner in range(6):
            following = (corner + 1) % 6
            faces.append([low + corner, low + following, high + following])
            faces.append([low + corner, high + following, high + corner])
    top = 6 * bands
    for triangle in CAP_CORNERS:
        faces.append([int(c) for c in triangle[::-1]])
        faces.append([top + int(c) for c in triangle])
    return trimesh.Trimesh(
        vertices=vertices, faces=np.asarray(faces, dtype=np.int64), process=False
    )


def build_model(rng: np.random.Generator) -> trimesh.Trimesh:
    """The L-block with a bump on it: two shells, as a remeshed miniature arrives."""
    bump = trimesh.creation.icosphere(subdivisions=2, radius=2.4)
    bump.apply_translation([-5.5, 2.0, 13.7])
    return trimesh.util.concatenate([build_block(), bump])


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


# The tip diameters of the SLA presets, as radii of the contact head a support ends in.
TIP_HEAD_RADII = (0.05, 0.1, 0.15, 0.2, 0.3)
TRUNK_RADIUS = 0.35
HEAD_HEIGHT = 0.6


def lathe(profile: np.ndarray, sections: int = 16) -> trimesh.Trimesh:
    """A closed solid of revolution around Z, from a profile of (radius, z) points.

    Every profile radius has to be positive: the two flat ends are closed by a fan from
    a centre point on the axis, so a profile that ends on the axis would leave a ring of
    coincident corners behind. One winding serves a wall that leans either way and the
    horizontal annulus of a step, which is what makes one body out of a trunk and a
    contact head: two concentric cylinders joined by a step are one shell, a cylinder
    stacked on another is two.
    """
    corners = np.asarray(profile, dtype=np.float64)
    if corners.ndim != 2 or corners.shape[1] != 2 or len(corners) < 2:
        raise ValueError("A profile is an n by 2 array of radius and height")
    if sections < 3:
        raise ValueError("A solid of revolution needs at least three sections")
    if (corners[:, 0] <= 0).any():
        raise ValueError("A profile radius has to be positive, the ends are capped")

    angles = np.linspace(0.0, 2.0 * np.pi, sections, endpoint=False)
    ring = np.column_stack([np.cos(angles), np.sin(angles), np.zeros(sections)])
    vertices = np.vstack([radius * ring + [0.0, 0.0, height] for radius, height in corners])
    low_centre = len(vertices)
    top_centre = low_centre + 1
    vertices = np.vstack([vertices, [[0.0, 0.0, corners[0, 1]], [0.0, 0.0, corners[-1, 1]]]])

    faces = []
    for level in range(len(corners) - 1):
        low, high = level * sections, (level + 1) * sections
        for index in range(sections):
            following = (index + 1) % sections
            faces.append([low + index, low + following, high + following])
            faces.append([low + index, high + following, high + index])
    # The two flat faces, each a fan of the ring around the centre on the axis.
    top = (len(corners) - 1) * sections
    for index in range(1, sections - 1):
        faces.append([low_centre, index + 1, index])
        faces.append([top_centre, top + index, top + index + 1])
    return trimesh.Trimesh(
        vertices=vertices, faces=np.asarray(faces, dtype=np.int64), process=False
    )


def build_support(
    tip_head_radius: float,
    underside_z: float = UNDERSIDE_Z,
    trunk_radius: float = TRUNK_RADIUS,
    head_height: float = HEAD_HEIGHT,
    sections: int = 16,
) -> trimesh.Trimesh:
    """One support shell: a trunk from the plate up to a small head under the model.

    The head is the last `head_height` of the support, and the step below it is further
    than the contact gap from the model, so a measurement of the contact sees the head
    and nothing of the trunk: what comes back is the tip diameter that was built.
    """
    profile = np.array(
        [
            [trunk_radius, 0.0],
            [trunk_radius, underside_z - head_height],
            [tip_head_radius, underside_z - head_height],
            [tip_head_radius, underside_z],
        ],
        dtype=np.float64,
    )
    return lathe(profile, sections)


def as_soup(mesh: trimesh.Trimesh) -> trimesh.Trimesh:
    """The same faces with every corner stored once per face, the way an STL holds them."""
    corners = np.asarray(mesh.faces, dtype=np.int64).reshape(-1)
    return trimesh.Trimesh(
        vertices=np.asarray(mesh.vertices, dtype=np.float64)[corners],
        faces=np.arange(len(corners), dtype=np.int64).reshape(-1, 3),
        process=False,
    )


def build_shell_scene(
    tip_head_radii=TIP_HEAD_RADII,
    underside_z: float = UNDERSIDE_Z,
    sections: int = 16,
    spacing: float = 3.6,
    first_x: float = -7.0,
    bands: int = 12,
    model: trimesh.Trimesh | None = None,
) -> tuple[trimesh.Trimesh, trimesh.Trimesh, list[float]]:
    """A supported scene written the way a slicer writes one: N+1 disjoint bodies.

    The model and one support shell per entry of `tip_head_radii`, in one triangle soup
    with no vertex in common, exactly as an STL export of a supported file arrives.
    The model is one shell with more faces than any support of it, which is what tells
    the split which shell is the model. Every support is a trunk standing on the plate
    with a head of a known diameter touching the underside of the model, so the contacts
    have to come back at the sizes they were built with.

    Returns the scene, the plain model on its own, and the tip radii that were built.
    """
    if model is None:
        model = build_block(bands)
    radii = [float(value) for value in tip_head_radii]
    supports = []
    for index, radius in enumerate(radii):
        support = build_support(radius, underside_z=underside_z, sections=sections)
        support.apply_translation([first_x + spacing * index, 0.0, 0.0])
        supports.append(support)

    scene = trimesh.util.concatenate([model, *supports])
    return as_soup(scene), model, radii
