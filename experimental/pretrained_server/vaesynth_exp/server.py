"""Local server: gives the plugin the audio of each bud (step 2).

Runs on the same computer as the DAW and only listens on 127.0.0.1. The
plugin's experimental adapter grows the plant itself (in C++, like
vaesynth_exp/plant.py) and asks this server for audio.

Protocol (HTTP/1.1, all numbers little-endian):

GET  /info
     -> JSON: model, config version, sample rate, count (11), direction (11
        floats), sigma, lean_k, leash.
POST /seed?key=<key>&rate=<Hz>&channels=<1|2>
     body: float32 samples, interleaved, at most 10 s.
     The server encodes the seed once and keeps it in a cache under <key>
     (the adapter uses a hash of the audio). -> JSON {"key", "frames"}
POST /render?key=<key>&coords=<11 comma-separated floats>
     -> body: float32 interleaved at 44.1 kHz, same channels as the seed.
        Headers: X-Sample-Rate, X-Channels, X-Frames.
     404 {"error": "unknown_seed"} if the key is not cached: send /seed, retry.
     A node equal to the seed (coords = sampleNeutral or a library patch)
     returns the seed itself, at 44.1 kHz.

Every parameter can also go in a header instead of the query string:
X-VAE-Key, X-VAE-Rate, X-VAE-Channels, X-VAE-Coords. The plugin's adapter
uses the headers.

Errors are JSON {"error": ...} with a 4xx/5xx status. The adapter treats any
error or timeout as "this bud stays unlit" (render() returns nullptr).

Run:   python -m vaesynth_exp.server [--port 8765] [--config configs/config_v001.json]
Speed: python -m vaesynth_exp.server --benchmark
"""

from __future__ import annotations

import argparse
import json
import threading
import time
from collections import OrderedDict
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

import numpy as np

from . import audio as au
from . import config as cfg
from .latent import HOP, SAMPLE_RATE
from .plant import COUNT, MutationSettings, is_origin

HOST = "127.0.0.1"
DEFAULT_PORT = 8765
MAX_BODY = 64 * 1024 * 1024
MAX_SEEDS = 32


class Engine:
    """Model + config + seed cache. Thread-safe: one model call at a time."""

    def __init__(self, model, config: dict, settings: MutationSettings | None = None,
                 max_seconds: float = au.MAX_BUD_SECONDS):
        self.model = model
        self.config = config
        self.settings = settings or MutationSettings()
        self.lock = threading.Lock()
        self.seeds: OrderedDict[str, dict] = OrderedDict()
        self.set_max_seconds(max_seconds)

    def set_max_seconds(self, seconds: float) -> None:
        """Longest bud. Decoding time grows with length, so this is the main speed lever."""
        self.max_seconds = float(seconds)
        self.max_frames = int(np.ceil(self.max_seconds * SAMPLE_RATE / HOP)) + 2

    def info(self) -> dict:
        return {
            "model": self.model.name,
            "config_version": self.config.get("version"),
            "sample_rate": SAMPLE_RATE,
            "count": COUNT,
            "direction": [float(v) for v in self.config["direction"]],
            "sigma": self.settings.sigma,
            "lean_k": self.settings.lean_k,
            "leash": self.settings.leash,
            "max_bud_seconds": self.max_seconds,
        }

    def add_seed(self, key: str, interleaved: np.ndarray, rate: int, channels: int) -> int:
        if channels not in (1, 2) or rate < 8000 or interleaved.size % channels:
            raise ValueError("bad seed format")
        audio = interleaved.reshape(-1, channels).T[:, : int(au.MAX_SEED_SECONDS * rate)]
        # Buds are no longer than the seed's audible part nor than max_seconds:
        # encode only that (plus a margin), it is all the decoder will use.
        audible = au.trim_tail(audio, rate)
        useful = audible[:, : int((self.max_seconds + 0.25) * rate)]
        stereo = au.to_model_input(useful, rate)
        with self.lock:
            latent = self.model.encode(stereo)[:, : self.max_frames]
        self.seeds[key] = {"latent": latent, "audio": au.resample(audio, rate),
                           "audible_samples": int(round(audible.shape[1] * SAMPLE_RATE / rate)),
                           "channels": channels}
        self.seeds.move_to_end(key)
        while len(self.seeds) > MAX_SEEDS:
            self.seeds.popitem(last=False)
        return latent.shape[1]

    def render(self, key: str, coords: np.ndarray) -> np.ndarray | None:
        seed = self.seeds.get(key)
        if seed is None:
            return None
        self.seeds.move_to_end(key)
        if is_origin(coords):
            return seed["audio"][:, : int(au.MAX_BUD_SECONDS * SAMPLE_RATE)]
        frames = min(seed["latent"].shape[1], self.max_frames)
        z = seed["latent"][:, :frames] + cfg.offset(self.config, coords)[:, None]
        with self.lock:
            out = self.model.decode(z)
        return au.finish_bud(out, seed["channels"], seed["audible_samples"], self.max_seconds)


