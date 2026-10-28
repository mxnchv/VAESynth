"""The local server's protocol, with a fast fake model (no weights needed)."""

import json
import threading
import urllib.error
import urllib.request

import numpy as np
import pytest

from vaesynth_exp import config as cfg
from vaesynth_exp.latent import HOP, SAMPLE_RATE
from vaesynth_exp.plant import CENTRE, SAMPLE_NEUTRAL
from vaesynth_exp.server import Engine, serve


class FakeModel:
    """encode: frame RMS as 64 channels. decode: a tone whose level follows the latent."""

    name, device = "fake", "cpu"

    def encode(self, stereo):
        frames = int(np.ceil(stereo.shape[1] / HOP))
        padded = np.zeros((2, frames * HOP), np.float32)
        padded[:, : stereo.shape[1]] = stereo
        rms = np.sqrt((padded.reshape(2, frames, HOP) ** 2).mean(axis=(0, 2)))
        return np.tile(rms, (64, 1)).astype(np.float32)

    def decode(self, z):
        level = np.repeat(z.mean(axis=0), HOP)
        t = np.arange(level.size) / SAMPLE_RATE
        tone = (level * np.sin(2 * np.pi * 440 * t)).astype(np.float32)
        return np.stack([tone, 0.5 * tone])


@pytest.fixture()
def server():
    rng = np.random.default_rng(0)
    means = rng.standard_normal((50, 64))
    config = cfg.build(means, 1000 * 2 ** means[:, 0])
    config.update(version=1, basis=np.asarray(config["basis"], np.float32),
                  scale=np.asarray(config["scale"], np.float32), direction=np.asarray(config["direction"], np.float32))
    httpd = serve(Engine(FakeModel(), config), port=0)
    thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    thread.start()
    yield f"http://127.0.0.1:{httpd.server_address[1]}"
    httpd.shutdown()


def call(url, data=None):
    request = urllib.request.Request(url, data=data, method="POST" if data is not None else "GET")
    try:
        with urllib.request.urlopen(request, timeout=10) as response:
            return response.status, dict(response.headers), response.read()
    except urllib.error.HTTPError as error:
        return error.code, dict(error.headers), error.read()


def coords_query(c):
    return ",".join(f"{v:.9g}" for v in c)


def test_info(server):
    status, _, body = call(server + "/info")
    info = json.loads(body)
    assert status == 200 and info["count"] == 11 and len(info["direction"]) == 11 and info["config_version"] == 1


def test_seed_then_render_keeps_channels_and_is_deterministic(server):
    for channels in (1, 2):
        rate = 48000
        t = np.arange(rate) / rate
        mono = (0.5 * np.sin(2 * np.pi * 330 * t) * np.exp(-3 * t)).astype("<f4")
        interleaved = np.repeat(mono, channels) if channels == 2 else mono
        key = f"seed{channels}"
        status, _, body = call(f"{server}/seed?key={key}&rate={rate}&channels={channels}", interleaved.tobytes())
        assert status == 200, body

        coords = CENTRE.copy()
        coords[0] = 0.8
        url = f"{server}/render?key={key}&coords={coords_query(coords)}"
        status, headers, audio = call(url, b"")
        assert status == 200
        assert int(headers["X-Channels"]) == channels and int(headers["X-Sample-Rate"]) == SAMPLE_RATE
        frames = int(headers["X-Frames"])
        assert len(audio) == frames * channels * 4
        assert frames <= SAMPLE_RATE  # no longer than the 1 s seed
        assert np.abs(np.frombuffer(audio, "<f4")).max() <= 0.95 + 1e-6
        assert call(url, b"")[2] == audio  # same request, same audio


def test_seed_node_returns_the_seed(server):
    rate = SAMPLE_RATE
    mono = (0.2 * np.sin(2 * np.pi * 200 * np.arange(rate // 2) / rate)).astype("<f4")
    call(f"{server}/seed?key=s&rate={rate}&channels=1", mono.tobytes())
    status, _, audio = call(f"{server}/render?key=s&coords={coords_query(SAMPLE_NEUTRAL)}", b"")
    assert status == 200 and np.allclose(np.frombuffer(audio, "<f4"), mono)


def test_errors(server):
    assert call(f"{server}/render?key=nope&coords={coords_query(CENTRE)}", b"")[0] == 404
    assert call(f"{server}/render?key=nope&coords=1,2,3", b"")[0] == 400
    assert call(f"{server}/seed?key=x&rate=44100&channels=3", np.zeros(9, "<f4").tobytes())[0] == 400
    assert call(server + "/nothing")[0] == 404
