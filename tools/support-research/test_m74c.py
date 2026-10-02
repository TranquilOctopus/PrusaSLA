"""Tests for the calibration measurements of M7.4c.

Synthetic only, made from scratch: a box and a handful of points written here, no
mesh file, no dataset, no trimesh and no scipy. The measurement functions of
calibrate.py and calibration_report.py import numpy alone for exactly this reason, so
the tip counting, the up-axis tilt and the tables can be checked on any machine that
has the tools' requirements half installed.
"""

from __future__ import annotations

import math

import numpy as np
import pytest

from calibrate import (
    ManifestError,
    cluster_count,
    connected_components,
    count_support_tips,
    largest_flat_area,
    parse_pairs,
    select_pairs,
    up_axis,
    validate_pairs,
    vertex_labels,
    weld,
)
from calibration_report import collect, median_iqr, parse_log, render, share


# --------------------------------------------------------------------------------
# Synthetic shapes
# --------------------------------------------------------------------------------


def box(size=(10.0, 10.0, 4.0), centre=(0.0, 0.0, 0.0)) -> tuple[np.ndarray, np.ndarray]:
    """A closed box of outward-facing triangles, as a triangle soup."""
    half = np.asarray(size, dtype=np.float64) / 2.0
    middle = np.asarray(centre, dtype=np.float64)
    vertices = np.array(
        [
            [-1, -1, -1], [1, -1, -1], [1, 1, -1], [-1, 1, -1],
            [-1, -1, 1], [1, -1, 1], [1, 1, 1], [-1, 1, 1],
        ],
        dtype=np.float64,
    ) * half + middle
    faces = np.array(
        [
            [0, 3, 2], [0, 2, 1],      # -Z
            [4, 5, 6], [4, 6, 7],      # +Z
            [0, 1, 5], [0, 5, 4],      # -Y
            [1, 2, 6], [1, 6, 5],      # +X
            [2, 3, 7], [2, 7, 6],      # +Y
            [3, 0, 4], [3, 4, 7],      # -X
        ],
        dtype=np.int64,
    )
    return vertices, faces


def turn_over(vertices: np.ndarray, angle_deg: float = 180.0, axis=(1.0, 0.0, 0.0)) -> np.ndarray:
    """The same shape rotated, not mirrored: a printed model is turned, never flipped."""
    rotation = tilt_transform(angle_deg, axis)[:3, :3]
    return vertices @ rotation.T


def frustum(bottom_radius: float = 20.0, top_radius: float = 6.0, height: float = 30.0,
            sides: int = 16) -> tuple[np.ndarray, np.ndarray]:
    """A bust: a wide flat base, a narrow top, slanted walls. Triangle soup."""
    angles = np.linspace(0.0, 2.0 * np.pi, sides, endpoint=False)
    bottom = np.column_stack(
        [bottom_radius * np.cos(angles), bottom_radius * np.sin(angles), np.zeros(sides)]
    )
    top = np.column_stack(
        [top_radius * np.cos(angles), top_radius * np.sin(angles), np.full(sides, height)]
    )
    vertices = np.vstack([bottom, top])
    faces = []
    for index in range(sides):
        following = (index + 1) % sides
        # The wall quad, wound outwards, and then the two caps as fans.
        faces.append([index, following, following + sides])
        faces.append([index, following + sides, index + sides])
    for index in range(1, sides - 1):
        faces.append([0, index + 1, index])                          # the base, facing down
        faces.append([sides, sides + index, sides + index + 1])      # the top, facing up
    return vertices, np.asarray(faces, dtype=np.int64)


def polygon_area(radius: float, sides: int) -> float:
    """The area of the regular polygon of `sides` corners and circumradius `radius`."""
    return 0.5 * sides * radius * radius * math.sin(2.0 * math.pi / sides)