def make_handler(engine: Engine):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, fmt, *args):  # quieter than the default
            pass

        def _json(self, status: int, payload: dict) -> None:
            body = json.dumps(payload).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def _query(self) -> dict:
            """Parameters from the query string, or from X-VAE-<Name> headers (what the plugin sends)."""
            params = {k: v[0] for k, v in parse_qs(urlparse(self.path).query).items()}
            for name in ("key", "rate", "channels", "coords"):
                value = self.headers.get(f"X-VAE-{name.capitalize()}")
                if name not in params and value is not None:
                    params[name] = value
            return params

        def do_GET(self):
            if urlparse(self.path).path == "/info":
                self._json(200, engine.info())
            else:
                self._json(404, {"error": "not_found"})

        def do_POST(self):
            path, q = urlparse(self.path).path, self._query()
            length = int(self.headers.get("Content-Length") or 0)
            if length > MAX_BODY:
                self._json(413, {"error": "too_large"})
                return
            body = self.rfile.read(length) if length else b""
            started = time.perf_counter()
            try:
                if path == "/seed":
                    samples = np.frombuffer(body, "<f4")
                    frames = engine.add_seed(q["key"], samples, int(q["rate"]), int(q["channels"]))
                    print(f"semilla {q['key'][:12]}: {samples.size // int(q['channels'])} muestras, "
                          f"{frames} frames ({1000 * (time.perf_counter() - started):.0f} ms)", flush=True)
                    self._json(200, {"key": q["key"], "frames": frames})
                elif path == "/render":
                    coords = np.array([float(v) for v in q["coords"].split(",")], np.float32)
                    if coords.size != COUNT or not np.all(np.isfinite(coords)):
                        raise ValueError("coords must be 11 finite numbers")
                    audio = engine.render(q["key"], coords)
                    if audio is None:
                        self._json(404, {"error": "unknown_seed"})
                        return
                    data = np.ascontiguousarray(audio.T, "<f4").tobytes()
                    self.send_response(200)
                    self.send_header("Content-Type", "application/octet-stream")
                    self.send_header("Content-Length", str(len(data)))
                    self.send_header("X-Sample-Rate", str(SAMPLE_RATE))
                    self.send_header("X-Channels", str(audio.shape[0]))
                    self.send_header("X-Frames", str(audio.shape[1]))
                    self.end_headers()
                    self.wfile.write(data)
                    print(f"brote: {1000 * (time.perf_counter() - started):.0f} ms", flush=True)
                else:
                    self._json(404, {"error": "not_found"})
            except (KeyError, ValueError) as error:
                self._json(400, {"error": "bad_request", "detail": str(error)})
            except Exception as error:  # never take the server down for one request
                self._json(500, {"error": "internal", "detail": repr(error)})

    return Handler


