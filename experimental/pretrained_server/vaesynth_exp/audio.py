"""Loading sounds, formats, and the two measures used while listening."""

from __future__ import annotations

from math import gcd
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

from .latent import SAMPLE_RATE

EXTENSIONS = {".wav", ".aif", ".aiff", ".flac", ".ogg", ".mp3", ".caf"}
MAX_SEED_SECONDS = 10.0  # vae::maxSeedSeconds
MAX_BUD_SECONDS = 4.5    # DspVariationEngine::maxBudSeconds


def find_sounds(folder: str | Path) -> list[Path]:
    return sorted(p for p in Path(folder).rglob("*") if p.suffix.lower() in EXTENSIONS and not p.name.startswith("."))


def load(path: str | Path) -> tuple[np.ndarray, int]:
    """Returns (channels, samples) float32 at the file's own rate, at most 10 s."""
    data, rate = sf.read(str(path), dtype="float32", always_2d=True)
    data = data[: int(MAX_SEED_SECONDS * rate)].T
    return np.ascontiguousarray(data), rate


def resample(audio: np.ndarray, rate_from: int, rate_to: int = SAMPLE_RATE) -> np.ndarray:
    if rate_from == rate_to:
        return audio.astype(np.float32)
    g = gcd(rate_from, rate_to)
    return resample_poly(audio, rate_to // g, rate_from // g, axis=-1).astype(np.float32)


def to_model_input(audio: np.ndarray, rate: int) -> np.ndarray:
    """Any file -> stereo (2, N) at 44.1 kHz, as the autoencoder expects."""
    x = resample(audio, rate)
    if x.shape[0] == 1:
        x = np.repeat(x, 2, axis=0)
    return x[:2]


def trim_tail(audio: np.ndarray, rate: int, threshold_db: float = -60.0, pad_seconds: float = 0.05) -> np.ndarray:
    """Drops the silent end of a one-shot (below `threshold_db` from its peak).

    A shorter seed means shorter buds, and decoding time grows with length.
    Nothing audible is lost: only what is 60 dB under the peak.
    """
    level = np.abs(audio).max(axis=0)
    peak = float(level.max()) if level.size else 0.0
    if peak <= 0.0:
        return audio
    loud = np.nonzero(level >= peak * 10 ** (threshold_db / 20))[0]
    end = min(audio.shape[1], int(loud[-1]) + 1 + int(pad_seconds * rate))
    return audio[:, :end]


def finish_bud(stereo: np.ndarray, seed_channels: int, seed_samples: int,
               max_seconds: float = MAX_BUD_SECONDS) -> np.ndarray:
    """Model output -> what the plugin would play.

    Same channel count as the seed (the plugin keeps stereo seeds stereo),
    no longer than the seed nor than `max_seconds`, a short fade at the end,
    and never louder than 0.95 (quieter buds stay quieter, both sides together).
    """
    length = min(stereo.shape[1], seed_samples, int(max_seconds * SAMPLE_RATE))
    out = stereo[:, :length].copy()
    if seed_channels == 1:
        out = out.mean(axis=0, keepdims=True)
    fade = min(length, int(0.05 * SAMPLE_RATE))
    if fade > 0:
        out[:, length - fade:] *= np.linspace(1.0, 0.0, fade, dtype=np.float32)
    peak = float(np.abs(out).max()) if out.size else 0.0
    if peak > 0.95:
        out *= 0.95 / peak
    return out.astype(np.float32)


def _stft_mag(mono: np.ndarray, n_fft: int) -> np.ndarray:
    hop = n_fft // 4
    if len(mono) < n_fft:
        mono = np.pad(mono, (0, n_fft - len(mono)))
    frames = np.lib.stride_tricks.sliding_window_view(mono, n_fft)[::hop]
    return np.abs(np.fft.rfft(frames * np.hanning(n_fft), axis=-1))


def spectral_centroid(audio: np.ndarray, rate: int = SAMPLE_RATE) -> float:
    """Energy-weighted mean centroid in Hz: the "brightness" measure for lean."""
    mag = _stft_mag(audio.mean(axis=0), 2048)
    freqs = np.fft.rfftfreq(2048, 1.0 / rate)
    energy = mag.sum(axis=1)
    if energy.sum() <= 1e-12:
        return 0.0
    per_frame = (mag * freqs).sum(axis=1) / np.maximum(energy, 1e-12)
    return float((per_frame * energy).sum() / energy.sum())


def spectral_distance(a: np.ndarray, b: np.ndarray) -> float:
    """Multi-resolution log-magnitude distance (0 = identical). Mono mix, shortest length."""
    n = min(a.shape[-1], b.shape[-1])
    x, y = a[..., :n].mean(axis=0), b[..., :n].mean(axis=0)
    total = 0.0
    for n_fft in (512, 1024, 2048):
        mx, my = _stft_mag(x, n_fft), _stft_mag(y, n_fft)
        total += float(np.mean(np.abs(np.log(mx + 1e-5) - np.log(my + 1e-5))))
    return total / 3.0