def disc(centre, radius=0.4, count=12) -> np.ndarray:
    """A ring of points, one tip's worth of support vertices."""
    angles = np.linspace(0.0, 2.0 * np.pi, count, endpoint=False)
    offsets = np.column_stack([np.cos(angles), np.sin(angles), np.zeros(count)])
    return np.asarray(centre, dtype=np.float64) + offsets * radius


def pillar(x, y, z_low, z_high, radius=0.4, count=12) -> np.ndarray:
    """A trunk of vertices from one height to another: a structure that reaches down."""
    angles = np.linspace(0.0, 2.0 * np.pi, count, endpoint=False)
    offsets = np.column_stack([np.cos(angles), np.sin(angles), np.zeros(count)]) * radius
    low = offsets + np.array([x, y, z_low], dtype=np.float64)
    high = offsets + np.array([x, y, z_high], dtype=np.float64)
    return np.vstack([low, high])


def gap_from(vertices: np.ndarray, surface_z: float) -> np.ndarray:
    """How far every vertex is from a flat model surface at `surface_z`, from below."""
    return surface_z - vertices[:, 2]


def tilt_transform(angle_deg: float, axis=(1.0, 0.0, 0.0)) -> np.ndarray:
    """A rotation by `angle_deg` about `axis`, as a 4 by 4 transform."""
    direction = np.asarray(axis, dtype=np.float64)
    direction /= np.linalg.norm(direction)
    radians = np.deg2rad(angle_deg)
    cross = np.array(
        [[0.0, -direction[2], direction[1]], [direction[2], 0.0, -direction[0]],
         [-direction[1], direction[0], 0.0]]
    )
    rotation = (
        np.eye(3) + np.sin(radians) * cross + (1.0 - np.cos(radians)) * (cross @ cross)
    )
    transform = np.eye(4)
    transform[:3, :3] = rotation
    return transform


# --------------------------------------------------------------------------------
# The manifest reader
# --------------------------------------------------------------------------------


MANIFEST = """\
# The calibration manifest of the research dataset (gitignored, never committed).
pairs:
  - id: cal001
    source: Factory Fortress
    model: TCDM2009
    part: head 3
    category: head
    supported_stl: 'C:/data/models/cal001/supported.stl'
    unsupported_stl: C:/data/models/cal001/plain.stl
  - id: cal002
    category: weapon
    supported_stl: out/x/cal002-supported.stl   # a raft under it
    unsupported_stl: out/x/cal002-plain.stl
notes:
  # Not a pair: another section must be skipped whole.
  - id: not-a-pair
    category: head
"""


def test_parse_pairs_reads_every_field_of_every_pair():
    pairs = parse_pairs(MANIFEST)

    assert len(pairs) == 2
    assert pairs[0]["id"] == "cal001"
    assert pairs[0]["category"] == "head"
    assert pairs[0]["source"] == "Factory Fortress"
    assert pairs[0]["part"] == "head 3"
    assert pairs[0]["supported_stl"] == "C:/data/models/cal001/supported.stl"
    assert pairs[0]["unsupported_stl"] == "C:/data/models/cal001/plain.stl"
    assert pairs[1]["id"] == "cal002"
    assert pairs[1]["category"] == "weapon"


def test_parse_pairs_strips_quotes_and_comments():
    pairs = parse_pairs(MANIFEST)

    # A quoted path keeps its colon, an unquoted one keeps its backslashes, and the
    # comment behind it goes.
    assert ":" in pairs[0]["supported_stl"]
    assert not pairs[1]["supported_stl"].endswith(".stl   ")
    assert pairs[1]["unsupported_stl"] == "out/x/cal002-plain.stl"


def test_parse_pairs_skips_the_other_sections():
    ids = [pair["id"] for pair in parse_pairs(MANIFEST)]

    assert "not-a-pair" not in ids


def test_parse_pairs_of_an_empty_manifest_is_empty():
    assert parse_pairs("") == []
    assert parse_pairs("pairs:\n") == []


def test_validate_pairs_names_the_pair_without_a_file():
    with pytest.raises(ManifestError) as failure:
        validate_pairs([{"id": "cal001", "supported_stl": "a.stl"}])

    assert "cal001" in str(failure.value)


