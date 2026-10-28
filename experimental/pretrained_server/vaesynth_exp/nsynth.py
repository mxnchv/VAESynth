"""NSynth as the sound source for the listening experiment.

NSynth (Magenta, CC BY 4.0): one-shot notes of 4 s at 16 kHz, with metadata
(instrument family, source, pitch, velocity). Splits: train 289,205 notes,
valid 12,678, test 4,096 (TFDS catalogue). The test split is the smallest
download and is plenty for this experiment.

Caveat for listening: at 16 kHz nothing is above 8 kHz. The autoencoder works
at 44.1 kHz, so NSynth notes are upsampled and sound duller than the beta's
seeds; the brightness direction is learned only from content below 8 kHz.
Attribution is required if anything derived from NSynth is ever distributed.
"""

from __future__ import annotations

import json
import random
import tarfile
import urllib.request
from collections import defaultdict
from pathlib import Path

# Not verified from this session: if it fails, download the archive from
# https://magenta.tensorflow.org/datasets/nsynth and set NSYNTH_ARCHIVE.
URL = "http://download.magenta.tensorflow.org/datasets/nsynth/nsynth-{split}.jsonwav.tar.gz"


def fetch(split: str, dest: str | Path, archive: str | Path | None = None) -> Path:
    """Downloads (unless `archive` is given) and extracts a split. Returns its folder."""
    dest = Path(dest)
    folder = dest / f"nsynth-{split}"
    if (folder / "examples.json").exists():
        return folder
    dest.mkdir(parents=True, exist_ok=True)
    if archive is None:
        archive = dest / f"nsynth-{split}.jsonwav.tar.gz"
        if not Path(archive).exists():
            print("Descargando", URL.format(split=split), "…")
            urllib.request.urlretrieve(URL.format(split=split), archive)
    with tarfile.open(archive) as tar:
        try:
            tar.extractall(dest, filter="data")
        except TypeError:  # Python without extraction filters
            tar.extractall(dest)
    return folder


def examples(folder: str | Path) -> list[dict]:
    """Metadata of every note, with the path of its WAV added as `path`."""
    folder = Path(folder)
    with open(folder / "examples.json", encoding="utf-8") as f:
        meta = json.load(f)
    notes = []
    for name, info in meta.items():
        path = folder / "audio" / f"{name}.wav"
        if path.exists():
            notes.append({**info, "name": name, "path": path})
    return notes


def balanced(notes: list[dict], count: int, pitches=(36, 84), velocities=(75, 100, 127), seed: int = 0) -> list[dict]:
    """Up to `count` notes spread evenly over instrument families, in a pitch range."""
    pool = [n for n in notes if pitches[0] <= n["pitch"] <= pitches[1] and n["velocity"] in velocities]
    by_family = defaultdict(list)
    for n in pool:
        by_family[n["instrument_family_str"]].append(n)
    rng = random.Random(seed)
    for group in by_family.values():
        rng.shuffle(group)
    picked, families = [], sorted(by_family)
    while len(picked) < count and any(by_family[f] for f in families):
        for f in families:
            if by_family[f] and len(picked) < count:
                picked.append(by_family[f].pop())
    return picked


def seeds_per_family(notes: list[dict], per_family: int = 1, pitch: int = 60, seed: int = 1) -> list[dict]:
    """Seeds for the plants: per family, notes at (or nearest to) `pitch`, velocity 100 if possible."""
    by_family = defaultdict(list)
    for n in notes:
        by_family[n["instrument_family_str"]].append(n)
    rng = random.Random(seed)
    chosen = []
    for family in sorted(by_family):
        group = by_family[family]
        rng.shuffle(group)
        group.sort(key=lambda n: (abs(n["pitch"] - pitch), n["velocity"] != 100))
        chosen.extend(group[:per_family])
    return chosen
