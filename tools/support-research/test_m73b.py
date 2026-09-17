from __future__ import annotations

import numpy as np
import pytest
import trimesh

from register import register
from separate import separate_supports, separate_supports_scaled
from synth import apply_print_transform, build_model, build_supports, remesh


@pytest.mark.parametrize("stage", ["original", "transformed", "remeshed"])
def test_known_split_at_each_geometry_stage(stage):
    rng = np.random.default_rng(2026)
    model = build_model(rng)
    supports, _ = build_supports(model, rng)
    scene_model = model.copy()
    transform = np.eye(4)
    if stage != "original":
        scene_model, transform = apply_print_transform(scene_model, rng)
        supports, _ = apply_print_transform(supports, rng)
    if stage == "remeshed":
        scene_model = remesh(scene_model.subdivide(), rng)
        supports = remesh(supports, rng)
    scene = trimesh.util.concatenate([scene_model, supports])
    split = separate_supports(model, scene, transform, epsilon=0.3)
    known_support = np.arange(len(scene.faces)) >= len(scene_model.faces)
    np.testing.assert_array_equal(split.support_mask, known_support)
    model_max = split.centroid_distances[~known_support].max()
    support_min = split.centroid_distances[known_support].min()
    assert model_max < 0.3 < support_min
    print(f"stage={stage} model_max={model_max:.6f} support_min={support_min:.6f}")


def test_separation_after_registration_and_remeshing():
    rng = np.random.default_rng(2026)
    model = build_model(rng)
    supports, _ = build_supports(model, rng)
    moved_model, expected = apply_print_transform(model, rng)
    moved_supports, _ = apply_print_transform(supports, rng)
    subdivided_model = moved_model.subdivide()
    remeshed_model = remesh(subdivided_model, rng)
    remeshed_supports = remesh(moved_supports, rng)
    assert len(remeshed_model.faces) < len(subdivided_model.faces)
    assert len(remeshed_model.faces) != len(model.faces)
    scene = trimesh.util.concatenate([remeshed_model, remeshed_supports])
    registration = register(model, scene, np.random.default_rng(2026))
    np.testing.assert_allclose(registration.transform, expected, atol=0.02)
    model_vertices = model.vertices.copy()
    scene_vertices = scene.vertices.copy()
    scene_faces = scene.faces.copy()
    split = separate_supports(model, scene, registration.transform, epsilon=0.3)
    known_support = np.arange(len(scene.faces)) >= len(remeshed_model.faces)
    np.testing.assert_array_equal(split.support_mask, known_support)
    np.testing.assert_array_equal(split.model_mask, ~known_support)
    assert split.centroid_distances[~known_support].max() < 0.3
    assert split.centroid_distances[known_support].min() > 0.3
    np.testing.assert_array_equal(model.vertices, model_vertices)
    np.testing.assert_array_equal(scene.vertices, scene_vertices)
    np.testing.assert_array_equal(scene.faces, scene_faces)
    print(
        f"model_faces={np.count_nonzero(split.model_mask)} "
        f"support_faces={np.count_nonzero(split.support_mask)} "
        f"accuracy={np.mean(split.support_mask == known_support):.3f}"
    )


def test_exact_surface_distance_threshold_and_batching():
    model = trimesh.Trimesh(
        vertices=[[0, 0, 0], [12, 0, 0], [0, 12, 0]],
        faces=[[0, 1, 2]],
        process=False,
    )
    heights = np.array([0.0, 0.125, 0.25, 0.5, -0.5])
    triangles = np.array(
        [[[1, 1, z], [2, 1, z], [1, 2, z]] for z in heights]
    )
    scene = trimesh.Trimesh(
        vertices=triangles.reshape(-1, 3),
        faces=np.arange(len(heights) * 3).reshape(-1, 3),
        process=False,
    )
    for batch_size in (1, 2, 32):
        split = separate_supports(model, scene, np.eye(4), 0.25, batch_size)
        np.testing.assert_allclose(split.centroid_distances, np.abs(heights))
        np.testing.assert_array_equal(
            split.support_mask, [False, False, False, True, True]
        )
    split = separate_supports(model, scene, np.eye(4), epsilon=0)
    np.testing.assert_array_equal(split.model_mask, [True, False, False, False, False])