def test_select_pairs_by_id_and_by_category():
    pairs = parse_pairs(MANIFEST)

    assert [pair["id"] for pair in select_pairs(pairs, ids="cal002")] == ["cal002"]
    assert [pair["id"] for pair in select_pairs(pairs, category="head")] == ["cal001"]
    # An unset filter takes everything, an id that is not there takes nothing.
    assert len(select_pairs(pairs)) == 2
    assert select_pairs(pairs, ids="cal404") == []


# --------------------------------------------------------------------------------
# The up-axis tilt
# --------------------------------------------------------------------------------


def test_up_axis_of_an_untransformed_model_is_vertical():
    tilt, axis = up_axis(np.eye(4))

    assert tilt == pytest.approx(0.0)
    assert axis == pytest.approx([0.0, 0.0, 1.0])


def test_up_axis_recovers_a_known_tilt():
    for angle in (15.0, 35.0, 90.0, 120.0):
        tilt, _ = up_axis(tilt_transform(angle))

        assert tilt == pytest.approx(angle)


def test_up_axis_says_which_way_the_model_was_tipped():
    # A head tipped over forward: the up axis points forward and to the plate.
    _, axis = up_axis(tilt_transform(35.0))

    assert axis[0] == pytest.approx(0.0)
    assert axis[2] == pytest.approx(np.cos(np.deg2rad(35.0)))
    assert abs(axis[1]) == pytest.approx(np.sin(np.deg2rad(35.0)))


def test_up_axis_of_a_full_turn_is_vertical_again():
    tilt, _ = up_axis(tilt_transform(360.0))

    assert tilt == pytest.approx(0.0, abs=1e-9)


def test_up_axis_refuses_a_transform_that_is_not_a_rotation():
    with pytest.raises(ValueError):
        up_axis(np.zeros((3, 3)))


# --------------------------------------------------------------------------------
# Weld and connected components
# --------------------------------------------------------------------------------


def test_weld_merges_the_duplicates_of_a_triangle_soup():
    vertices, faces = box()
    soup = vertices[faces].reshape(-1, 3)

    welded = weld(soup)

    assert len(soup) == len(faces) * 3
    assert int(welded.max()) + 1 == len(vertices)


def test_weld_keeps_corners_a_micron_apart_apart():
    vertices = np.array([[0.0, 0.0, 0.0], [0.0, 0.0, 0.0005]], dtype=np.float64)

    # 0.5 um is above the weld tolerance of 0.1 um: two corners stay two corners.
    assert int(weld(vertices).max()) + 1 == 2
    assert int(weld(vertices, tolerance=1e-3).max()) + 1 == 1


def test_connected_components_splits_a_chain_from_a_pair():
    # A chain 0-1-2 and a pair 3-4 that touches nothing else.
    labels = connected_components(5, [0, 1, 3], [1, 2, 4])

    assert len(set(labels.tolist())) == 2
    assert labels[0] == labels[2]
    assert labels[0] != labels[4]


def test_connected_components_joins_a_long_chain():
    count = 200
    labels = connected_components(count, np.arange(count - 1), np.arange(1, count))

    assert len(set(labels.tolist())) == 1


def test_connected_components_of_nothing_is_nothing():
    assert connected_components(0, [], []).tolist() == []
    # Nodes with no edge are each their own part.
    assert connected_components(3, [], []).tolist() == [0, 1, 2]


def test_connected_components_refuses_an_edge_out_of_range():
    with pytest.raises(ValueError):
        connected_components(2, [0, 5], [1, 4])


def vertex_labels_of(vertices: np.ndarray, faces: np.ndarray) -> np.ndarray:
    """The labels of a triangle soup: each face's three corners grouped as they are."""
    corners = len(faces) * 3
    return vertex_labels(
        vertices[faces].reshape(-1, 3), np.arange(corners, dtype=np.int64).reshape(-1, 3)
    )


