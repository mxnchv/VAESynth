"""The pretrained autoencoder: Stable Audio Open's VAE ("Oobleck").

Only the autoencoder is loaded, not the text-to-audio model. Weights come from
the Hugging Face repo `stabilityai/stable-audio-open-1.0` (subfolder `vae`),
which asks you to accept Stability AI's licence and to log in with a token.
They are cached locally and never go into the repository.

Latent: 64 channels, one frame every 2048 samples at 44.1 kHz (~21.5 Hz).
Encoding uses the posterior mean and decoding adds no noise, so the same
input always gives the same output (up to CPU/GPU numeric differences).
"""

from __future__ import annotations

import numpy as np
import torch

MODEL_ID = "stabilityai/stable-audio-open-1.0"
SAMPLE_RATE = 44100
HOP = 2048
LATENT_CHANNELS = 64


def pick_device() -> str:
    if torch.cuda.is_available():
        return "cuda"
    if getattr(torch.backends, "mps", None) is not None and torch.backends.mps.is_available():
        return "mps"
    return "cpu"


class LatentModel:
    """encode(audio) -> latent (64, T) and decode(latent) -> audio (2, N)."""

    def __init__(self, device: str | None = None, random_weights: bool = False, token: str | None = None):
        from diffusers import AutoencoderOobleck

        self.device = device or pick_device()
        if random_weights:
            # Smoke tests only: same architecture, untrained. Sounds like noise.
            torch.manual_seed(0)
            self.vae = AutoencoderOobleck()
            self.name = "oobleck-random-weights"
        else:
            self.vae = AutoencoderOobleck.from_pretrained(MODEL_ID, subfolder="vae", token=token)
            self.name = f"{MODEL_ID}/vae"
        self.vae = self.vae.to(self.device).eval()

    @torch.no_grad()
    def encode(self, stereo: np.ndarray) -> np.ndarray:
        """stereo: float32 (2, N) at 44.1 kHz. Returns the posterior mean (64, T)."""
        n = stereo.shape[1]
        padded = int(np.ceil(max(n, HOP) / HOP) * HOP)
        x = np.zeros((1, 2, padded), np.float32)
        x[0, :, :n] = stereo
        out = self.vae.encode(torch.from_numpy(x).to(self.device))
        return out.latent_dist.mean[0].float().cpu().numpy()

    @torch.no_grad()
    def decode(self, latents: np.ndarray) -> np.ndarray:
        """latents: (64, T) or a batch (B, 64, T). Returns (2, N) or (B, 2, N)."""
        single = latents.ndim == 2
        z = torch.from_numpy(np.asarray(latents[None] if single else latents, np.float32)).to(self.device)
        audio = self.vae.decode(z).sample.float().cpu().numpy()
        return audio[0] if single else audio
