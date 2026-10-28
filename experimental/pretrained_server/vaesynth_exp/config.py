"""The engine's configuration: basis B, scale s and brightness direction d.

  offset = B @ ((coords - 0.5) * s)        (64 latent channels)
  bud latent[:, t] = seed latent[:, t] + offset   for every frame t

B: the 11 main directions (PCA) of the time-averaged latents of a sample of
   the library.
s: per direction, so that coords 0 and 1 sit `spread` standard deviations
   either side of the seed.
d: unit vector in coordinate space along which sounds get brighter (linear fit
   of log2 spectral centroid). The engine pushes buds along it with `lean`.

Configs are versioned: each new one is saved as configs/config_vNNN.json and
an existing file is never overwritten.
"""

from __future__ import annotations

import hashlib
import json
import re
from datetime import datetime, timezone
from pathlib import Path

import numpy as np

from .plant import COUNT

CONFIG_DIR = Path(__file__).resolve().parent.parent / "configs"
_PATTERN = re.compile(r"config_v(\d{3})\.json$")


def build(mean_latents: np.ndarray, centroids_hz: np.ndarray, spread: float = 3.0) -> dict:
    """mean_latents: (N, 64) time-averaged latents. centroids_hz: (N,)."""
    m = np.asarray(mean_latents, np.float64)
    centre = m.mean(axis=0)
    _, sing, vt = np.linalg.svd(m - centre, full_matrices=False)
    k = min(COUNT, len(sing))
    basis = np.zeros((m.shape[1], COUNT))                 # (64, 11), orthonormal columns
    basis[:, :k] = vt[:k].T                               # fewer sounds than 11: extra columns stay 0
    std = np.zeros(COUNT)
    std[:k] = sing[:k] / np.sqrt(max(len(m) - 1, 1))
    std = np.maximum(std, 1e-6)                           # a direction without variation stays still
    scale = 2.0 * spread * std                            # coords 0..1 -> -spread..+spread std

    coords = ((m - centre) @ basis) / scale               # each sound in coordinate units
    target = np.log2(np.maximum(np.asarray(centroids_hz, np.float64), 1.0))
    design = np.c_[coords, np.ones(len(coords))]
    weights, *_ = np.linalg.lstsq(design, target, rcond=None)
    w = weights[:COUNT]
    prediction = design @ weights
    r2 = 1.0 - np.sum((target - prediction) ** 2) / max(np.sum((target - target.mean()) ** 2), 1e-12)
    norm = float(np.linalg.norm(w))
    direction = w / norm if norm > 0 else np.zeros(COUNT)

    variance = np.maximum(sing ** 2, 1e-30)
    return {
        "basis": basis.tolist(),
        "scale": scale.tolist(),
        "direction": direction.tolist(),
        "spread_std": spread,
        "stats": {
            "sounds": int(len(m)),
            "explained_variance_11": float(variance[:COUNT].sum() / variance.sum()),
            "brightness_r2": float(r2),
            # Octaves of centroid gained moving one coordinate unit along d.
            "brightness_octaves_per_unit": norm,
        },
    }


def save(config: dict, model_name: str, sound_paths: list[str], notes: str = "") -> Path:
    """Saves as the next config_vNNN.json. Never overwrites."""
    CONFIG_DIR.mkdir(parents=True, exist_ok=True)
    taken = [int(m.group(1)) for p in CONFIG_DIR.iterdir() if (m := _PATTERN.search(p.name))]
    version = max(taken, default=0) + 1
    path = CONFIG_DIR / f"config_v{version:03d}.json"
    record = {
        "version": version,
        "created_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "model": model_name,
        "sounds_hash": hashlib.sha256("\n".join(sorted(sound_paths)).encode()).hexdigest()[:16],
        "notes": notes,
        **config,
    }
    with open(path, "x", encoding="utf-8") as f:  # "x": fails instead of overwriting
        json.dump(record, f, indent=1)
    return path


def load(path: str | Path | None = None) -> dict:
    """A given config file, or the newest one."""
    if path is None:
        found = sorted(p for p in CONFIG_DIR.glob("config_v*.json") if _PATTERN.search(p.name))
        if not found:
            raise FileNotFoundError(f"No config in {CONFIG_DIR}")
        path = found[-1]
    with open(path, encoding="utf-8") as f:
        record = json.load(f)
    record["basis"] = np.asarray(record["basis"], np.float32)
    record["scale"] = np.asarray(record["scale"], np.float32)
    record["direction"] = np.asarray(record["direction"], np.float32)
    return record


def offset(config: dict, coords: np.ndarray) -> np.ndarray:
    """Latent offset (64,) for a node's coordinates."""
    c = (np.asarray(coords, np.float32) - 0.5) * config["scale"]
    return (config["basis"] @ c).astype(np.float32)
