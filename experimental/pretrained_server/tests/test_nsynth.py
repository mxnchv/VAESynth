"""NSynth helpers on a tiny fake split (no download)."""

import json
import tarfile

import numpy as np
import soundfile as sf

from vaesynth_exp import nsynth

FAMILIES = ["bass", "brass", "guitar", "mallet"]


def make_archive(tmp_path):
    root = tmp_path / "build" / "nsynth-test"
    (root / "audio").mkdir(parents=True)
    meta = {}
    for f, family in enumerate(FAMILIES):
        for pitch in (40, 60, 62, 90):
            for velocity in (50, 100):
                name = f"{family}_acoustic_{f:03d}-{pitch:03d}-{velocity:03d}"
                meta[name] = {"pitch": pitch, "velocity": velocity, "instrument_family_str": family,
                              "instrument_source_str": "acoustic", "sample_rate": 16000}
                t = np.arange(1600) / 16000
                tone = 0.3 * np.sin(2 * np.pi * 440 * 2 ** ((pitch - 69) / 12) * (f + 1) * t)
                sf.write(root / "audio" / f"{name}.wav", tone.astype(np.float32), 16000)
    (root / "examples.json").write_text(json.dumps(meta))
    archive = tmp_path / "nsynth-test.jsonwav.tar.gz"
    with tarfile.open(archive, "w:gz") as tar:
        tar.add(root, arcname="nsynth-test")
    return archive


def test_fetch_from_archive_and_select(tmp_path):
    folder = nsynth.fetch("test", tmp_path / "data", make_archive(tmp_path))
    notes = nsynth.examples(folder)
    assert len(notes) == len(FAMILIES) * 8
    assert all(n["path"].exists() for n in notes)

    picked = nsynth.balanced(notes, 8)
    assert len(picked) == 8
    assert {n["instrument_family_str"] for n in picked} == set(FAMILIES)   # spread over families
    assert all(36 <= n["pitch"] <= 84 and n["velocity"] in (75, 100, 127) for n in picked)

    seeds = nsynth.seeds_per_family(notes, 1, 60)
    assert [(n["instrument_family_str"], n["pitch"], n["velocity"]) for n in seeds] == [(f, 60, 100) for f in FAMILIES]

    # A second call reuses the extracted folder.
    assert nsynth.fetch("test", tmp_path / "data") == folder
