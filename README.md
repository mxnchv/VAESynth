# VAESynth

Sintetizador granular cuyo espacio de sonido sale de un VAE (deep learning) y se
navega como una esfera 3D. Formatos: VST3, AU, CLAP y Standalone, para macOS y Windows.

Diseño completo: [`docs/DESIGN.md`](docs/DESIGN.md).

> **Estado: prototipo v0.2.** Carga samples, coloca sus granos en la esfera y
> los toca con MIDI. Todavía **sin deep learning**: los granos se colocan con
> descriptores de audio (volumen, brillo, ruido…) y solo suenan granos reales.
> El VAE llegará en la v1.

## Cómo se usa

1. Carga VAESynth como **instrumento** en el DAW (o abre el Standalone).
2. **Arrastra uno o varios archivos de audio** (WAV, AIFF, FLAC, OGG, MP3, CAF) a la
   ventana, o usa **+ Add sample**. Se analizan en segundo plano y sus granos
   aparecen en la esfera, un color por sample.
3. **Toca notas MIDI.** Suenan los granos más cercanos al cursor (el punto blanco),
   que se iluminan al sonar.

### La esfera
- **Arrastra** sobre ella para mover el cursor (si había una trayectoria, pasa a Manual).
- **Rueda del ratón**: radio, de −1,5 a +1,5. Negativo es el lado opuesto (antípoda);
  cerca de 0 se oye la mezcla de todos los samples.
- **Clic en un punto**: escuchas ese grano tal cual.
- **Alt + arrastrar** (o clic derecho): girar la cámara. **3D / Top / Front** cambian la vista.

### Pestañas
- **Space**: Azimuth, Elevation, Radius y la trayectoria (**Orbit** / **Lissajous**).
  **Speed** puede ir libre o en **Sync** (una vuelta cada N compases, enganchada a
  la posición de la canción).
- **Grains**: **Size** y **Density** en **Free** (ms, granos/s) o **Sync** (1/4, 1/8T…
  alineados con la rejilla del DAW); Spray, Shape y Neighbours.
- **Pitch**:
  - **Transpose**: transpone cada sample desde su nota base (se detecta sola).
  - **Texture**: los granos conservan su altura; con escala se ajustan a ella.
  - **Tune**: cada grano con altura clara suena exactamente en la nota que tocas.
  - **Key / Scale**: las notas se ajustan a la escala elegida.
  - **Harmonic filter**: prefiere granos cuyo contenido ya encaja con la escala.
- **Voice**: ADSR, Width, Gain y Velocity → Radius.
- **Samples**: nota base (− / +), **M**ute, **S**olo y **×** para quitar cada sample;
  **Clear all** los quita todos.

### Detalles
- El mapa se **fija** tras la primera carga: añadir o quitar samples no mueve a los
  demás. **Rebuild map** lo recalcula (la automatización sonará distinto).
- La sesión del DAW guarda los samples (por ruta), el mapa, notas base, mute/solo y
  todos los parámetros. Si mueves o borras un archivo, aparecerá como *Missing sample*.
- Sin DAW (Standalone) el tempo es 120 BPM.
- Límite actual: 32 samples, hasta 2 minutos cada uno.

## Descargar una versión compilada

Cada push y cada PR compilan automáticamente para Mac y Windows:

1. En GitHub, pestaña **Actions** → la ejecución más reciente de **Build**.
2. Abajo, en **Artifacts**, descarga `VAESynth-macOS` o `VAESynth-Windows`.

### Instalar en macOS

Descomprime el zip (doble clic) y en el Terminal:

```sh
bash ~/Downloads/VAESynth-macOS/install.sh
```

Copia VST3, AU, CLAP y la app Standalone a su sitio y deja todo listo para que
macOS los cargue (la compilación no está notarizada). Después reinicia el DAW y
haz un rescan de plugins.

### Instalar en Windows

| Formato | Carpeta |
|---|---|
| VST3 | `C:\Program Files\Common Files\VST3\` |
| CLAP | `C:\Program Files\Common Files\CLAP\` |

SmartScreen puede avisar al abrir el Standalone; elige "Más información → Ejecutar de todas formas".

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
