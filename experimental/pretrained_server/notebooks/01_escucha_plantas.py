# ---
# jupyter:
#   jupytext:
#     formats: ipynb,py:percent
#   kernelspec:
#     display_name: Python 3
#     name: python3
# ---

# %% [markdown]
# # VAESynth · Paso 1: plantas de escucha con un modelo preentrenado
#
# Este cuaderno **no toca el plugin**. Hace crecer plantas como lo haría el motor experimental y las guarda como WAV,
# con una página HTML para escucharlas ordenadas como en la planta (nivel de abajo arriba; lean de izquierda a derecha).
#
# - Modelo: el VAE de Stable Audio Open (solo el autoencoder). Antes de usarlo hay que aceptar su licencia en Hugging Face
#   y registrarse con Stability (ver `README.md`).
# - Topología y semillas aleatorias: las mismas que el plugin (`Plant::grow`, con el mínimo de Wild).
# - Cada brote: latente de la semilla + un mismo desplazamiento en todos los frames. Los 11 valores del brote son coordenadas
#   en las 11 direcciones principales del latente (0,5 = sin desplazamiento).
#
# Funciona en Google Colab (con GPU) o en local. En CPU funciona, pero tarda bastante más.

# %% [markdown]
# ## 1. Entorno

# %%
import os
import sys
from pathlib import Path

IN_COLAB = "google.colab" in sys.modules

# Carpeta experimental/pretrained_server del repositorio (rama experiment/pretrained-engine).
# En Colab: copia esa carpeta a tu Drive (o clona la rama ahí) y ajusta la ruta.
if IN_COLAB:
    from google.colab import drive
    drive.mount("/content/drive")
    PACKAGE_DIR = Path("/content/drive/MyDrive/VAESynth/experimental/pretrained_server")
    import subprocess
    subprocess.run([sys.executable, "-m", "pip", "install", "-q", "-r", str(PACKAGE_DIR / "requirements.txt")], check=True)
else:
    PACKAGE_DIR = Path.cwd().parent if Path.cwd().name == "notebooks" else Path.cwd()

sys.path.insert(0, str(PACKAGE_DIR))

# %% [markdown]
# ## 2. Parámetros
#
# - `SOUNDS_DIR`: carpeta con sonidos de la librería (se busca en subcarpetas).
# - `N_CONFIG_SOUNDS`: cuántos sonidos se usan para calcular las direcciones del latente (B, s, d). 200–500 está bien.
# - `SEEDS`: nombres de archivo concretos para usar como semilla, o `None` para elegir `N_SEEDS` al azar.
# - `NEW_CONFIG`: `True` calcula una configuración nueva y la guarda como versión nueva; `False` usa la última guardada
#   (o la de `CONFIG_PATH`). Nunca se sobrescribe una configuración existente.
# - `SMOKE_TEST`: solo para comprobar que todo corre. Usa pesos aleatorios y sonidos sintéticos: suena a ruido.

# %%
SOUNDS_DIR = Path("/content/drive/MyDrive/VAESynth/libreria" if IN_COLAB else "~/VAESynth/libreria").expanduser()
OUTPUT_DIR = Path("/content/drive/MyDrive/VAESynth/escucha" if IN_COLAB else "~/VAESynth/escucha").expanduser()

N_CONFIG_SOUNDS = 300
SEEDS = None            # p. ej. ["cristal_A4.wav", "cuerda_pizz.wav"]
N_SEEDS = 12
WILDS = [0.0, 0.5, 1.0]
RNG_SEEDS = [1]         # varias = varias plantas (Regrow) por semilla y Wild
SPREAD = 3.0            # coordenadas 0 y 1 = ±SPREAD desviaciones típicas del latente
SIGMA, LEAN_K, LEASH = 1.0, 1.2, 0.0   # ver MutationSettings en vaesynth_exp/plant.py

NEW_CONFIG = True
CONFIG_PATH = None
CONFIG_NOTES = ""       # qué tiene de particular esta versión (se guarda en el archivo)
SMOKE_TEST = os.environ.get("VAESYNTH_SMOKE") == "1"

HF_TOKEN = os.environ.get("HF_TOKEN")
if IN_COLAB and HF_TOKEN is None:
    try:
        from google.colab import userdata
        HF_TOKEN = userdata.get("HF_TOKEN")
    except Exception:
        pass

# %% [markdown]
# ## 3. Modelo

# %%
import random

import numpy as np

from vaesynth_exp import audio as au
from vaesynth_exp import config as cfg
from vaesynth_exp import listen
from vaesynth_exp.latent import LatentModel
from vaesynth_exp.plant import MutationSettings

