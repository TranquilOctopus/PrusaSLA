#!/usr/bin/env python3
"""Generate an orientation test piece STL for SLA printer calibration.

The model is an asymmetric flat plate with raised features that make
mirroring and rotation immediately visible when printed.
"""

import sys
import struct
from pathlib import Path


def box_triangles(x0, y0, z0, x1, y1, z1):
    """Return 12 triangles (36 vertices) for an axis-aligned box with outward normals.

    Each triangle is a tuple of 3 vertices, each vertex is (x, y, z).
    Winding is counter-clockwise when viewed from outside.
    """
    # Ensure proper ordering
    x0, x1 = min(x0, x1), max(x0, x1)
    y0, y1 = min(y0, y1), max(y0, y1)
    z0, z1 = min(z0, z1), max(z0, z1)

    tris = []

    # Face -X (normal -1, 0, 0): vertices ordered CCW from outside
    tris.append(((x0, y0, z0), (x0, y0, z1), (x0, y1, z1)))
    tris.append(((x0, y1, z1), (x0, y1, z0), (x0, y0, z0)))

    # Face +X (normal +1, 0, 0)
    tris.append(((x1, y0, z1), (x1, y0, z0), (x1, y1, z0)))
    tris.append(((x1, y1, z0), (x1, y1, z1), (x1, y0, z1)))

    # Face -Y (normal 0, -1, 0)
    tris.append(((x0, y0, z0), (x1, y0, z0), (x1, y0, z1)))
    tris.append(((x1, y0, z1), (x0, y0, z1), (x0, y0, z0)))

    # Face +Y (normal 0, +1, 0)
    tris.append(((x0, y1, z1), (x1, y1, z1), (x1, y1, z0)))
    tris.append(((x1, y1, z0), (x0, y1, z0), (x0, y1, z1)))

    # Face -Z (normal 0, 0, -1)
    tris.append(((x0, y0, z0), (x0, y1, z0), (x1, y1, z0)))
    tris.append(((x1, y1, z0), (x1, y0, z0), (x0, y0, z0)))

    # Face +Z (normal 0, 0, +1)
    tris.append(((x0, y0, z1), (x1, y0, z1), (x1, y1, z1)))
    tris.append(((x1, y1, z1), (x0, y1, z1), (x0, y0, z1)))

    return tris


def compute_normal(v0, v1, v2):
    """Compute outward normal for a triangle (assumes CCW winding from outside)."""
    # Cross product (v1 - v0) x (v2 - v0)
    ux, uy, uz = v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2]
    vx, vy, vz = v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2]
    nx = uy * vz - uz * vy
    ny = uz * vx - ux * vz
    nz = ux * vy - uy * vx
    # Normalize
    length = (nx * nx + ny * ny + nz * nz) ** 0.5
    if length == 0:
        return (0.0, 0.0, 0.0)
    return (nx / length, ny / length, nz / length)


def write_binary_stl(path, triangles):
    """Write triangles to a binary STL file."""
    with open(path, 'wb') as f:
        # 80-byte header
        f.write(b'orientation test piece for SLA printer calibration' + b'\x00' * 30)
        # Triangle count (uint32 little-endian)
        f.write(struct.pack('<I', len(triangles)))
        # Triangles
        for tri in triangles:
            normal = compute_normal(tri[0], tri[1], tri[2])
            f.write(struct.pack('<3f', *normal))
            for v in tri:
                f.write(struct.pack('<3f', *v))
            f.write(struct.pack('<H', 0))  # attribute byte count


def main():
    # Default output path
    if len(sys.argv) > 1:
        output_path = Path(sys.argv[1])
    else:
        output_path = Path('orientation_test_piece.stl')

    # All dimensions in mm
    # Base plate: 40 x 25 x 1.5 at Z=0
    base_plate = (0, 0, 0, 40, 25, 1.5)

    # Raised features at Z=1.5 to 3.0 (1.5mm tall)
    z_base = 1.5
    z_top = 3.0

    # Letter "F": stem along Y at left (x=4), ~18mm tall, arms pointing +X
    # Stem: x=[4,6], y=[0,18], z=[1.5,3.0]
    f_stem = (4, 0, z_base, 6, 18, z_top)
    # Top arm: x=[4,18], y=[16,18], z=[1.5,3.0]
    f_top = (4, 16, z_base, 18, 18, z_top)
    # Middle arm: x=[4,14], y=[8,10], z=[1.5,3.0]
    f_mid = (4, 8, z_base, 14, 10, z_top)

    # Square boss in +X/+Y corner, 2mm from edges: x=[34,38], y=[19,23], z=[1.5,3.0]
    boss = (34, 19, z_base, 38, 23, z_top)

    # Bar along -Y edge (y=0), right half x=[20,38], 2mm wide in Y
    bar = (20, 0, z_base, 38, 2, z_top)

    boxes = [base_plate, f_stem, f_top, f_mid, boss, bar]

    all_triangles = []
    for box in boxes:
        all_triangles.extend(box_triangles(*box))

    write_binary_stl(output_path, all_triangles)

    print(f"Generated {output_path} with {len(all_triangles)} triangles")


if __name__ == '__main__':
    main()