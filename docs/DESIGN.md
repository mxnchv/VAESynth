# VAESynth — Documento de diseño

Estado: **borrador v0.1** · Recoge las decisiones tomadas antes de empezar a programar.
Lo marcado como *provisional* se validará escuchando el prototipo.

---

## 1. Qué es

VAESynth es un **sintetizador granular** cuyo espacio de sonido sale de un
**modelo de deep learning (VAE)**. El usuario carga samples, el modelo los
coloca en un espacio latente y ese espacio se navega como una **esfera 3D**.
Moverse por la esfera, a mano o con trayectorias automáticas, cambia los granos
que suenan.

## 2. Plataformas y tecnología

| | |
|---|---|
| Sistemas | macOS y Windows |
| Formatos | VST3, AU, CLAP y Standalone |
| Framework | JUCE con CMake |
| Licencia de JUCE | Starter (gratis, hasta 20.000 $ de ingresos). Hay que comprobar si el límite cuenta los ingresos totales de quien licencia y no los de cada plugin |
| Inferencia DL | Por decidir: RTNeural o ONNX Runtime (licencias permisivas) |
| Idioma de la interfaz | Inglés |

## 3. Inputs

- **N samples** arrastrados al plugin. Se diseña para N desde el principio, y 1 es un caso particular.
- Entrada de audio en directo: **futuro**.
- El límite de samples lo pone el **total de granos** (memoria, tiempo de codificación y legibilidad), no el número de samples. Se medirá con el prototipo.
- Cargar un sample tiene que tardar **entre nada y unos pocos segundos**.

## 4. Modelo de deep learning

- **VAE de granos preentrenado offline** con un dataset amplio, incluido en el plugin.
- Al cargar un sample se trocea en granos y el encoder los codifica. Es casi instantáneo y **no se entrena nada dentro del plugin**.
- **Modo híbrido**: cerca de un grano real suena el grano original y, entre granos, lo que genera el decoder. El control **Real · Generated** decide la proporción.
- Descartado para el núcleo: **difusión** (lenta y con un espacio poco navegable) y el **fine-tuning dentro del plugin** (demasiado lento).
- Futuro opcional: cargar modelos **RAVE** preentrenados como bancos de timbre.

## 5. De espacio latente a esfera

- El encoder da a cada grano una posición fija en el espacio latente completo.
- Para la esfera se proyecta a 3D (PCA) con los samples cargados.
- La proyección **se congela** una vez calculada. Los samples nuevos se colocan sin mover los anteriores.
- El botón **Rebuild map** recalcula la proyección, avisando de que la automatización y las trayectorias cambiarán de sonido.
- La proyección se guarda en el **preset y en el estado de la sesión del DAW**.

## 6. Navegación

Coordenadas esféricas. Las tres son parámetros automatizables del DAW.

| Eje | Parámetro | Rango |
|---|---|---|
| X | Azimuth | −180° a 180° |
| Y | Elevation | −90° a 90° |
| Z | Radius | −1,5 R a +1,5 R |

- **R = 1** es la superficie. Más allá de ±1 está la zona de **extrapolación**.
- Un **radio negativo** corresponde al **antípoda**: al automatizar Z de +R a −R, el cursor atraviesa la esfera por el centro.
- **Centro = promedio** de todos los granos (*provisional*).
- **Trayectorias**: Manual, Orbit y Lissajous, con control de velocidad y sincronía con el tempo. Dibujar o grabar trayectorias queda para el futuro.

## 7. Motor granular (v1)

| Parámetro | Descripción |
|---|---|
| Size | Duración del grano, 5–500 ms |
| Density | Granos por segundo |
| Spray | Cada grano sale de un punto aleatorio del espacio alrededor del cursor |
| Real · Generated | Proporción entre granos reales y granos generados por el decoder |
| Shape | Envolvente del grano, de suave (Hann) a percusiva |
| Neighbours | Cuántos granos cercanos contribuyen al sonido |

Futuro: probabilidad de reverso.

## 8. Tono

- **Transpose / Texture**: en Transpose, la nota MIDI transpone los granos respecto a la nota base del sample. En Texture, el teclado no transpone.
- **Nota base por sample**: se detecta automáticamente y es editable.
- **Detune**: desafinación aleatoria por grano, en cents.
- Futuro: glide.

## 9. Voces

- **Cursor compartido**: todas las notas suenan desde el mismo punto de la esfera.
- **Velocity → Radius**: tocar fuerte empuja la nota hacia fuera.
- Polifonía inicial: **8 voces**, ampliable tras medir el rendimiento.
- ADSR de amplitud, Width (abertura estéreo) y Gain.
- Futuro: cursor por nota y MPE. No está descartado.

## 10. Interfaz

- Estilo cercano a APSIS: **fondo negro, todo en blanco y gris, líneas finas**.
- El **color se reserva para identificar los samples**.
- Tipografía: **Jost**.
- **Knobs de arco** sin indicador de punto.
- Vista principal: la esfera, con vistas **3D · Top · Front**.
- Panel con pestañas **Space · Grains · Pitch · Voice**.
- Barra inferior con los samples, **Add sample**, el indicador **Map locked** y **Rebuild map**.
- Referencia: mockup D del lienzo de mockups (enlace privado de claude.ai, compartido desde Share).

## 11. Plan

| Fase | Contenido |
|---|---|
| **v0** | Esqueleto del proyecto, compilación automática para Mac y Windows, motor granular y esfera con **granos reales** (sin DL). Para escuchar y validar la navegación |
| **En paralelo** | Cuaderno de Python: entrenar un VAE de granos pequeño y escuchar interpolaciones fuera del plugin |
| **v1** | Integrar el encoder y el decoder (modo híbrido) y el panel completo de controles |
| **v2+** | RAVE opcional, entrada en directo, cursor por nota y MPE, efectos integrados |

## 12. Preguntas abiertas

- Comportamiento definitivo del centro (promedio, caos u otro): decidirlo escuchando.
- Límite práctico de samples y granos, y polifonía máxima: medirlos.
- Dimensión del espacio latente y dataset de entrenamiento del VAE.
- Runtime de inferencia: RTNeural u ONNX Runtime.
- Licencia definitiva (JUCE Starter, Indie o AGPL) según el modelo de distribución.