if SMOKE_TEST:
    import tempfile

    import soundfile as sf

    SOUNDS_DIR = Path(tempfile.mkdtemp(prefix="vaesynth_smoke_sounds_"))
    OUTPUT_DIR = Path(tempfile.mkdtemp(prefix="vaesynth_smoke_out_"))
    cfg.CONFIG_DIR = Path(tempfile.mkdtemp(prefix="vaesynth_smoke_configs_"))  # no ensuciar configs/
    rng = np.random.default_rng(0)
    for k in range(12):
        sr = [44100, 48000, 22050][k % 3]
        t = np.arange(int(sr * (0.8 + 0.3 * k))) / sr
        f0 = 110 * 2 ** (k / 5)
        env = np.exp(-t * (2 + k))
        tone = sum(np.sin(2 * np.pi * f0 * h * t) / h ** (1 + k % 3) for h in range(1, 12)) * env
        tone += 0.05 * rng.standard_normal(len(t)) * env
        data = tone[:, None] if k % 2 == 0 else np.c_[tone, np.roll(tone, 30) * 0.8]
        sf.write(SOUNDS_DIR / f"sintetico_{k:02d}.wav", (0.5 * data / np.abs(data).max()).astype(np.float32), sr)
    N_CONFIG_SOUNDS, N_SEEDS, WILDS = 12, 2, [0.0, 1.0]

model = LatentModel(random_weights=SMOKE_TEST, token=HF_TOKEN)
print("Modelo:", model.name, "· dispositivo:", model.device)

# %% [markdown]
# ## 4. Configuración del motor: direcciones del latente (B, s) y dirección "brillante" (d)

# %%
sounds = au.find_sounds(SOUNDS_DIR)
print(len(sounds), "sonidos en", SOUNDS_DIR)

if NEW_CONFIG:
    picked = random.Random(0).sample(sounds, min(N_CONFIG_SOUNDS, len(sounds)))
    means, centroids, used = [], [], []
    for i, path in enumerate(picked):
        try:
            raw, rate = au.load(path)
            x = au.to_model_input(raw, rate)
            means.append(model.encode(x).mean(axis=1))
            centroids.append(au.spectral_centroid(x))
            used.append(str(path.relative_to(SOUNDS_DIR)))
        except Exception as error:  # un archivo raro no debe parar todo
            print("Saltado:", path.name, "·", error)
        if (i + 1) % 50 == 0:
            print(f"{i + 1}/{len(picked)} codificados")
    built = cfg.build(np.stack(means), np.array(centroids), spread=SPREAD)
    CONFIG_PATH = cfg.save(built, model.name, used, notes=CONFIG_NOTES)
    print("Guardada nueva configuración:", CONFIG_PATH.name)

config = cfg.load(CONFIG_PATH)
CONFIG_PATH = Path(CONFIG_PATH) if CONFIG_PATH else sorted(cfg.CONFIG_DIR.glob("config_v*.json"))[-1]
print("Usando", CONFIG_PATH.name, "·", config["stats"])
if config["stats"]["sounds"] < 100:
    print("Aviso: con menos de ~100 sonidos, las 11 direcciones y el R² del brillo están sobreajustados. Úsalos solo para probar.")

# %% [markdown]
# Cómo leer las estadísticas:
# - `explained_variance_11`: qué parte de la variación entre sonidos cubren las 11 direcciones.
# - `brightness_r2`: cuánto del brillo explica la dirección `d` (0 = nada, 1 = todo). Si es muy bajo, `lean` no tendrá
#   mucho sentido con este latente: ya es un resultado.

# %% [markdown]
# ## 5. Hacer crecer las plantas

# %%
settings = MutationSettings(sigma=SIGMA, lean_k=LEAN_K, leash=LEASH)
by_name = {p.name: p for p in sounds}
seeds = [by_name[name] for name in SEEDS] if SEEDS else random.Random(1).sample(sounds, min(N_SEEDS, len(sounds)))

run_dir = OUTPUT_DIR / CONFIG_PATH.stem
pages, summaries = [], []
for seed_path in seeds:
    for wild in WILDS:
        for rng_seed in RNG_SEEDS:
            plant = listen.render_plant(model, config, seed_path, wild, rng_seed, settings)
            title = f"{seed_path.stem} · Wild {wild} · rng {rng_seed}"
            folder = run_dir / seed_path.stem / f"wild{wild:.2f}_rng{rng_seed}"
            pages.append((title, listen.write_plant(plant, folder, title)))
            summaries.append({"planta": title, **listen.summarise(plant)})
            print(summaries[-1])

listen.write_index(pages, run_dir)
listen.copy_config(CONFIG_PATH, run_dir)
print("Abre", run_dir / "index.html")

# %% [markdown]
# ## 6. Resumen
#
# - `reconstruction_gap`: cuánto difiere D(E(semilla)) de la semilla. Es el "salto en la base de la planta".
# - `mean_dist_seed_by_depth`: debería crecer con el nivel y con Wild.
# - `lean_vs_brightness_corr`: > 0 si los brotes de la derecha son de verdad más brillantes que su padre.
#
# Las cifras orientan; lo que decide es escuchar. Apuntad qué sorprende, qué aburre y qué suena roto, en qué semillas falla,
# desde qué nivel deja de reconocerse la semilla y si la derecha suena más brillante.

# %%
for s in summaries:
    print(s)
