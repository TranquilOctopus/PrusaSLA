"""Tests for the shell split of a supported scene (M7.3e).

Synthetic only, made from scratch with synth.py: a model and a handful of cone
supports with known tip diameters, written as the triangle soup a slicer writes, with
and without a print rotation on top. No mesh file, no dataset, no networkx - the
components come from calibrate.connected_components, which is a numpy union-find.
"""

from __future__ import annotations

import numpy as np
import pytest
import trimesh

from calibrate import summarise, up_axis
from register import register
from shells import (
    DIAMETER_CLASSES_MM,
    diameter_class,
    face_components,
    measure_shells,
    shell_contact,
    split_supported_scene,
)
from synth import (
    TIP_HEAD_RADII,
    UNDERSIDE_Z,
    apply_print_transform,
    build_block,
    build_shell_scene,
    build_support,
    lathe,
)


def transform_error(estimated: np.ndarray, expected: np.ndarray) -> float:
    rotation = estimated[:3, :3].T @ expected[:3, :3]
    return float(
        np.degrees(np.arccos(np.clip((np.trace(rotation) - 1.0) / 2.0, -1.0, 1.0)))
    )


@pytest.fixture(scope="module")
def shell_scene():
    scene, model, radii = build_shell_scene()
    return scene, model, radii


@pytest.fixture(scope="module")
def model_and_supports(shell_scene):
    scene, model, radii = shell_scene
    model_shell, support_shells = split_supported_scene(scene)
    return model, model_shell, support_shells, radii


# --------------------------------------------------------------------------------
# The split
# --------------------------------------------------------------------------------


def test_split_finds_the_model_and_every_support(model_and_supports):
    model, model_shell, support_shells, radii = model_and_supports

    assert len(support_shells) == len(radii)
    # The model shell is the model: same face count, same extents, and it is the shell
    # that has the most faces by a long way.
    assert len(model_shell.faces) == len(model.faces)
    np.testing.assert_allclose(model_shell.extents, model.extents, atol=1e-9)
    assert len(model_shell.faces) > max(len(shell.faces) for shell in support_shells)


def test_split_reads_a_triangle_soup_of_disjoint_bodies(shell_scene):
    scene, _, radii = shell_scene

    # Nothing in the scene shares a vertex index with anything else: the split has to
    # come out of the positions, the way it does for an STL.
    corners = np.asarray(scene.faces, dtype=np.int64).reshape(-1)
    assert len(corners) == 3 * len(scene.faces)
    _, _, labels = face_components(scene.vertices, scene.faces)
    assert len(set(labels.tolist())) == 1 + len(radii)


def test_every_support_shell_keeps_its_own_faces(shell_scene, model_and_supports):
    scene, model, radii = shell_scene
    _, model_shell, support_shells, _ = model_and_supports

    for shell in support_shells:
        assert len(shell.faces) > 0
        # No vertex that none of its own faces uses: a stale one would look like a
        # structure of its own, and a face of the model would ride along.
        assert len(shell.vertices) == len(np.unique(shell.faces))
    # The shells partition the scene: every face of it is in exactly one of them, so
    # nothing was dropped and nothing was counted twice.
    assert len(model_shell.faces) + sum(len(shell.faces) for shell in support_shells) == len(
        scene.faces
    )
    assert len(model_shell.faces) == len(model.faces)


def test_a_single_shell_scene_has_no_supports():
    model = build_block()

    model_shell, support_shells = split_supported_scene(model)

    assert support_shells == []
    assert len(model_shell.faces) == len(model.faces)


def test_the_model_block_is_one_closed_shell():
    model = build_block(bands=4)

    assert model.is_watertight
    assert model.volume > 0.0
    # One shell however the walls are split into bands, and more faces as they are, which
    # is how a test scene gives its model more faces than any one support.
    assert split_supported_scene(model)[1] == []
    assert len(build_block(bands=8).faces) > len(model.faces)


def test_face_components_of_a_scene_without_faces_is_nothing():
    with pytest.raises(ValueError):
        face_components(np.zeros((0, 3)), np.zeros((0, 3), dtype=np.int64))


def test_face_components_refuses_a_face_that_indexes_nothing():
    with pytest.raises(ValueError):
        face_components(np.zeros((3, 3)), np.array([[0, 1, 2], [0, 1, 9]]))


# --------------------------------------------------------------------------------
# Contacts, diameters, structures
# --------------------------------------------------------------------------------


def test_tips_and_diameter_classes_come_back_exactly(model_and_supports):
    _, model_shell, support_shells, radii = model_and_supports

    stats = measure_shells(model_shell, support_shells, np.random.default_rng(5))

    assert stats.shells == 1 + len(radii)
    assert stats.structures == len(radii)
    assert stats.tips == len(radii)
    # One tip per support, at the diameter it was built with: the head is all there is
    # within the contact gap, so nothing wider is measured.
    np.testing.assert_allclose(sorted(stats.tip_diameters_mm), sorted(2.0 * np.asarray(radii)), atol=1e-9)
    assert stats.diameter_classes == {
        "0.1": 1, "0.2": 1, "0.3": 1, "0.4": 1, "0.6": 1,
    }
    assert stats.tips == sum(stats.diameter_classes.values())


