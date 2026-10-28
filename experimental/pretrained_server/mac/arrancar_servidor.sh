#!/usr/bin/env bash
# Arranca el servidor experimental en 127.0.0.1:8765. Déjalo abierto mientras pruebas el plugin.
# Uso:   bash experimental/pretrained_server/mac/arrancar_servidor.sh   (Ctrl+C para parar)
# Opciones: --port 8765  --config configs/config_v001.json  --device mps|cpu
set -euo pipefail
# Operaciones que la GPU del Mac aún no tenga: que caigan a la CPU en vez de fallar.
export PYTORCH_ENABLE_MPS_FALLBACK=1
cd "$(dirname "$0")/.."
exec .venv/bin/python -m vaesynth_exp.server "$@"
