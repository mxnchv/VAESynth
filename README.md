# VAESynth

Sintetizador granular cuyo espacio de sonido sale de un VAE (deep learning) y se
navega como una esfera 3D. Formatos: VST3, AU, CLAP y Standalone, para macOS y Windows.

Diseño completo: [`docs/DESIGN.md`](docs/DESIGN.md).

> **Estado: prototipo v0.** Ya suena: carga samples, coloca sus granos en la
> esfera y los toca con MIDI. Todavía **sin deep learning**: los granos se colocan
> con descriptores de audio (volumen, brillo, ruido…) y solo suenan granos reales.
> El VAE llegará en la v1.

## Cómo se usa (v0)

1. Carga VAESynth como **instrumento** en el DAW (o abre el Standalone).
2. **Arrastra uno o varios archivos de audio** (WAV, AIFF, FLAC, OGG, MP3) a la
   ventana, o usa **+ Add sample**. Se analizan en segundo plano y sus granos
   aparecen en la esfera, un color por sample.
3. **Toca notas MIDI.** Suenan los granos más cercanos al cursor (el punto blanco).
4. Muévete por la esfera con **Azimuth**, **Elevation** y **Radius** (pestaña *Space*),
   o activa una trayectoria (**Orbit** / **Lissajous**).

Detalles:
- **Radius** va de −1,5 a +1,5: negativo es el lado opuesto (antípoda) y cerca de 0
  se oye la mezcla de todos los samples.
- Arrastra sobre la esfera para girar la cámara; **3D / Top / Front** cambian la vista.
- El mapa se **fija** tras la primera carga: los samples que añadas después no mueven
  a los anteriores. **Rebuild map** lo recalcula (la automatización sonará distinto).
- Pestaña *Pitch*: **Transpose** transpone según la nota base de cada sample (C3 por
  defecto, ajustable con − / +); **Texture** no transpone.
- La sesión del DAW guarda los samples (por ruta), el mapa y todos los parámetros.
  Si mueves o borras un archivo, aparecerá como *Missing sample*.
- Límite actual: 32 samples, hasta 2 minutos cada uno.

## Descargar una versión compilada

Cada push y cada PR compilan automáticamente para Mac y Windows:

1. En GitHub, pestaña **Actions** → la ejecución más reciente de **Build**.
2. Abajo, en **Artifacts**, descarga `VAESynth-macOS` o `VAESynth-Windows`.

### Instalar

| Formato | macOS | Windows |
|---|---|---|
| VST3 | `~/Library/Audio/Plug-Ins/VST3/` | `C:\Program Files\Common Files\VST3\` |
| AU | `~/Library/Audio/Plug-Ins/Components/` | — |
| CLAP | `~/Library/Audio/Plug-Ins/CLAP/` | `C:\Program Files\Common Files\CLAP\` |

Las compilaciones **no están firmadas**:
- **macOS**: tras copiar los plugins, quita la cuarentena:
  `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/*/VAESynth.*`
  Para que Logic detecte el AU nuevo puede hacer falta reiniciar o ejecutar `killall -9 AudioComponentRegistrar`.
- **Windows**: SmartScreen puede avisar al abrir el Standalone; elige "Más información → Ejecutar de todas formas".

## Compilar en local

Requisitos: CMake 3.22+ y Xcode (macOS) o Visual Studio 2022 (Windows).
JUCE 9 y clap-juce-extensions se descargan solos en el primer configure.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

Los plugins quedan en `build/VAESynth_artefacts/Release/`.

Pruebas del motor y del procesador (sin DAW):

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DVAESYNTH_BUILD_TESTS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

La tipografía Jost se distribuye bajo la SIL Open Font License (`assets/fonts/OFL.txt`).