def test_every_support_of_the_scene_reaches_the_plate(model_and_supports):
    _, model_shell, support_shells, radii = model_and_supports

    stats = measure_shells(model_shell, support_shells, np.random.default_rng(5))

    assert stats.structures_on_plate == len(radii)
    assert stats.rafts == 0
    assert stats.support_height_max_mm == pytest.approx(0.0)


def test_a_raft_is_a_base_and_not_a_tip():
    # A model standing on a raft: a wide flat shell on the plate whose top face is a
    # millimetre below the model, and one trunk standing on the raft, reaching up to it.
    raft = trimesh.creation.box(extents=[24.0, 10.0, 1.0])
    raft.apply_translation([1.0, 0.0, 0.5])
    trunk = build_support(TIP_HEAD_RADII[0], underside_z=UNDERSIDE_Z - 1.0)
    trunk.apply_translation([-7.0, 0.0, 1.0])

    model_shell, support_shells = split_supported_scene(
        trimesh.util.concatenate([build_block(bands=12), raft, trunk])
    )
    stats = measure_shells(model_shell, support_shells, np.random.default_rng(5))

    assert stats.structures == 2
    assert stats.rafts == 1
    # The trunk stands on the raft, a millimetre up, so it is not on the plate.
    assert stats.structures_on_plate == 0
    # The raft's top face is under the model, but counting it as a tip would put one
    # support on the plate under every model that sits on a raft. The trunk's head is
    # the one contact there is.
    assert stats.tips == 1
    assert stats.support_height_max_mm == pytest.approx(1.0)


def test_a_tree_that_stops_short_of_the_model_is_no_tip():
    trunk = build_support(TIP_HEAD_RADII[1], underside_z=2.0)
    trunk.apply_translation([-7.0, 0.0, 0.0])

    model_shell, support_shells = split_supported_scene(
        trimesh.util.concatenate([build_block(bands=12), trunk])
    )
    stats = measure_shells(model_shell, support_shells, np.random.default_rng(5))

    assert stats.structures == 1
    assert stats.structures_on_plate == 1
    assert stats.tips == 0


def test_two_heads_within_one_cluster_radius_are_still_two_tips():
    # Two thin trunks 0.4 mm apart, so their heads would be one cluster of points if
    # the clusters were not built per structure.
    first = build_support(TIP_HEAD_RADII[0], trunk_radius=0.15)
    first.apply_translation([-7.0, 0.0, 0.0])
    second = build_support(TIP_HEAD_RADII[0], trunk_radius=0.15)
    second.apply_translation([-6.6, 0.0, 0.0])

    model_shell, support_shells = split_supported_scene(
        trimesh.util.concatenate([build_block(bands=12), first, second])
    )
    stats = measure_shells(model_shell, support_shells, np.random.default_rng(5))

    assert stats.structures == 2
    assert stats.tips == 2
    assert stats.diameter_classes["0.1"] == 2


def test_a_cluster_of_touching_vertices_is_one_tip_measured_its_width():
    # One head of 0.1 mm and a second head 0.3 mm to the side, both touching the model:
    # within the cluster radius, so one contact, and its width is the whole cluster.
    angles = np.linspace(0.0, 2.0 * np.pi, 16, endpoint=False)
    ring = np.column_stack([np.cos(angles), np.sin(angles), np.zeros(16)])
    points = np.vstack([0.05 * ring + [0.0, 0.0, 5.0], 0.05 * ring + [0.3, 0.0, 5.0]])

    contact = shell_contact(points, np.zeros(len(points)), plate_z=5.0)

    assert contact.tips == 1
    assert contact.tip_diameters_mm[0] == pytest.approx(0.4)
    assert contact.thickness_mm == pytest.approx(0.0)
    assert contact.height_mm == pytest.approx(0.0)
    # Flat and wide and on the plate: a raft, not a tree, whatever it touched.
    assert contact.reaches_plate is True
    assert contact.raft is True


def test_shell_contact_needs_one_distance_per_vertex():
    with pytest.raises(ValueError):
        shell_contact(np.zeros((4, 3)), np.zeros(3), 0.0)


def test_diameter_class_is_the_nearest_size_a_printer_offers():
    assert diameter_class(0.11) == "0.1"
    assert diameter_class(0.16) == "0.2"
    assert diameter_class(0.62) == "0.6"
    assert diameter_class(1.4) == "0.6"
    assert list(DIAMETER_CLASSES_MM) == [0.1, 0.2, 0.3, 0.4, 0.6]
    with pytest.raises(ValueError):
        diameter_class(0.2, classes=())


