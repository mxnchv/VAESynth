# Motor experimental con un modelo preentrenado

Experimento para oír **plantas cuyos brotes salen de un espacio latente aprendido**, antes de diseñar y entrenar un modelo propio. Usa el VAE de Stable Audio Open como motor provisional.

Está pensado como un USB: se enchufa, se prueba y se quita sin que cambie nada del plugin.

## Reglas

Las reglas y los ajustes acordados están en [`../README.md`](../README.md) (`experimental/README.md`). Si algo de este archivo lo contradice, manda aquel.

- **Paso actual: 1 (cuaderno de escucha). No se ha tocado ningún archivo del plugin.**
- Los pesos del modelo, los tokens y los audios de la librería **nunca** se suben al repositorio.
- Sin RAVE por ahora, por su licencia.
- **Comparar con la beta DSP:** las mismas semillas se escuchan en el plugin actual cargándolas como semilla. El cuaderno no reproduce el motor DSP, que está en C++.

## Antes de empezar: licencia

El VAE de Stable Audio Open está bajo la **Stability AI Community License**: es gratuita por debajo de 1 M$ de ingresos anuales, y pide registrarse con Stability cuando la investigación apoya un producto comercial.

1. Registrarse con Stability (https://stability.ai/membership).
2. En Hugging Face, aceptar las condiciones de `stabilityai/stable-audio-open-1.0` y crear un token de lectura.
3. Dar el token al cuaderno: variable de entorno `HF_TOKEN` o, en Colab, un secreto llamado `HF_TOKEN`.

Uso interno: nada de esto se distribuye.

## Qué hay aquí

| Ruta | Qué es |
|---|---|
| `notebooks/01_escucha_plantas.ipynb` | El cuaderno del paso 1. `01_escucha_plantas.py` es la misma versión en texto, para revisar cambios |
| `vaesynth_exp/plant.py` | Port a Python de `Plant::grow` (mismo generador mulberry32, misma topología, mínimo de Wild) y de la mutación del motor experimental |
| `vaesynth_exp/latent.py` | Carga solo el autoencoder (`AutoencoderOobleck`), codifica con la media del posterior y decodifica sin ruido |
| `vaesynth_exp/config.py` | Calcula y guarda la configuración del motor (B, s, d) con versiones |
| `vaesynth_exp/audio.py` | Carga de sonidos, formato de los brotes y las dos medidas (brillo y distancia espectral) |
| `vaesynth_exp/listen.py` | Hace crecer plantas, las escribe como WAV y genera la página de escucha |
| `configs/` | Configuraciones versionadas `config_vNNN.json` |
| `tests/` | Comprobaciones del port y del versionado (`python -m pytest tests`) |

## Cómo funciona

**Coordenadas.** Cada nodo tiene 11 valores en [0, 1], igual que los `Traits` del plugin. Aquí son coordenadas en las 11 direcciones principales del latente, y 0,5 significa "sin desplazamiento":

```
desplazamiento = B @ ((coords - 0.5) * s)            (64 canales del latente)
latente del brote[:, t] = latente de la semilla[:, t] + desplazamiento   (para todos los frames t)
```

- Cuando el padre es la semilla (`sampleNeutral` o un patch de la biblioteca), sus hijos parten de 0,5. La semilla suena siempre como el archivo.
- Plantar un brote = hacer crecer la planta desde sus coordenadas. El render sigue siendo "latente de la semilla original + desplazamiento".

**Mutación** (`MutationSettings`), por cada coordenada:

```
hijo = clip(padre + N(0,1)·amount·sigma + leash·(0.5 − padre) + lean·amount·lean_k·d, 0, 1)
```

- `amount` y `lean` salen de la planta exactamente como en el plugin.
- `leash` es la "correa" hacia la semilla (P2). Con 0, el paseo es libre.

**Configuración (B, s, d).** Se calcula codificando una muestra de la librería:

- B: PCA de los latentes promediados en el tiempo.
- s: escala para que las coordenadas 0 y 1 queden a ±`SPREAD` desviaciones típicas de la semilla.
- d: la dirección en la que los sonidos suben de centroide espectral (regresión lineal). Es la "derecha" de la planta.

**Las configuraciones tienen versión.** Cada configuración nueva se guarda como `config_vNNN.json` y un archivo existente no se sobrescribe nunca: el guardado falla antes que pisarlo. Cada carpeta de resultados lleva una copia de la configuración que usó.

**Brotes.** Tienen los mismos canales que la semilla (estéreo si la semilla es estéreo), duran como mucho lo que la semilla y 4,5 s, y nunca superan un pico de 0,95.

## Fuente de sonidos: NSynth

Por decisión del equipo, el experimento usa **NSynth** (Magenta, licencia CC BY 4.0) y no la librería propia. El cuaderno lo descarga solo (`FUENTE = "nsynth"`, por defecto).

- Se usa la parte `test`: 4.096 notas, la descarga más pequeña. La parte `train` ocupa decenas de GB y no hace falta.
- **Configuración (B, s, d):** `N_CONFIG_SOUNDS` notas repartidas por igual entre familias de instrumento, entre MIDI 36 y 84.
- **Semillas:** por defecto, una nota por familia cerca de MIDI 60 (velocidad 100 si existe). Se pueden elegir con `SEEDS`, por nombre (por ejemplo `"guitar_acoustic_010-060-100"`).
- **Limitación importante:** NSynth está a 16 kHz, así que **no hay nada por encima de 8 kHz**. Las notas se convierten a 44,1 kHz para el VAE, pero seguirán sonando más apagadas que las semillas de la beta. Por eso:
  - La dirección "brillante" (`d`) se aprende solo con el contenido por debajo de 8 kHz.
  - La comparación con la beta no es justa en brillo; sí en movimiento, variedad y parecido con la semilla.
- Todas las notas duran 4 s, son tonales y son de instrumentos (no hay voces ni objetos). El latente se organiza por timbres de instrumento.
- La URL de descarga directa no está comprobada desde esta sesión. Si falla, descarga `nsynth-test.jsonwav.tar.gz` desde https://magenta.tensorflow.org/datasets/nsynth y pon su ruta en `NSYNTH_ARCHIVE`.
- Para volver a sonidos propios: `FUENTE = "carpeta"` y `SOUNDS_DIR`.

## Cómo usarlo

1. Tener esta carpeta en Google Drive (clonando la rama en `MyDrive/<cualquier nombre>`; el cuaderno la encuentra solo) o en local.
2. Abrir el cuaderno en Colab con GPU (o en local).
3. Ejecutarlo. Descarga NSynth, calcula una configuración nueva y hace crecer las plantas de las semillas con Wild 0, 0,5 y 1.
4. Abrir `OUTPUT_DIR/config_vNNN/index.html`. Cada planta tiene su página: la semilla, la reconstrucción D(E(x)) y los brotes por nivel, ordenados de izquierda (oscuro) a derecha (brillante).

Para comprobar que todo corre sin pesos reales: `VAESYNTH_SMOKE=1 python notebooks/01_escucha_plantas.py`. Usa pesos aleatorios y sonidos sintéticos, así que suena a ruido.

**Velocidad.** Con GPU (Colab) va bien. En CPU es lento. En la prueba de humo (CPU de un servidor en la nube, pesos aleatorios), 4 plantas de 80 brotes con sonidos de 1–4 s tardaron unos 20 minutos: varios segundos por brote. Es una cifra orientativa, no una medición en Mac ni en Windows, pero confirma que este modelo sirve para escuchar y no para el presupuesto del plugin (30–50 ms por brote).

## Qué mirar al escuchar

| Cifra | Qué indica |
|---|---|
| `reconstruction_gap` | El salto en la base de la planta: cuánto difiere D(E(semilla)) de la semilla |
| `mean_dist_seed_by_depth` | Debería crecer con el nivel y con Wild |
| `lean_vs_brightness_corr` | Mayor que 0 si los brotes de la derecha son de verdad más brillantes que su padre |
| `brightness_r2` (de la configuración) | Si es muy bajo, `lean` tiene poco sentido con este latente |

Las cifras orientan; decide el oído. Conviene apuntar:

- Qué sorprende, qué aburre y qué suena roto.
- En qué semillas falla.
- Desde qué nivel deja de reconocerse la semilla.
- Si la derecha suena más brillante.

## Para los pasos 2–3 (servidor y adaptador en el plugin)

Indicaciones del colega, a respetar cuando se llegue ahí:

- Servidor en `127.0.0.1`, no en `localhost`.
- Si el servidor falla, `render()` devuelve `nullptr`: el brote se queda apagado (ya está soportado).
- Si la semilla es estéreo, el brote también (como `renderSample`).
- Al soltar Wild se recalcula toda la planta: con un modelo lento, habrá esperas.
- El adaptador lee la configuración por versión y no la sobrescribe.