def serve(engine: Engine, port: int = DEFAULT_PORT) -> ThreadingHTTPServer:
    return ThreadingHTTPServer((HOST, port), make_handler(engine))


def benchmark(engine: Engine, seconds: float = 4.0, buds: int = 8,
              caps: tuple[float, ...] = (4.5, 3.0, 2.0)) -> list[dict]:
    """Time per bud for a synthetic stereo one-shot of `seconds` (audible to the end),
    with each maximum bud length in `caps`."""
    t = np.arange(int(SAMPLE_RATE * seconds)) / SAMPLE_RATE
    tone = (0.3 * np.sin(2 * np.pi * 220 * t) * np.exp(-0.5 * t)).astype(np.float32)
    interleaved = np.c_[tone, tone].reshape(-1)
    rng = np.random.default_rng(0)
    previous = engine.max_seconds
    results = []
    for cap in caps:
        engine.set_max_seconds(cap)
        started = time.perf_counter()
        engine.add_seed(f"benchmark{cap}", interleaved, SAMPLE_RATE, 2)
        encode_ms = 1000 * (time.perf_counter() - started)
        engine.render(f"benchmark{cap}", np.clip(0.5 + 0.1 * rng.standard_normal(COUNT), 0, 1).astype(np.float32))  # warm-up
        times = []
        for _ in range(buds):
            coords = np.clip(0.5 + 0.1 * rng.standard_normal(COUNT), 0, 1).astype(np.float32)
            started = time.perf_counter()
            engine.render(f"benchmark{cap}", coords)
            times.append(1000 * (time.perf_counter() - started))
        per_bud = float(np.median(times))
        results.append({
            "device": engine.model.device,
            "max_bud_seconds": cap,
            "bud_seconds": round(min(cap, seconds), 2),
            "encode_ms": round(encode_ms),
            "ms_per_bud_median": round(per_bud),
            "plant_of_65_buds_s": round(65 * per_bud / 1000, 1),
            "plant_of_120_buds_s": round(120 * per_bud / 1000, 1),
        })
    engine.set_max_seconds(previous)
    return results


def main() -> None:
    from .latent import LatentModel

    parser = argparse.ArgumentParser(description="VAESynth experimental engine server (127.0.0.1 only)")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--config", help="config_vNNN.json to use (default: the newest in configs/)")
    parser.add_argument("--device", help="cpu, mps or cuda (default: cuda if present, else cpu; "
                                             "on the M1, cpu measured faster than mps)")
    parser.add_argument("--max-seconds", type=float, default=au.MAX_BUD_SECONDS,
                        help="longest bud in seconds (default 4.5). Shorter = proportionally faster")
    parser.add_argument("--threads", type=int, help="CPU threads for PyTorch (default: PyTorch's choice)")
    parser.add_argument("--benchmark", action="store_true", help="measure time per bud and exit")
    parser.add_argument("--random-weights", action="store_true", help="tests only: untrained model")
    args = parser.parse_args()

    import torch

    if args.threads:
        torch.set_num_threads(args.threads)
    device = args.device or ("cuda" if torch.cuda.is_available() else "cpu")
    config = cfg.load(args.config)
    print(f"Configuración v{config['version']:03d} · cargando el modelo…", flush=True)
    model = LatentModel(device=device, random_weights=args.random_weights)
    engine = Engine(model, config, max_seconds=args.max_seconds)
    print(f"Modelo {model.name} en {model.device} · {model.fused} capas con weight norm fusionadas · "
          f"{torch.get_num_threads()} hilos · brotes de hasta {args.max_seconds:g} s", flush=True)

    if args.benchmark:
        for result in benchmark(engine):
            print(json.dumps(result))
        return

    server = serve(engine, args.port)
    print(f"Escuchando en http://{HOST}:{args.port} · Ctrl+C para parar", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("Parado.")


if __name__ == "__main__":
    main()