def test_vertex_labels_joins_a_closed_box_into_one_component():
    vertices, faces = box()

    labels = vertex_labels_of(vertices, faces)

    # A closed box is one connected part, however many faces and corners it has.
    assert len(set(labels.tolist())) == 1
    assert int(weld(vertices[faces].reshape(-1, 3)).max()) + 1 == len(vertices)


def test_vertex_labels_splits_two_boxes_that_only_touch_nowhere():
    lower, lower_faces = box(size=(10.0, 10.0, 4.0))
    upper, upper_faces = box(size=(4.0, 4.0, 4.0), centre=(0.0, 0.0, 10.0))
    vertices = np.vstack([lower, upper])
    faces = np.vstack([lower_faces, upper_faces + len(lower)])

    assert len(set(vertex_labels_of(vertices, faces).tolist())) == 2


# --------------------------------------------------------------------------------
# Clusters
# --------------------------------------------------------------------------------


def test_cluster_count_of_one_ring_is_one_cluster():
    assert cluster_count(disc((0.0, 0.0, 0.0)), 1.5) == 1


def test_cluster_count_joins_a_chain_of_rings_within_the_radius():
    points = np.vstack([disc((0.0, 0.0, 0.0), radius=0.2), disc((1.2, 0.0, 0.0), radius=0.2)])

    # Two rings 1.2 mm apart, each ring's own points 0.4 mm apart.
    assert cluster_count(points, 1.5) == 1


def test_cluster_count_splits_rings_further_apart_than_the_radius():
    points = np.vstack([disc((0.0, 0.0, 0.0), radius=0.2), disc((2.0, 0.0, 0.0), radius=0.2)])

    assert cluster_count(points, 1.5) == 2


def test_cluster_count_of_nothing_is_zero():
    assert cluster_count(np.zeros((0, 3)), 1.5) == 0


# --------------------------------------------------------------------------------
# The tip count
# --------------------------------------------------------------------------------


def test_one_contact_is_one_tip():
    vertices = np.vstack([pillar(0.0, 0.0, 0.0, 6.0), disc((0.0, 0.0, 6.0))])

    counts = count_support_tips(vertices, np.zeros(len(vertices), dtype=np.int64),
                                gap_from(vertices, 6.0))

    assert counts.tips == 1
    assert counts.structures == 1
    assert counts.structures_on_plate == 1
    assert counts.base_structures == 0


def test_a_branching_tree_counts_its_tips_not_its_trunk():
    # One tree on the plate: three trunks 20 mm apart, each with one tip at 8 mm. The
    # model surface is at 8 mm, so the trunks 4 mm below it are nowhere near it.
    spots = np.vstack(
        [pillar(x, 0.0, 0.0, 4.0) for x in (-20.0, 0.0, 20.0)]
        + [pillar(x, 0.0, 4.0, 8.0) for x in (-20.0, 0.0, 20.0)]
        + [disc((x, 0.0, 8.0)) for x in (-20.0, 0.0, 20.0)]
    )

    counts = count_support_tips(spots, np.zeros(len(spots), np.int64), gap_from(spots, 8.0))

    # The trunks and the four millimetres of stem between them add nothing.
    assert counts.tips == 3
    assert counts.structures == 1
    assert counts.structures_on_plate == 1


def test_two_trees_that_touch_side_by_side_count_twice():
    # Two tips 1.0 mm apart: within one tip's cluster radius, but two structures.
    first = np.vstack([pillar(-0.5, 0.0, 0.0, 6.0), disc((-0.5, 0.0, 6.0))])
    second = np.vstack([pillar(0.5, 0.0, 0.0, 6.0), disc((0.5, 0.0, 6.0))])
    spots = np.vstack([first, second])
    component = np.concatenate([np.zeros(len(first), np.int64), np.ones(len(second), np.int64)])

    counts = count_support_tips(spots, component, gap_from(spots, 6.0))

    assert counts.tips == 2
    assert counts.structures == 2
    assert counts.structures_on_plate == 2


