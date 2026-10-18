# VAESynth

Sintetizador granular cuyo espacio de sonido sale de un VAE (deep learning) y se
navega como una esfera 3D. Formatos: VST3, AU, CLAP y Standalone, para macOS y Windows.

Diseño completo: [`docs/DESIGN.md`](docs/DESIGN.md).

> **Estado:** esqueleto. El plugin carga en el DAW y suena un **tono de prueba**
> (una onda senoidal por nota MIDI) para comprobar que todo funciona. El motor
> granular llega en la v0.

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