def test_centroid_rule_for_contact_crossing_face():
    model = trimesh.creation.box(extents=[4, 4, 2])
    scene = trimesh.Trimesh(
        vertices=[[0, 0, -1], [0.5, 0, -2], [0, 0.5, -2]],
        faces=[[0, 1, 2]],
        process=False,
    )
    split = separate_supports(model, scene, np.eye(4), epsilon=0.3)
    np.testing.assert_allclose(split.centroid_distances, [2 / 3])
    assert split.support_mask.all()


@pytest.mark.parametrize("epsilon", [-1.0, np.nan, np.inf])
def test_invalid_epsilon(epsilon):
    mesh = trimesh.creation.box()
    with pytest.raises(ValueError, match="Epsilon"):
        separate_supports(mesh, mesh, np.eye(4), epsilon=epsilon)


@pytest.mark.parametrize("batch_size", [0, -1, 1.5, True])
def test_invalid_batch_size(batch_size):
    mesh = trimesh.creation.box()
    with pytest.raises(ValueError, match="Batch size"):
        separate_supports(mesh, mesh, np.eye(4), batch_size=batch_size)


@pytest.mark.parametrize(
    "transform",
    [
        np.eye(3),
        np.full((4, 4), np.nan),
        np.diag([2, 1, 1, 1]),
        np.diag([-1, 1, 1, 1]),
        np.diag([1, 1, 1, 2]),
    ],
)
def test_invalid_transform(transform):
    mesh = trimesh.creation.box()
    with pytest.raises(ValueError, match="Transform"):
        separate_supports(mesh, mesh, transform)


def test_empty_meshes():
    empty = trimesh.Trimesh()
    model = trimesh.creation.box()
    split = separate_supports(model, empty, np.eye(4))
    assert split.centroid_distances.shape == (0,)
    assert split.support_mask.shape == (0,)
    assert split.model_mask.shape == (0,)
    with pytest.raises(ValueError, match="Model must contain triangles"):
        separate_supports(empty, model, np.eye(4))


@pytest.mark.parametrize("invalid_model", [True, False])
def test_nonfinite_mesh(invalid_model):
    model = trimesh.creation.box()
    scene = model.copy()
    invalid = model if invalid_model else scene
    invalid.vertices[0, 0] = np.nan
    with pytest.raises(ValueError, match="Mesh vertices must be finite"):
        separate_supports(model, scene, np.eye(4))


# Tests for separate_supports_scaled
@pytest.mark.parametrize("stage", ["original", "transformed", "remeshed"])
def test_scaled_known_split_at_each_geometry_stage(stage):
    rng = np.random.default_rng(2026)
    model = build_model(rng)
    supports, _ = build_supports(model, rng)
    scene_model = model.copy()
    transform = np.eye(4)
    if stage != "original":
        scene_model, transform = apply_print_transform(scene_model, rng)
        supports, _ = apply_print_transform(supports, rng)
    if stage == "remeshed":
        scene_model = remesh(scene_model.subdivide(), rng)
        supports = remesh(supports, rng)
    scene = trimesh.util.concatenate([scene_model, supports])
    split = separate_supports_scaled(model, scene, transform, epsilon=0.3, rng=rng)
    known_support = np.arange(len(scene.faces)) >= len(scene_model.faces)
    np.testing.assert_array_equal(split.support_mask, known_support)
    model_max = split.centroid_distances[~known_support].max()
    support_min = split.centroid_distances[known_support].min()
    assert model_max < 0.3 < support_min