def test_one_tree_with_two_tips_on_top_of_each_other_counts_one():
    # Two tips 0.4 mm apart on the same trunk: one tip with a wide head, not two.
    first = np.vstack([pillar(0.0, 0.0, 0.0, 6.0), disc((0.0, 0.0, 6.0), radius=0.1)])
    second = disc((0.4, 0.0, 6.0), radius=0.1)
    spots = np.vstack([first, second])

    counts = count_support_tips(spots, np.zeros(len(spots), np.int64), gap_from(spots, 6.0))

    assert counts.tips == 1


def test_a_structure_that_never_reaches_the_model_counts_no_tip():
    # A tree that stands beside the model, 12 mm clear of it.
    vertices = np.vstack([pillar(0.0, 0.0, 0.0, 6.0), disc((0.0, 0.0, 6.0))])
    distance = gap_from(vertices, 18.0)

    counts = count_support_tips(vertices, np.zeros(len(vertices), np.int64), distance)

    assert counts.tips == 0
    assert counts.structures == 1
    assert counts.structures_on_plate == 1


def test_a_raft_is_a_base_and_not_a_tip():
    # A model sitting on a raft: the raft's top face is within a millimetre of the
    # model's underside, so counting it as a tip would put one support on the plate
    # under every model.
    raft = np.vstack(
        [disc((0.0, 0.0, 0.0), radius=6.0, count=48), disc((0.0, 0.0, 0.8), radius=6.0, count=48)]
    )
    tree = np.vstack([pillar(0.0, 0.0, 0.8, 6.0), disc((0.0, 0.0, 6.0))])
    spots = np.vstack([raft, tree])
    component = np.concatenate([np.zeros(len(raft), np.int64), np.ones(len(tree), np.int64)])

    counts = count_support_tips(spots, component, gap_from(spots, 6.0))

    assert counts.tips == 1
    assert counts.structures == 2
    assert counts.base_structures == 1
    assert counts.structures_on_plate == 1


def test_a_tip_beyond_the_gap_is_not_counted():
    # The registration is off by up to 0.9 mm, so the gap has to be wider than that:
    # a tip 1.2 mm off the surface still counts, one 3 mm off does not.
    near = disc((0.0, 0.0, 5.2))
    far = disc((30.0, 0.0, 3.0))
    spots = np.vstack([near, far])
    component = np.concatenate([np.zeros(len(near), np.int64), np.ones(len(far), np.int64)])

    counts = count_support_tips(spots, component, gap_from(spots, 6.0))

    assert counts.tips == 1


def test_tip_count_of_no_supports_at_all_is_zero():
    counts = count_support_tips(np.zeros((0, 3)), np.zeros(0, np.int64), np.zeros(0))

    assert counts.tips == 0
    assert counts.structures == 0
    assert counts.base_structures == 0


def test_tip_count_needs_one_component_and_one_distance_per_vertex():
    with pytest.raises(ValueError):
        count_support_tips(np.zeros((4, 3)), np.zeros(3, np.int64), np.zeros(4))


# --------------------------------------------------------------------------------
# The largest flat area
# --------------------------------------------------------------------------------


def test_the_largest_flat_area_of_a_box_is_its_base():
    vertices, faces = box(size=(10.0, 10.0, 4.0))

    area = largest_flat_area(vertices, faces)

    # 2 x 10 x 10 for the caps and 4 x 10 x 4 for the walls.
    assert area.area_mm2 == pytest.approx(100.0)
    assert area.fraction == pytest.approx(100.0 / 360.0)
    assert area.normal == pytest.approx([0.0, 0.0, -1.0], abs=1e-9)
    assert area.faces == 2
    assert area.faces_plate is True


