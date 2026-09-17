from __future__ import annotations

import json

import numpy as np
import pytest
import trimesh

from contacts import describe_contacts, write_debug_mesh
from register import register
from separate import separate_supports_scaled
from synth import (
    TIP_RADIUS,
    UNDERSIDE_Z,
    apply_print_transform,
    build_model,
    build_supports,
    remesh,
)


def _scene(stage: str, rng: np.random.Generator):
    """Model, scene, transform and the known support-face mask for a stage."""
    model = build_model(rng)
    supports, known = build_supports(model, rng)
    scene_model, transform = model.copy(), np.eye(4)
    if stage != "original":
        scene_model, transform = apply_print_transform(scene_model, rng)
        supports, _ = apply_print_transform(supports, rng)
    if stage == "remeshed":
        scene_model = remesh(scene_model.subdivide(), rng)
        supports = remesh(supports, rng)
    scene = trimesh.util.concatenate([scene_model, supports])
    mask = np.arange(len(scene.faces)) >= len(scene_model.faces)
    return model, scene, transform, mask, known


def _matched(report, known) -> list[tuple]:
    """Pair each known contact with the nearest described one."""
    pairs = []
    for expected in known:
        target = np.asarray(expected["position"], dtype=np.float64)
        best = min(
            report.contacts,
            key=lambda c: float(np.linalg.norm(np.asarray(c.position_model) - target)),
        )
        pairs.append((expected, best, float(np.linalg.norm(np.asarray(best.position_model) - target))))
    return pairs


@pytest.mark.parametrize("stage", ["original", "transformed", "remeshed"])
def test_every_known_contact_is_described(stage):
    rng = np.random.default_rng(2026)
    model, scene, transform, mask, known = _scene(stage, rng)
    report = describe_contacts(model, scene, transform, mask)

    assert len(report.contacts) == len(known)
    for expected, found, distance in _matched(report, known):
        assert distance < 1.0, f"{stage}: contact {expected['position']} off by {distance:.3f} mm"
        # The tips meet the flat underside, whose outward normal points down.
        assert np.dot(found.normal_model, expected["normal"]) > 0.9
        assert 0.5 * TIP_RADIUS * 2 <= found.diameter <= 2.5 * TIP_RADIUS * 2
        assert found.vertex_count >= 3


def test_contacts_are_distinct_and_on_the_underside():
    rng = np.random.default_rng(7)
    model, scene, transform, mask, known = _scene("original", rng)
    report = describe_contacts(model, scene, transform, mask)

    positions = np.array([c.position_model for c in report.contacts])
    assert len(np.unique(np.round(positions, 3), axis=0)) == len(known)
    np.testing.assert_allclose(positions[:, 2], UNDERSIDE_Z, atol=0.2)
    # Underside contacts face straight down, so the overhang angle is ~0.
    assert max(c.overhang_deg for c in report.contacts) < 15.0
    assert all(c.serves_local_minimum for c in report.contacts)
    assert all(c.penetration >= 0.0 for c in report.contacts)


def test_heights_above_plate_and_structure():
    rng = np.random.default_rng(11)
    model, scene, transform, mask, _ = _scene("original", rng)
    report = describe_contacts(model, scene, transform, mask)

    assert report.plate_z == pytest.approx(-0.8, abs=0.01)  # pad sits below z=0
    for contact in report.contacts:
        assert contact.height_above_plate == pytest.approx(UNDERSIDE_Z - report.plate_z, abs=0.3)
    # Pillars, tips and pad are fused into one support body reaching the plate.
    assert len(report.components) >= 1
    assert any(component["touches_plate"] for component in report.components)
    assert sum(component["contacts"] for component in report.components) == len(report.contacts)


def test_contacts_follow_the_registered_transform():
    rng = np.random.default_rng(2026)
    model, scene, expected_transform, mask, known = _scene("remeshed", rng)
    registration = register(model, scene, np.random.default_rng(2026))
    np.testing.assert_allclose(registration.transform, expected_transform, atol=0.02)
    split = separate_supports_scaled(
        model, scene, registration.transform, epsilon=0.3, rng=np.random.default_rng(2026)
    )
    report = describe_contacts(model, scene, registration.transform, split.support_mask)

    assert len(report.contacts) == len(known)
    for _, found, distance in _matched(report, known):
        assert distance < 1.0
        # Scene-frame position must be the model-frame one carried through the transform.
        expected_scene = trimesh.transform_points(
            np.asarray(found.position_model)[None], registration.transform
        )[0]
        np.testing.assert_allclose(found.position_scene, expected_scene, atol=1e-6)


def test_report_writes_json_and_debug_mesh(tmp_path):
    rng = np.random.default_rng(3)
    model, scene, transform, mask, known = _scene("original", rng)
    report = describe_contacts(model, scene, transform, mask)

    json_path = report.to_json(tmp_path / "out" / "contacts.json")
    payload = json.loads(json_path.read_text(encoding="utf-8"))
    assert len(payload["contacts"]) == len(known)
    assert payload["plate_z"] == pytest.approx(report.plate_z)
    assert {"position_scene", "diameter", "overhang_deg"} <= payload["contacts"][0].keys()

    mesh_path = write_debug_mesh(scene, mask, report, tmp_path / "out" / "debug.ply")
    debug = trimesh.load(mesh_path, process=False)
    assert len(debug.faces) > len(scene.faces)  # markers added


def test_no_supports_gives_empty_report():
    rng = np.random.default_rng(5)
    model = build_model(rng)
    scene = model.copy()
    report = describe_contacts(model, scene, np.eye(4), np.zeros(len(scene.faces), dtype=bool))
    assert report.contacts == []
    assert report.components == []


@pytest.mark.parametrize(
    "kwargs, message",
    [
        ({"cluster_radius": 0.0}, "Radii"),
        ({"minimum_radius": -1.0}, "Radii"),
        ({"curvature_radius": np.nan}, "Radii"),
        ({"contact_gap": -0.1}, "Epsilon"),
    ],
)
def test_invalid_parameters(kwargs, message):
    rng = np.random.default_rng(5)
    model, scene, transform, mask, _ = _scene("original", rng)
    with pytest.raises(ValueError, match=message):
        describe_contacts(model, scene, transform, mask, **kwargs)


def test_support_mask_length_is_checked():
    rng = np.random.default_rng(5)
    model, scene, transform, _, _ = _scene("original", rng)
    with pytest.raises(ValueError, match="Support mask"):
        describe_contacts(model, scene, transform, np.zeros(3, dtype=bool))
