from __future__ import annotations

import numpy as np
import pytest
import trimesh

from register import (
    MeshDistanceQuery,
    RegistrationError,
    _sample_surface,
    coarse_alignment,
    print_tilt,
    register,
    residual_stats,
)
from synth import (
    UNDERSIDE_Z,
    apply_print_transform,
    build_model,
    build_supports,
    remesh,
)


def transform_error(estimated: np.ndarray, expected: np.ndarray) -> tuple[float, float]:
    rotation = estimated[:3, :3].T @ expected[:3, :3]
    angle = float(
        np.degrees(np.arccos(np.clip((np.trace(rotation) - 1.0) / 2.0, -1.0, 1.0)))
    )
    translation = float(np.linalg.norm(estimated[:3, 3] - expected[:3, 3]))
    return angle, translation


@pytest.fixture(scope="module")
def synthetic_pair():
    rng = np.random.default_rng(2026)
    model = build_model(rng)
    supports, contacts = build_supports(model, rng)
    scene = trimesh.util.concatenate([model, supports])
    moved, expected = apply_print_transform(scene, rng)
    coarser = remesh(moved, rng)
    return model, supports, contacts, moved, coarser, expected


def test_registration_recovers_decimated_supported_shape(synthetic_pair):
    model, _, _, _, scene, expected = synthetic_pair
    result = register(model, scene, np.random.default_rng(2026))
    angle, translation = transform_error(result.transform, expected)
    print(f"rotation_error={angle:.6f}deg translation_error={translation:.6f}mm")
    print(f"inlier_fraction={result.inlier_fraction:.4f} rms={result.rms:.6f}mm p95={result.p95:.6f}mm")
    assert angle < 1.0
    assert translation < 0.2
    assert result.inlier_fraction >= 0.9
    assert result.rms < 0.15
    assert result.p95 < 0.3
    assert result.converged
    assert print_tilt(result.transform) == pytest.approx(print_tilt(expected), abs=1.0)
    assert np.linalg.det(result.transform[:3, :3]) == pytest.approx(1.0, abs=1e-10)
    stats = residual_stats(model, scene, result.transform)
    assert stats["inlier_fraction"] >= 0.9
    assert stats["rms"] < 0.15
    assert stats["p95"] < 0.3


def test_remeshing_is_coarser(synthetic_pair):
    _, _, _, original, coarser, _ = synthetic_pair
    assert len(coarser.faces) < len(original.faces) * 0.8
    assert len(coarser.vertices) < len(original.vertices)


def test_tips_touch_underside_without_penetration(synthetic_pair):
    model, supports, contacts, _, _, _ = synthetic_pair
    assert len(contacts) == 3
    positions = np.array([contact["position"] for contact in contacts])
    closest, distances, faces = MeshDistanceQuery(model).query(positions)
    np.testing.assert_allclose(distances, 0.0, atol=1e-10)
    np.testing.assert_allclose(closest, positions, atol=1e-10)
    np.testing.assert_allclose(model.face_normals[faces], [[0, 0, -1]] * 3, atol=1e-10)
    assert supports.bounds[1, 2] == pytest.approx(UNDERSIDE_Z, abs=1e-10)
    assert model.bounds[0, 2] == pytest.approx(UNDERSIDE_Z, abs=1e-10)
    pieces = supports.split(only_watertight=False)
    assert len(pieces) == 7
    for index, contact in enumerate(contacts):
        tip = pieces[index * 2]
        pillar = pieces[index * 2 + 1]
        apex = tip.vertices[np.argmax(tip.vertices[:, 2])]
        np.testing.assert_allclose(apex, contact["position"], atol=1e-10)
        assert tip.bounds[0, 2] == pytest.approx(pillar.bounds[1, 2], abs=1e-10)
        assert pillar.bounds[0, 2] == pytest.approx(0.0, abs=1e-10)


@pytest.mark.parametrize("left_handed", [False, True])
def test_coarse_candidates_are_four_proper_rotations(synthetic_pair, monkeypatch, left_handed):
    model, _, _, _, scene, _ = synthetic_pair
    original = np.linalg.eigh
    calls = 0

    def eigenvectors(matrix):
        nonlocal calls
        values, vectors = original(matrix)
        vectors[:, 0] *= np.linalg.det(vectors)
        if left_handed and calls == 0:
            vectors[:, 0] *= -1.0
        calls += 1
        return values, vectors

    monkeypatch.setattr(np.linalg, "eigh", eigenvectors)
    candidates = coarse_alignment(model, scene, np.random.default_rng(3))
    assert len(candidates) == 4
    rng = np.random.default_rng(3)
    source_center = _sample_surface(model, 4000, rng).mean(axis=0)
    target_center = _sample_surface(scene, 4000, rng).mean(axis=0)
    for transform in candidates:
        rotation = transform[:3, :3]
        assert np.linalg.det(rotation) == pytest.approx(1.0, abs=1e-10)
        np.testing.assert_allclose(rotation.T @ rotation, np.eye(3), atol=1e-10)
        np.testing.assert_allclose(rotation @ source_center + transform[:3, 3], target_center, atol=1e-10)


def test_mirrored_shape_is_rejected(synthetic_pair):
    model, _, _, _, scene, _ = synthetic_pair
    mirrored = scene.copy()
    reflection = np.eye(4)
    reflection[0, 0] = -1.0
    mirrored.apply_transform(reflection)
    with pytest.raises(RegistrationError) as error:
        register(model, mirrored, np.random.default_rng(2026))
    assert error.value.result.inlier_fraction < 0.9
    assert np.linalg.det(error.value.result.transform[:3, :3]) == pytest.approx(1.0, abs=1e-10)


def test_wrong_alignment_is_rejected(synthetic_pair, monkeypatch):
    model, _, _, _, scene, expected = synthetic_pair
    wrong = expected.copy()
    wrong[:3, 3] += 100.0
    monkeypatch.setattr("register.coarse_alignment", lambda *args: [wrong])
    with pytest.raises(RegistrationError) as error:
        register(model, scene, np.random.default_rng(2))
    assert error.value.result.inlier_fraction == pytest.approx(0.0)