def test_the_base_of_a_bust_is_its_largest_flat_area():
    vertices, faces = frustum(bottom_radius=20.0, top_radius=6.0, height=30.0)

    area = largest_flat_area(vertices, faces)

    # The base is wider than one slanted wall, so it is the patch that is found, and
    # it looks at the plate.
    assert area.area_mm2 == pytest.approx(polygon_area(20.0, 16))
    assert area.fraction > 0.25
    assert area.normal == pytest.approx([0.0, 0.0, -1.0], abs=1e-9)
    assert area.faces_plate is True


def test_a_bust_turned_over_puts_its_base_in_the_sky():
    vertices, faces = frustum(bottom_radius=20.0, top_radius=6.0, height=30.0)

    area = largest_flat_area(turn_over(vertices), faces)

    # Same base, now facing the sky: this is what "print it the other way up" means
    # for a miniature, and it is why the calibration measures the flat area at all.
    assert area.area_mm2 == pytest.approx(polygon_area(20.0, 16))
    assert area.normal == pytest.approx([0.0, 0.0, 1.0], abs=1e-9)
    assert area.faces_plate is False


def test_a_bigger_plate_under_a_small_box_is_the_largest_flat_area():
    plate, plate_faces = box(size=(40.0, 40.0, 2.0), centre=(0.0, 0.0, -3.0))
    small, small_faces = box(size=(6.0, 6.0, 4.0))
    vertices = np.vstack([plate, small])
    faces = np.vstack([plate_faces, small_faces + len(plate)])

    area = largest_flat_area(vertices, faces)

    assert area.area_mm2 == pytest.approx(1600.0)
    assert area.faces_plate is True


def test_a_column_has_no_flat_area_facing_the_plate():
    # A thin column: every side is as big as any other and none of them faces the
    # plate, so the answer has to be a dash, not a guess.
    vertices, faces = box(size=(6.0, 6.0, 40.0))

    area = largest_flat_area(vertices, faces)

    assert area.area_mm2 == pytest.approx(6.0 * 40.0)
    assert area.faces_plate is False


def test_the_largest_flat_area_of_nothing_is_nothing():
    area = largest_flat_area(np.zeros((0, 3)), np.zeros((0, 3), dtype=np.int64))

    assert area.area_mm2 == 0.0
    assert area.faces == 0
    assert area.faces_plate is False


def test_the_largest_flat_area_ignores_degenerate_triangles():
    vertices, faces = box(size=(10.0, 10.0, 4.0))
    flat = np.vstack([vertices, [[0.0, 0.0, 0.0]] * 3])
    with_degenerate = np.vstack([faces, [[len(vertices)] * 3]])

    area = largest_flat_area(flat, with_degenerate)

    assert area.area_mm2 == pytest.approx(100.0)


# --------------------------------------------------------------------------------
# The report
# --------------------------------------------------------------------------------


LOG = """\
-------------------------------------------------------------------------------
Local island coverage: benchmark corpus
[CalibSupport] cal001 [head] starts
[CalibSupport] id=cal001, category=head, points_as_loaded=1200, points_expert_orientation=800, \
islands=40, height_mm=32.5, height_expert_mm=35.0
[CalibSupport] id=cal002, category=weapon, points_as_loaded=300, points_expert_orientation=-1, \
islands=12, height_mm=90.0, height_expert_mm=-1
[CalibSupport] id=cal003, category=head, points_as_loaded=900, points_expert_orientation=600, \
islands=30, height_mm=30.0, height_expert_mm=31.0
Some other line without an id.
"""


def calibration_records() -> list[dict]:
    return [
        {
            "id": "cal001", "category": "head", "expert_support_count": 400,
            "support_structures": 3, "support_structures_on_plate": 2,
            "support_base_structures": 1, "registration_rms_mm": 0.4,
            "registration_p95_mm": 0.8, "up_axis_tilt_deg": 10.0, "model_height_mm": 35.0,
            "flat_area_faces_plate": False,
        },
        {
            "id": "cal002", "category": "weapon", "expert_support_count": 100,
            "support_structures": 1, "support_structures_on_plate": 1,
            "support_base_structures": 0, "registration_rms_mm": 0.6,
            "registration_p95_mm": 1.1, "up_axis_tilt_deg": 45.0, "model_height_mm": 90.0,
            "flat_area_faces_plate": True,
        },
        {
            "id": "cal003", "category": "head", "expert_support_count": 300,
            "support_structures": 2, "support_structures_on_plate": 1,
            "support_base_structures": 1, "registration_rms_mm": 0.3,
            "registration_p95_mm": 0.7, "up_axis_tilt_deg": 50.0, "model_height_mm": 31.0,
            "flat_area_faces_plate": False,
        },
    ]