def test_measure_shells_of_a_scene_without_supports_is_all_zero():
    model = build_block()

    model_shell, support_shells = split_supported_scene(model)
    stats = measure_shells(model_shell, support_shells, np.random.default_rng(5))

    assert stats.shells == 1
    assert stats.structures == 0
    assert stats.tips == 0
    assert stats.contacts == []
    assert stats.model_face_fraction == pytest.approx(1.0)
    assert sum(stats.diameter_classes.values()) == 0


def test_measure_shells_refuses_a_support_shell_without_faces(model_and_supports):
    _, model_shell, support_shells, _ = model_and_supports
    empty = support_shells[0].copy()
    empty.update_faces(np.zeros((0, 3), dtype=np.int64))

    with pytest.raises(ValueError):
        measure_shells(model_shell, [empty], np.random.default_rng(5))


def test_lathe_refuses_a_profile_that_ends_on_the_axis():
    with pytest.raises(ValueError):
        lathe(np.array([[0.0, 0.0], [1.0, 1.0]]))


# --------------------------------------------------------------------------------
# Registration against the model shell
# --------------------------------------------------------------------------------


def test_registration_of_the_model_against_the_model_shell_recovers_the_rotation(shell_scene):
    scene, model, radii = shell_scene
    moved, expected = apply_print_transform(scene, np.random.default_rng(11))

    model_shell, support_shells = split_supported_scene(moved)
    assert len(support_shells) == len(radii)
    result = register(model, model_shell, np.random.default_rng(11))

    angle = transform_error(result.transform, expected)
    print(f"rotation_error={angle:.6f}deg inlier_fraction={result.inlier_fraction:.4f}")
    assert angle < 0.5
    assert result.inlier_fraction >= 0.9
    assert np.linalg.det(result.transform[:3, :3]) == pytest.approx(1.0, abs=1e-10)
    # The orientation is what the calibration is after, so it has to come back too.
    recovered, _ = up_axis(result.transform)
    assert recovered == pytest.approx(up_axis(expected)[0], abs=0.5)


def test_the_model_shell_is_the_model_in_the_print_frame(shell_scene):
    scene, model, _ = shell_scene
    moved, expected = apply_print_transform(scene, np.random.default_rng(11))

    model_shell, _ = split_supported_scene(moved)
    placed = trimesh.transform_points(np.asarray(model.vertices, dtype=np.float64), expected)

    np.testing.assert_allclose(
        model_shell.vertices.mean(axis=0), placed.mean(axis=0), atol=1e-9
    )
    # Its extents are the model's own extents in the print frame: the supports are thin,
    # so they cannot widen it, and a turned block does not keep its own extents.
    np.testing.assert_allclose(
        model_shell.extents, placed.max(axis=0) - placed.min(axis=0), atol=1e-9
    )
    assert model_shell.bounds[0, 2] > 0.0
    assert expected[2, 3] != 0.0


def test_the_model_sits_above_the_supports(shell_scene):
    scene, _, _ = shell_scene

    model_shell, support_shells = split_supported_scene(scene)

    # The underside of the model is the top of the scene: the supports hang from it.
    assert model_shell.bounds[0, 2] == pytest.approx(UNDERSIDE_Z)
    for shell in support_shells:
        assert shell.bounds[1, 2] == pytest.approx(UNDERSIDE_Z)
        assert shell.bounds[0, 2] == pytest.approx(0.0)


# --------------------------------------------------------------------------------
# What the calibration run makes of the shells
# --------------------------------------------------------------------------------


def test_the_summary_counts_tips_by_class_and_flags_an_unsplit_pair():
    records = [
        {
            "expert_support_count": 2,
            "support_structures": 2,
            "support_structures_on_plate": 2,
            "support_base_structures": 0,
            "supports_separate": True,
            "oriented_stl": None,
            "shell_tips_by_diameter_mm": {"0.1": 1, "0.6": 1},
        },
        {
            # A file whose supports are welded to the model splits into one shell and
            # measures no tips at all: the summary has to say so rather than look empty.
            "expert_support_count": 0,
            "support_structures": 0,
            "support_structures_on_plate": 0,
            "support_base_structures": 0,
            "supports_separate": False,
            "oriented_stl": "oriented/syn002.stl",
            "shell_tips_by_diameter_mm": {},
        },
    ]

    totals = summarise(records)

    assert totals["tips"] == 2
    assert totals["structures"] == 2
    assert totals["separate"] == 1
    assert totals["oriented"] == 1
    assert totals["tip_classes"] == "0.1x1,0.6x1"


def test_the_record_of_a_shell_scene_holds_counts_only(model_and_supports):
    _, model_shell, support_shells, radii = model_and_supports

    record = measure_shells(model_shell, support_shells, np.random.default_rng(5)).as_record()

    assert record["scene_shells"] == 1 + len(radii)
    assert record["shell_tips"] == len(radii)
    assert record["shell_tip_diameter_median_mm"] == pytest.approx(0.3)
    assert sum(record["shell_tips_by_diameter_mm"].values()) == len(radii)
    # Nothing that says where anything is: only counts, sizes and distances go in.
    for value in record.values():
        assert isinstance(value, (int, float, str, dict))
        assert not isinstance(value, list)