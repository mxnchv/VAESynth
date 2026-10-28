#!/usr/bin/env bash
# Mide cuánto tarda un brote en este Mac, con la GPU (MPS) y con la CPU.
# Uso:   bash experimental/pretrained_server/mac/medir_velocidad.sh
set -euo pipefail
# Operaciones que la GPU del Mac aún no tenga: que caigan a la CPU en vez de fallar.
export PYTORCH_ENABLE_MPS_FALLBACK=1
cd "$(dirname "$0")/.."

echo "== GPU del Mac (MPS)"
.venv/bin/python -m vaesynth_exp.server --benchmark --device mps || echo "!! La medición con MPS ha fallado (copia el error para Fer)."
echo
echo "== CPU"
.venv/bin/python -m vaesynth_exp.server --benchmark --device cpu
echo
echo "Copia estos resultados y mándaselos a Fer."
