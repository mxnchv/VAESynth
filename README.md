# VAESynth

Un instrumento para encontrar un sonido y tocarlo: **la semilla es un sonido y crece
en una planta de variaciones**. Formatos: VST3, AU, CLAP y Standalone, para macOS y Windows.

Diseño: [`docs/DESIGN.md`](docs/DESIGN.md). Encaje del futuro VAE: [`docs/VAE.md`](docs/VAE.md).

> **Estado: beta 0.3.** Las variaciones se hacen con DSP; el VAE llegará más adelante
> detrás de la misma interfaz.

## Cómo se usa

1. Carga VAESynth como **instrumento** en el DAW (o abre el Standalone). Arranca con
   una planta ya crecida a partir de la semilla *Glass*.
2. **Pasa el ratón por los brotes**: suenan, siempre en la escala elegida. Más arriba
   en la pantalla, más agudo.
3. **Clic en un brote** para elegirlo: es el sonido que toca tu MIDI. Sin nada elegido,
   el MIDI toca la semilla.
4. **Doble clic** (o *Plant this*) planta el brote: crece una planta nueva a partir de él.
   Las semillas anteriores quedan **bajo tierra**: clic en ellas para volver (o *Back*).
5. **New seed**: otra semilla de la biblioteca (Glass, Keys, Pluck, Bell, Breath) o
   **tu propio sonido**. También puedes **arrastrar un one-shot** a la ventana.

### La planta
- **Más arriba**: más lejos de la semilla. **Derecha**: más brillante y metálico.
  **Izquierda**: más oscuro y suave.
- Con tu sonido, **la semilla es exactamente tu archivo**, en su nota (se detecta sola).
  Los brotes le aplican filtro, envolvente, octavas, inversión, modulación en anillo,
  resonancia, aire y ensanchado.

### Controles
- **Key / Scale**: escala del barrido y del teclado (las notas MIDI se ajustan a ella;
  *Off* las deja pasar todas).
- **Wild**: cuánto se alejan los brotes de la semilla (se aplica al crecer o al pulsar **Regrow**).
- **Space**: reverb. **Volume**: volumen de salida (con limitador).
- **Regrow**: otra planta distinta a partir de la misma semilla.

La sesión del DAW guarda el historial de semillas, la planta, el brote elegido y los
parámetros. Los sonidos propios se guardan por ruta: si mueves el archivo, aparecerá
como *Missing*.

## Probarlo

### En tu Mac, compilando (recomendado para iterar)

Necesitas Xcode (App Store) y CMake (`brew install cmake`). Desde la carpeta del repo:

```sh
git pull
bash packaging/macos/build-and-install.sh
```

Compila e instala VST3, AU, CLAP y la app Standalone. La primera vez tarda unos
minutos (descarga JUCE); después, segundos. Reinicia el DAW y haz un rescan.

### Descargando una versión publicada (recomendado si no compilas)

En GitHub, columna de la derecha → **Releases**. Cada versión (por ejemplo `beta-0.3`)
tiene `VAESynth-macOS.zip` y `VAESynth-Windows.zip`, y no caducan: es el punto de retorno
si una versión nueva no os convence. Se instalan igual que lo de abajo.

### Descargando la versión que compila GitHub en cada cambio

1. En GitHub, pestaña **Actions** → la ejecución más reciente de **Build** de tu rama.
2. Abajo, en **Artifacts**, descarga `VAESynth-macOS` o `VAESynth-Windows`.
3. **Mac**: descomprime y ejecuta `bash ~/Downloads/VAESynth-macOS/install.sh`.
   **Windows**: copia `VAESynth.vst3` a `C:\Program Files\Common Files\VST3\` y
   `VAESynth.clap` a `C:\Program Files\Common Files\CLAP\`.

### En Windows, compilando

Visual Studio 2022 (con "Desarrollo para escritorio con C++") y CMake:

```sh
cmake -B build
cmake --build build --config Release --parallel
```

Los plugins quedan en `build/VAESynth_artefacts/Release/`.

## Tests

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DVAESYNTH_BUILD_TESTS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

## Código

| Carpeta | Qué hay |
|---|---|
| `Source/engine` | La planta (`Plant`), el motor de variaciones DSP (`VariationEngine`), la carga de sonidos y la detección de nota (`Sounds`), el sampler en tiempo real (`Sampler`) y las escalas |
| `Source/PlantSession` | Historial de semillas, planta actual y renderizado de los brotes en segundo plano |
| `Source/ui` | Tema (Jost, blanco y negro) y la vista de la planta |
| `tests` | Tests del motor y del procesador completo |

La tipografía Jost se distribuye bajo la SIL Open Font License (`assets/fonts/OFL.txt`).
