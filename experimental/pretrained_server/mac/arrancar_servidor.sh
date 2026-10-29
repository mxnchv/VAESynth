#!/usr/bin/env bash
# Arranca el servidor experimental en 127.0.0.1:8765. Déjalo abierto mientras pruebas el plugin.
# Uso:   bash experimental/pretrained_server/mac/arrancar_servidor.sh   (Ctrl+C para parar)
#
# Por defecto, lo que decidió Monchu tras medir en su M1: CPU y brotes de hasta 3 s.
# Cualquier opción que añadas manda sobre estas, p. ej.:
#   --max-seconds 4.5   --device mps   --port 8766   --config configs/config_v001.json   --threads 4
set -euo pipefail
# Operaciones que la GPU del Mac aún no tenga: que caigan a la CPU en vez de fallar.
export PYTORCH_ENABLE_MPS_FALLBACK=1
cd "$(dirname "$0")/.."
exec .venv/bin/python -m vaesynth_exp.server --device cpu --max-seconds 3 "$@"
