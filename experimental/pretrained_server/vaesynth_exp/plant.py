"""Port of the plugin's plant growth (Source/engine/Plant.cpp) plus the
experimental engine's mutation in latent coordinates.

The topology, geometry and random sequence follow `vae::Plant::grow` on
branch claude/plant-beta (commit 726088a): same mulberry32 generator, same
order of random draws, same Wild minimum. The plugin computes in float32 with
the platform's libm, so values can differ in the last bits; the plants are
meant to be the same by ear, not bit for bit.

Coordinates: a node holds 11 values in [0, 1], like the plugin's `Traits`.
For the experimental engine they are coordinates in a subspace of the latent:
0.5 means "no offset from the seed". See README.md, section "Coordenadas".
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field

import numpy as np

COUNT = 11            # vae::trait::count
MAX_DEPTH = 4         # vae::Plant::maxDepth
MINIMUM_WILD = 0.25   # vae::Plant::minimumWild
TWO_PI = np.float32(2.0 * math.pi)

# vae::sampleNeutral and the library seeds (Source/engine/Traits.h). The engine
# treats a parent equal to one of these as the seed itself (offset zero).
SAMPLE_NEUTRAL = (0.5, 0.5, 0.0, 0.4, 0.0, 1.0, 0.0, 0.0, 0.0, 0.5, 0.0)
LIBRARY = {
    "Glass":  (0.85, 0.40, 0.62, 0.35, 0.03, 0.55, 0.20, 0.05, 0.40, 0.5, 0.0),
    "Keys":   (0.45, 0.25, 0.28, 0.00, 0.04, 0.60, 0.25, 0.04, 0.30, 0.5, 0.0),
    "Pluck":  (0.70, 0.35, 0.05, 0.20, 0.02, 0.30, 0.35, 0.10, 0.15, 0.5, 0.0),
    "Bell":   (0.60, 0.50, 0.88, 0.55, 0.01, 0.85, 0.15, 0.02, 0.10, 0.5, 0.0),
    "Breath": (0.30, 0.70, 0.02, 0.10, 0.35, 0.50, 0.65, 0.55, 0.25, 0.5, 0.0),
}
ORIGINS = [np.array(SAMPLE_NEUTRAL, np.float32)] + [np.array(v, np.float32) for v in LIBRARY.values()]
CENTRE = np.full(COUNT, 0.5, np.float32)


class Rng:
    """mulberry32, as `vae::Rng` (Source/engine/VariationEngine.h)."""

    def __init__(self, seed: int):
        self.state = seed & 0xFFFFFFFF

    def uniform(self) -> np.float32:
        self.state = (self.state + 0x6D2B79F5) & 0xFFFFFFFF
        t = self.state
        t = ((t ^ (t >> 15)) * (t | 1)) & 0xFFFFFFFF
        t ^= (t + (((t ^ (t >> 7)) * (t | 61)) & 0xFFFFFFFF)) & 0xFFFFFFFF
        t &= 0xFFFFFFFF
        # C++: (float) uint32 / 4294967296.0f  (the cast rounds to float first)
        return np.float32(np.float32(t ^ (t >> 14)) / np.float32(4294967296.0))

    def gauss(self) -> np.float32:
        u = max(np.float32(1.0e-9), self.uniform())
        radius = np.float32(math.sqrt(-2.0 * math.log(float(u))))
        return np.float32(radius * np.float32(math.cos(float(TWO_PI * self.uniform()))))


@dataclass
class MutationSettings:
    """How the experimental engine moves in latent coordinates.

    sigma:  random step per coordinate, multiplied by the plant's `amount`.
    lean_k: how far `lean` pushes along the brightness direction `d`.
    leash:  pull back towards the seed (theta in proposal P2). 0 = free walk.
    """

    sigma: float = 1.0
    lean_k: float = 1.2
    leash: float = 0.0


@dataclass
class Node:
    parent: int = -1
    depth: int = 0
    angle: float = 0.0
    length: float = 0.0
    born: float = 0.0
    phase: float = 0.0
    lean: float = 0.0
    amount: float = 0.0
    coords: np.ndarray = field(default_factory=lambda: CENTRE.copy())


def is_origin(t: np.ndarray) -> bool:
    return any(np.all(np.abs(t - o) <= 1.0e-5) for o in ORIGINS)


def mutate(parent: np.ndarray, lean: float, amount: float, rng: Rng,
           direction: np.ndarray, settings: MutationSettings) -> np.ndarray:
    """Child coordinates. Draws exactly COUNT gaussians, like the DSP engine."""
    base = CENTRE if is_origin(parent) else parent.astype(np.float32)
    child = np.empty(COUNT, np.float32)
    for i in range(COUNT):
        step = rng.gauss() * np.float32(amount * settings.sigma)
        pull = np.float32(settings.leash) * (np.float32(0.5) - base[i])
        push = np.float32(lean * amount * settings.lean_k) * np.float32(direction[i])
        child[i] = np.clip(base[i] + step + pull + push, 0.0, 1.0)
    return child


def grow(wild: float, rng_seed: int, direction: np.ndarray | None = None,
         settings: MutationSettings | None = None,
         seed_coords: np.ndarray | None = None) -> list[Node]:
    """`vae::Plant::grow`, with the experimental engine's mutation.

    seed_coords: the generation's starting point. None = the seed itself
    (a freshly loaded sound). Pass a bud's coords to grow from a planted bud.
    """
    direction = np.zeros(COUNT, np.float32) if direction is None else np.asarray(direction, np.float32)
    settings = settings or MutationSettings()
    rng = Rng(rng_seed)
    strength = np.float32(MINIMUM_WILD + (1.0 - MINIMUM_WILD) * min(1.0, max(0.0, wild)))

    root = Node(coords=np.array(SAMPLE_NEUTRAL, np.float32) if seed_coords is None
                else np.asarray(seed_coords, np.float32).copy())
    root.phase = float(rng.uniform() * TWO_PI)
    nodes = [root]

    def branch(parent_index: int) -> None:
        parent = nodes[parent_index]
        if parent.depth >= MAX_DEPTH:
            return
        if parent.depth == 0:
            count = 3
        else:
            count = 2 if rng.uniform() < (0.75 if parent.depth >= 2 else 0.5) else 3
        spread = 0.6 if parent.depth == 0 else 0.42

        for i in range(count):
            side = i / (count - 1) * 2.0 - 1.0
            child = Node(parent=parent_index, depth=parent.depth + 1)
            child.angle = float(np.float32(parent.angle * 0.7 + side * spread) + rng.gauss() * np.float32(0.12))
            base_length = 0.3 if parent.depth == 0 else parent.length * 0.8
            child.length = float(np.float32(base_length) * (np.float32(0.85) + rng.uniform() * np.float32(0.3)))
            child.born = float(np.float32(parent.born + 0.32) + rng.uniform() * np.float32(0.18))
            child.phase = float(rng.uniform() * TWO_PI)
            child.lean = float(min(1.0, max(-1.0, child.angle / 1.1)))
            child.amount = float(strength * np.float32(0.17) * np.float32(0.6 + parent.depth * 0.2))
            child.coords = mutate(parent.coords, child.lean, child.amount, rng, direction, settings)
            nodes.append(child)
            branch(len(nodes) - 1)

    branch(0)
    return nodes
