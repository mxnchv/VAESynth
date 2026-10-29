#!/usr/bin/env bash
# Compila VAESynth con el motor experimental, SOLO como app Standalone y dentro de
# build/experimental de esta copia del experimento. No instala nada: el VAESynth
# normal de tus DAW no cambia.
#
# Uso, desde la carpeta del experimento (VAESynth-experimento):
#   bash experimental/pretrained_server/mac/compilar_plugin_experimental.sh
set -euo pipefail
cd "$(dirname "$0")/../../.."

echo "== Configurando (opción VAESYNTH_EXPERIMENTAL_PRETRAINED encendida)"
cmake -B build/experimental -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$(uname -m)" \
      -DVAESYNTH_EXPERIMENTAL_PRETRAINED=ON -DVAESYNTH_BUILD_TESTS=ON

echo "== Compilando la app Standalone y los tests del motor experimental"
cmake --build build/experimental --config Release --target VAESynth_Standalone VAESynthExperimentalTests \
      --parallel "$(sysctl -n hw.ncpu)"

tests=build/experimental/VAESynthExperimentalTests_artefacts/Release/VAESynthExperimentalTests
echo "== Tests del motor experimental"
if curl -s --max-time 2 http://127.0.0.1:8765/info >/dev/null; then
    VAESYNTH_TEST_PORT=8765 "$tests"
else
    echo "(El servidor no está arrancado: solo se prueba lo que no lo necesita.)"
    "$tests"
fi

app=build/experimental/VAESynth_artefacts/Release/Standalone/VAESynth.app
echo
echo "Listo. Con el servidor arrancado (mac/arrancar_servidor.sh), abre la app:"
echo "  open \"$app\""