def test_scaled_separation_after_registration_and_remeshing():
    rng = np.random.default_rng(2026)
    model = build_model(rng)
    supports, _ = build_supports(model, rng)
    moved_model, expected = apply_print_transform(model, rng)
    moved_supports, _ = apply_print_transform(supports, rng)
    subdivided_model = moved_model.subdivide()
    remeshed_model = remesh(subdivided_model, rng)
    remeshed_supports = remesh(moved_supports, rng)
    scene = trimesh.util.concatenate([remeshed_model, remeshed_supports])
    registration = register(model, scene, np.random.default_rng(2026))
    np.testing.assert_allclose(registration.transform, expected, atol=0.02)
    split = separate_supports_scaled(model, scene, registration.transform, epsilon=0.3, rng=rng)
    known_support = np.arange(len(scene.faces)) >= len(remeshed_model.faces)
    np.testing.assert_array_equal(split.support_mask, known_support)
    np.testing.assert_array_equal(split.model_mask, ~known_support)
    assert split.centroid_distances[~known_support].max() < 0.3
    assert split.centroid_distances[known_support].min() > 0.3
    print(
        f"model_faces={np.count_nonzero(split.model_mask)} "
        f"support_faces={np.count_nonzero(split.support_mask)} "
        f"accuracy={np.mean(split.support_mask == known_support):.3f}"
    )


def test_scaled_exact_surface_distance_threshold():
    model = trimesh.Trimesh(
        vertices=[[0, 0, 0], [12, 0, 0], [0, 12, 0]],
        faces=[[0, 1, 2]],
        process=False,
    )
    heights = np.array([0.0, 0.125, 0.25, 0.5, -0.5])
    triangles = np.array(
        [[[1, 1, z], [2, 1, z], [1, 2, z]] for z in heights]
    )
    scene = trimesh.Trimesh(
        vertices=triangles.reshape(-1, 3),
        faces=np.arange(len(heights) * 3).reshape(-1, 3),
        process=False,
    )
    split = separate_supports_scaled(model, scene, np.eye(4), epsilon=0.25, rng=np.random.default_rng(42))
    # Support mask should be correct
    np.testing.assert_array_equal(
        split.support_mask, [False, False, False, True, True]
    )
    split = separate_supports_scaled(model, scene, np.eye(4), epsilon=0, rng=np.random.default_rng(42))
    np.testing.assert_array_equal(split.model_mask, [True, False, False, False, False])


def test_scaled_centroid_rule_for_contact_crossing_face():
    model = trimesh.creation.box(extents=[4, 4, 2])
    scene = trimesh.Trimesh(
        vertices=[[0, 0, -1], [0.5, 0, -2], [0, 0.5, -2]],
        faces=[[0, 1, 2]],
        process=False,
    )
    split = separate_supports_scaled(model, scene, np.eye(4), epsilon=0.3, rng=np.random.default_rng(42))
    # Distance should be approximately 2/3
    assert split.centroid_distances[0] > 0.5
    assert split.support_mask.all()


@pytest.mark.parametrize("epsilon", [-1.0, np.nan, np.inf])
def test_scaled_invalid_epsilon(epsilon):
    mesh = trimesh.creation.box()
    with pytest.raises(ValueError, match="Epsilon"):
        separate_supports_scaled(mesh, mesh, np.eye(4), epsilon=epsilon)


@pytest.mark.parametrize("spacing", [0.0, -1.0, np.nan, np.inf])
def test_scaled_invalid_sample_spacing(spacing):
    mesh = trimesh.creation.box(extents=[1.0, 1.0, 1.0])
    with pytest.raises(ValueError, match="Sample spacing"):
        separate_supports_scaled(mesh, mesh, np.eye(4), sample_spacing=spacing)


@pytest.mark.parametrize(
    "kwargs, message",
    [
        ({"max_samples": 0}, "Max samples"),
        ({"max_samples": 1.5}, "Max samples"),
        ({"chunk_size": 0}, "Chunk size"),
        ({"batch_size": True}, "Batch size"),
    ],
)
def test_scaled_invalid_sizes(kwargs, message):
    mesh = trimesh.creation.box(extents=[1.0, 1.0, 1.0])
    with pytest.raises(ValueError, match=message):
        separate_supports_scaled(mesh, mesh, np.eye(4), **kwargs)


@pytest.mark.parametrize(
    "transform",
    [
        np.eye(3),
        np.full((4, 4), np.nan),
        np.diag([2, 1, 1, 1]),
        np.diag([-1, 1, 1, 1]),
        np.diag([1, 1, 1, 2]),
    ],
)
def test_scaled_invalid_transform(transform):
    mesh = trimesh.creation.box()
    with pytest.raises(ValueError, match="Transform"):
        separate_supports_scaled(mesh, mesh, transform)