def test_parse_log_keys_every_line_by_its_id():
    measured = parse_log(LOG)

    assert set(measured) == {"cal001", "cal002", "cal003"}
    assert measured["cal001"]["points_as_loaded"] == 1200
    assert measured["cal001"]["points_expert_orientation"] == 800
    assert measured["cal001"]["islands"] == 40


def test_parse_log_keeps_the_start_line_out_of_the_numbers():
    measured = parse_log(LOG)

    # The "starts" line has an id but no value of ours to keep.
    assert measured["cal001"]["points_as_loaded"] == 1200
    assert "category" not in measured["cal001"]


def test_a_missing_oriented_stl_is_not_measured():
    measured = parse_log(LOG)

    assert measured["cal002"]["points_expert_orientation"] == -1


def test_median_iqr_of_nothing_is_a_dash():
    assert median_iqr([]) == "-"


def test_median_iqr_prints_a_count_as_a_count_and_a_length_with_a_decimal():
    assert median_iqr([400.0, 300.0]) == "350 (325-375)"
    assert median_iqr([0.42, 0.51]) == "0.5 (0.4-0.5)"


def test_share_of_nothing_is_a_dash():
    assert share(0, 0) == "-"
    assert share(1, 4) == "25%"


def test_collect_groups_by_category_and_counts_the_dataset():
    categories = collect(calibration_records(), parse_log(LOG))
    by_name = {category.name: category for category in categories}

    assert set(by_name) == {"all", "head", "weapon"}
    assert by_name["head"].pairs == 2
    assert by_name["weapon"].pairs == 1
    # The dataset row holds every pair, so it is the sum of the categories.
    assert by_name["all"].pairs == 3


def test_collect_keeps_the_ratio_only_where_both_sides_were_measured():
    categories = collect(calibration_records(), parse_log(LOG))
    by_name = {category.name: category for category in categories}

    # 1200 of ours against 400 expert tips: twice as many.
    assert by_name["head"].ratio_as_loaded == [3.0, 3.0]
    # cal002 has no oriented STL, so only the as-loaded ratio counts.
    assert by_name["weapon"].ratio_expert_orientation == []
    assert by_name["weapon"].ratio_as_loaded == [3.0]
    assert by_name["head"].measured == 2
    assert by_name["weapon"].measured == 1


def test_collect_counts_the_orientation_shares():
    categories = collect(calibration_records(), parse_log(LOG))
    by_name = {category.name: category for category in categories}

    # head: 10 deg (up) and 50 deg (in the 30-60 band); weapon: 45 deg, neither.
    assert by_name["head"].tilt_known == 2
    assert by_name["head"].face_up == 1
    assert by_name["head"].tipped == 1
    assert by_name["weapon"].face_up == 0
    assert by_name["weapon"].tipped == 1


def test_render_writes_aggregates_and_no_id():
    report = render(collect(calibration_records(), parse_log(LOG)))

    assert "### Support density" in report
    assert "### Orientation" in report
    assert "### Structure and registration residual" in report
    assert "| head |" in report
    for pair_id in ("cal001", "cal002", "cal003"):
        assert pair_id not in report


def test_render_survives_a_category_the_generator_never_ran_on():
    # Only calibrate.py ran: no line, no ratio, and the row says so instead of lying.
    categories = collect(calibration_records(), {})

    report = render(categories)
    head = next(category for category in categories if category.name == "head")

    assert head.measured == 0
    assert "|" in report
