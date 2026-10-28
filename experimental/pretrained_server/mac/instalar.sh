#!/usr/bin/env bash
# Instala lo necesario para el servidor experimental en un Mac con Apple Silicon.
# Todo queda dentro de experimental/pretrained_server/.venv (y los pesos del modelo
# en la caché de Hugging Face, ~/.cache/huggingface). No toca el plugin.
#
# Uso, desde Terminal:   bash experimental/pretrained_server/mac/instalar.sh
set -euo pipefail

# Operaciones que la GPU del Mac aún no tenga: que caigan a la CPU en vez de fallar.
export PYTORCH_ENABLE_MPS_FALLBACK=1
cd "$(dirname "$0")/.."
echo "== Carpeta: $(pwd)"

if [[ "$(uname -s)" != "Darwin" || "$(uname -m)" != "arm64" ]]; then
    echo "Este script es para Mac con Apple Silicon (M1/M2/M3/M4)." >&2
    exit 1
fi

# 1. uv: gestiona Python y las librerías sin tocar el Python del sistema.
if ! command -v uv >/dev/null 2>&1; then
    if ! command -v brew >/dev/null 2>&1; then
        echo "Falta Homebrew (https://brew.sh). Instálalo y vuelve a ejecutar este script." >&2
        exit 1
    fi
    echo "== Instalando uv con Homebrew"
    brew install uv
fi

# 2. Entorno de Python propio del experimento, con PyTorch (con soporte MPS) y diffusers.
if [[ -x .venv/bin/python ]]; then
    echo "== El entorno .venv ya existe: se actualizan sus librerías"
else
    echo "== Creando el entorno .venv (Python 3.12)"
    uv venv --python 3.12 .venv
fi
echo "== Instalando librerías (PyTorch, diffusers…). Puede tardar unos minutos."
uv pip install --python .venv/bin/python -r requirements.txt

.venv/bin/python - <<'EOF'
import torch
print("== PyTorch", torch.__version__, "· GPU del Mac (MPS):", "sí" if torch.backends.mps.is_available() else "NO")
EOF

# 3. Cuenta de Hugging Face (el modelo pide aceptar sus condiciones).
if ! .venv/bin/python -c "from huggingface_hub import get_token; import sys; sys.exit(0 if get_token() else 1)"; then
    echo
    echo "== Falta el token de Hugging Face."
    echo "   Antes: acepta las condiciones en https://huggingface.co/stabilityai/stable-audio-open-1.0"
    echo "   y crea un token de lectura en https://huggingface.co/settings/tokens"
    echo "   Pégalo cuando te lo pida (no se verá al escribir). Se guarda en tu Mac, nunca en el repositorio."
    .venv/bin/python -c "from huggingface_hub import login; login()"
fi

# 4. Descargar ya los pesos del autoencoder (~600 MB), para no esperar luego.
echo "== Descargando el autoencoder de Stable Audio Open (solo la primera vez)"
.venv/bin/python - <<'EOF'
from vaesynth_exp.latent import LatentModel
m = LatentModel()
print("== Modelo listo:", m.name, "en", m.device)
EOF

# 5. Configuración del motor (B, s, d) calculada en la ronda 1.
if ls configs/config_v*.json >/dev/null 2>&1; then
    echo "== Configuraciones disponibles: $(ls configs/config_v*.json | xargs -n1 basename | tr '\n' ' ')"
else
    echo "!! Falta configs/config_v001.json (la de la ronda 1). Pídesela a Fer: sin ella el servidor no arranca." >&2
fi

echo
echo "Instalación terminada. Siguiente paso:  bash mac/medir_velocidad.sh"