def test_scaled_empty_meshes():
    empty = trimesh.Trimesh()
    model = trimesh.creation.box()
    split = separate_supports_scaled(model, empty, np.eye(4), rng=np.random.default_rng(42))
    assert split.centroid_distances.shape == (0,)
    assert split.support_mask.shape == (0,)
    assert split.model_mask.shape == (0,)
    with pytest.raises(ValueError, match="Model must contain triangles"):
        separate_supports_scaled(empty, model, np.eye(4), rng=np.random.default_rng(42))


@pytest.mark.parametrize("invalid_model", [True, False])
def test_scaled_nonfinite_mesh(invalid_model):
    model = trimesh.creation.box()
    scene = model.copy()
    invalid = model if invalid_model else scene
    invalid.vertices[0, 0] = np.nan
    with pytest.raises(ValueError, match="Mesh vertices must be finite"):
        separate_supports_scaled(model, scene, np.eye(4), rng=np.random.default_rng(42))


def test_scaled_matches_exact_on_synthetic():
    """Verify scaled method produces identical support classification to exact method on synthetic tests."""
    rng = np.random.default_rng(2026)
    model = build_model(rng)
    supports, _ = build_supports(model, rng)
    moved_model, transform = apply_print_transform(model, rng)
    moved_supports, _ = apply_print_transform(supports, rng)
    subdivided_model = moved_model.subdivide()
    remeshed_model = remesh(subdivided_model, rng)
    remeshed_supports = remesh(moved_supports, rng)
    scene = trimesh.util.concatenate([remeshed_model, remeshed_supports])
    
    exact = separate_supports(model, scene, transform, epsilon=0.3)
    scaled = separate_supports_scaled(model, scene, transform, epsilon=0.3, rng=rng)
    
    # Support masks must be identical
    np.testing.assert_array_equal(scaled.support_mask, exact.support_mask)
    np.testing.assert_array_equal(scaled.model_mask, exact.model_mask)


def test_scaled_sparse_sampling_still_matches_exact():
    """A coarse sample spacing must not mislabel model faces as supports.

    Nearest-sample distance only bounds the true distance from above, so the
    support side needs a safety margin. With a deliberately coarse spacing the
    classification must still agree with the exact method.
    """
    rng = np.random.default_rng(7)
    model = build_model(rng)
    supports, _ = build_supports(model, rng)
    scene = trimesh.util.concatenate([model, supports])
    exact = separate_supports(model, scene, np.eye(4), epsilon=0.3)
    coarse = separate_supports_scaled(
        model, scene, np.eye(4), epsilon=0.3, sample_spacing=2.0, rng=rng
    )
    np.testing.assert_array_equal(coarse.support_mask, exact.support_mask)


def test_scaled_chunking_does_not_change_result():
    rng = np.random.default_rng(11)
    model = build_model(rng)
    supports, _ = build_supports(model, rng)
    scene = trimesh.util.concatenate([model, supports])
    whole = separate_supports_scaled(
        model, scene, np.eye(4), epsilon=0.3, rng=np.random.default_rng(3)
    )
    chunked = separate_supports_scaled(
        model, scene, np.eye(4), epsilon=0.3, chunk_size=7, batch_size=3,
        rng=np.random.default_rng(3),
    )
    np.testing.assert_array_equal(chunked.support_mask, whole.support_mask)
    np.testing.assert_allclose(chunked.centroid_distances, whole.centroid_distances)


def test_scaled_sample_count_is_bounded_by_max_samples():
    """Sample count follows surface area and the cap, not the face count."""
    rng = np.random.default_rng(5)
    model = build_model(rng)
    supports, _ = build_supports(model, rng)
    scene = trimesh.util.concatenate([model, supports])
    capped = separate_supports_scaled(
        model, scene, np.eye(4), epsilon=0.3, max_samples=1_000, rng=rng
    )
    exact = separate_supports(model, scene, np.eye(4), epsilon=0.3)
    np.testing.assert_array_equal(capped.support_mask, exact.support_mask)
