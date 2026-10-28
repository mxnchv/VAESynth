"""Grow plants offline, write them as WAV files and build a listening page."""

from __future__ import annotations

import csv
import html
import shutil
from dataclasses import asdict
from pathlib import Path

import numpy as np
import soundfile as sf

from . import audio as au
from . import config as cfg
from .latent import HOP, SAMPLE_RATE, LatentModel
from .plant import MutationSettings, grow, is_origin


def render_plant(model: LatentModel, config: dict, seed_path: str | Path, wild: float, rng_seed: int,
                 settings: MutationSettings | None = None, batch: int = 8) -> dict:
    """One plant: nodes, the audio of every bud, the reconstruction and measures."""
    settings = settings or MutationSettings()
    raw, rate = au.load(seed_path)
    stereo = au.to_model_input(raw, rate)
    seed_44k = au.resample(raw, rate)
    latent = model.encode(stereo)

    max_frames = int(np.ceil(au.MAX_BUD_SECONDS * SAMPLE_RATE / HOP)) + 2
    base = latent[:, :max_frames]
    nodes = grow(wild, rng_seed, config["direction"], settings)

    reconstruction = au.finish_bud(model.decode(base), raw.shape[0], seed_44k.shape[1])
    sounds: list[np.ndarray | None] = [None] * len(nodes)
    sounds[0] = seed_44k  # the seed plays the file exactly

    todo = [i for i, n in enumerate(nodes) if i > 0 and not is_origin(n.coords)]
    for start in range(0, len(todo), batch):
        chunk = todo[start:start + batch]
        z = np.stack([base + cfg.offset(config, nodes[i].coords)[:, None] for i in chunk])
        for i, out in zip(chunk, model.decode(z)):
            sounds[i] = au.finish_bud(out, raw.shape[0], seed_44k.shape[1])

    rows = []
    for i, (node, sound) in enumerate(zip(nodes, sounds)):
        parent = sounds[node.parent] if node.parent >= 0 else None
        rows.append({
            "node": i, "parent": node.parent, "depth": node.depth,
            "lean": round(node.lean, 3), "amount": round(node.amount, 4),
            "centroid_hz": round(au.spectral_centroid(sound), 1),
            "dist_seed": round(au.spectral_distance(sound, seed_44k), 4),
            "dist_parent": round(au.spectral_distance(sound, parent), 4) if parent is not None else 0.0,
            "coords": " ".join(f"{c:.3f}" for c in node.coords),
        })
    return {
        "seed": str(seed_path), "wild": wild, "rng_seed": rng_seed, "settings": asdict(settings),
        "nodes": nodes, "sounds": sounds, "reconstruction": reconstruction, "rows": rows,
        "reconstruction_gap": au.spectral_distance(reconstruction, seed_44k),
    }


def summarise(plant: dict) -> dict:
    """Numbers to read next to the listening: does lean work, how far each level goes."""
    rows = plant["rows"]
    by_depth = {}
    for r in rows[1:]:
        by_depth.setdefault(r["depth"], []).append(r["dist_seed"])
    gains, leans = [], []
    for r in rows[1:]:
        parent = rows[r["parent"]]
        if r["centroid_hz"] > 0 and parent["centroid_hz"] > 0:
            gains.append(np.log2(r["centroid_hz"] / parent["centroid_hz"]))
            leans.append(r["lean"])
    corr = float(np.corrcoef(leans, gains)[0, 1]) if len(gains) > 2 and np.std(gains) > 0 else float("nan")
    return {
        "buds": len(rows) - 1,
        "reconstruction_gap": round(plant["reconstruction_gap"], 4),
        "mean_dist_seed_by_depth": {d: round(float(np.mean(v)), 4) for d, v in sorted(by_depth.items())},
        # > 0: buds on the right really get brighter than their parent.
        "lean_vs_brightness_corr": round(corr, 3),
    }


def write_plant(plant: dict, folder: str | Path, title: str) -> Path:
    """WAVs + measures.csv + index.html in `folder`. Returns the page."""
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    names = []
    for row, sound in zip(plant["rows"], plant["sounds"]):
        name = "n00_semilla.wav" if row["node"] == 0 else f"n{row['node']:02d}_nivel{row['depth']}_lean{row['lean']:+.2f}.wav"
        sf.write(folder / name, sound.T, SAMPLE_RATE, subtype="FLOAT")
        names.append(name)
    sf.write(folder / "reconstruccion.wav", plant["reconstruction"].T, SAMPLE_RATE, subtype="FLOAT")

    with open(folder / "medidas.csv", "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=list(plant["rows"][0].keys()) + ["file"])
        writer.writeheader()
        for row, name in zip(plant["rows"], names):
            writer.writerow({**row, "file": name})

    summary = summarise(plant)
    levels = []
    for depth in range(0, 5):
        cells = sorted((r for r in plant["rows"] if r["depth"] == depth), key=lambda r: r["lean"])
        items = "".join(
            f'<div class="bud"><div class="lbl">#{r["node"]} · lean {r["lean"]:+.2f} · {r["centroid_hz"]:.0f} Hz'
            f' · d {r["dist_seed"]:.2f}</div><audio controls preload="none" src="{html.escape(names[r["node"]])}"></audio></div>'
            for r in cells)
        levels.append(f'<section><h3>Nivel {depth}</h3><div class="row">{items}</div></section>')
    page = f"""<!doctype html><meta charset="utf-8"><title>{html.escape(title)}</title>
<style>body{{font:14px system-ui;margin:24px;background:#111;color:#eee}}h3{{margin:18px 0 6px;font-weight:500}}
.row{{display:flex;flex-wrap:wrap;gap:10px}}.bud{{background:#1c1c1c;padding:8px;border-radius:8px}}
.lbl{{font-size:12px;color:#aaa;margin-bottom:4px}}audio{{width:220px}}code{{color:#9cf}}</style>
<h2>{html.escape(title)}</h2>
<p>Semilla: <code>{html.escape(Path(plant["seed"]).name)}</code> · Wild {plant["wild"]} · rngSeed {plant["rng_seed"]}
 · ajustes {html.escape(str(plant["settings"]))}</p>
<p>Reconstrucción D(E(x)) <audio controls preload="none" src="reconstruccion.wav"></audio>
 · salto en la base: <b>{summary["reconstruction_gap"]}</b></p>
<p>Distancia media a la semilla por nivel: {summary["mean_dist_seed_by_depth"]} ·
 correlación lean / brillo: <b>{summary["lean_vs_brightness_corr"]}</b></p>
<p style="color:#aaa">Dentro de cada nivel, de izquierda (oscuro) a derecha (brillante), como en la planta.</p>
{''.join(levels)}"""
    path = folder / "index.html"
    path.write_text(page, encoding="utf-8")
    return path


def write_index(pages: list[tuple[str, Path]], folder: str | Path) -> Path:
    folder = Path(folder)
    links = "".join(f'<li><a href="{html.escape(str(p.relative_to(folder)))}">{html.escape(t)}</a></li>' for t, p in pages)
    path = folder / "index.html"
    path.write_text(f'<!doctype html><meta charset="utf-8"><title>Plantas</title>'
                    f'<style>body{{font:15px system-ui;margin:24px;background:#111;color:#eee}}a{{color:#9cf}}</style>'
                    f'<h2>Plantas de escucha</h2><ul>{links}</ul>', encoding="utf-8")
    return path


def copy_config(config_path: Path, folder: str | Path) -> None:
    """Keeps a copy of the config used next to the plants, for the record."""
    shutil.copy2(config_path, Path(folder) / config_path.name)
