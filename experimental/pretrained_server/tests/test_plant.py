"""Checks for the Python port of the plant and for config versioning.

Run from experimental/pretrained_server:  python -m pytest tests
"""

import numpy as np
import pytest

from vaesynth_exp import config as cfg
from vaesynth_exp.plant import CENTRE, COUNT, SAMPLE_NEUTRAL, MutationSettings, Rng, grow, is_origin


def test_rng_matches_mulberry32_reference():
    # First values of mulberry32 for seed 1, printed by the C++ vae::Rng code.
    rng = Rng(1)
    values = [float(rng.uniform()) for _ in range(3)]
    assert values == pytest.approx([0.6270739, 0.0027357, 0.5274470], abs=1e-6)


def test_plant_size_and_shape():
    for seed in range(1, 50):
        nodes = grow(0.5, seed)
        assert 46 <= len(nodes) <= 121          # seed + 45..120 buds
        assert sum(n.depth == 1 for n in nodes) == 3
        assert max(n.depth for n in nodes) == 4


def test_deterministic():
    d = np.linspace(-1, 1, COUNT).astype(np.float32)
    a = grow(0.7, 1234, d)
    b = grow(0.7, 1234, d)
    assert all(np.array_equal(x.coords, y.coords) for x, y in zip(a, b))


def test_wild_minimum_and_root():
    nodes = grow(0.0, 7)
    assert nodes[0].coords.tolist() == pytest.approx(SAMPLE_NEUTRAL)
    assert is_origin(nodes[0].coords)
    # Wild 0 still moves buds away from the seed (minimum 0.25).
    assert np.abs(nodes[1].coords - CENTRE).max() > 0


def test_children_of_seed_start_at_centre():
    nodes = grow(1.0, 3, settings=MutationSettings(sigma=0.0, lean_k=0.0))
    for n in nodes[1:]:
        assert n.coords == pytest.approx(CENTRE)


def test_lean_pushes_along_direction():
    d = np.zeros(COUNT, np.float32)
    d[0] = 1.0
    nodes = grow(1.0, 9, d, MutationSettings(sigma=0.0, lean_k=1.0))
    for n in nodes[1:4]:
        assert np.sign(n.coords[0] - 0.5) == np.sign(n.lean) or abs(n.lean) < 1e-6


def test_config_versions_never_overwrite(tmp_path, monkeypatch):
    monkeypatch.setattr(cfg, "CONFIG_DIR", tmp_path)
    rng = np.random.default_rng(0)
    means = rng.standard_normal((40, 64))
    means[:, 0] *= 5  # a dominant direction that also carries brightness
    centroids = 1000 * 2 ** (means[:, 0] + 0.1 * rng.standard_normal(40))
    built = cfg.build(means, centroids)
    first = cfg.save(built, "test", ["a.wav"])
    second = cfg.save(built, "test", ["a.wav"])
    assert (first.name, second.name) == ("config_v001.json", "config_v002.json")
    loaded = cfg.load()
    assert loaded["version"] == 2
    assert loaded["basis"].shape == (64, COUNT)
    assert np.allclose(cfg.offset(loaded, CENTRE), 0)
    assert loaded["stats"]["brightness_r2"] > 0.5
