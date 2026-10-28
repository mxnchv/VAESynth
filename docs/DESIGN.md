# VAESynth — Documento de diseño

Estado: **v0.2 · concepto "planta"**. Sustituye al diseño de la esfera granular (v0.1).
Lo marcado como *provisional* se decidirá probando la beta.

Mockup interactivo de referencia: "VAESynth Plant" (enlace privado de claude.ai, compartido desde Share).

---

## 1. Qué es

VAESynth es un **instrumento para encontrar un sonido y tocarlo**.

- **La semilla es un sonido**: uno de la biblioteca o un one-shot tuyo arrastrado al plugin.
- La semilla **crece en una planta**. Cada **brote es una variación** de la semilla: cuanto más arriba, más se aleja de ella.
- **Pasar el ratón** por los brotes los toca, siempre en escala.
- **Clic** elige un brote: es el sonido que tocas por MIDI.
- **Plantar** un brote (doble clic) hace crecer una planta nueva a partir de él. Las semillas anteriores quedan **enterradas** bajo la planta y se puede volver a ellas.

Las variaciones las genera un **motor de variaciones** intercambiable: en la beta, con DSP; más adelante, con un **VAE** (ver [`VAE.md`](VAE.md)).

### Encaje en la familia mxnchv

| Plugin | Metáfora | Qué hace |
|---|---|---|
| Midirain | Lluvia | Genera notas |
| Seed | Jardín de semillas | Genera notas |
| **VAESynth** | **Planta de sonidos** | **Da el sonido** |

Midirain y Seed componen; VAESynth suena. Su MIDI puede tocar VAESynth.

## 2. Filosofía

La misma que Midirain y Seed:

- **Funciona en cuanto lo abres** y suena bien desde el primer clic.
- **Una metáfora visual**: todo se entiende mirando la planta. Sin ADSR, sin LFO, sin menús escondidos.
- **Pocos controles**, ninguno obligatorio.
- **Nunca desafina ni hace ruido feo**: siempre en escala, con limitador al final.
- **Estética mxnchv**: fondo negro, blancos con transparencias, Jost, trazos finos, animaciones tranquilas. Los controles desaparecen cuando el ratón descansa.

## 3. Plataformas y tecnología

| | |
|---|---|
| Sistemas | macOS (universal) y Windows |
| Formatos | VST3, AU, CLAP y Standalone |
| Framework | JUCE 9 con CMake |
| Licencia de JUCE | Starter mientras no haya ingresos. Revisar antes de vender |
| Inferencia DL (futuro) | Por decidir: ONNX Runtime o RTNeural (ver `VAE.md`) |
| Idioma de la interfaz | Inglés |

## 4. La planta

- **Semilla** abajo en el centro. De ella salen **3 ramas**, que se dividen en 2 o 3 hasta **4 niveles**: unos **40–70 brotes**.
- **Posición = significado**:
  - **Más arriba**: más lejos de la semilla (más pasos de mutación).
  - **Derecha**: más brillante y metálico. **Izquierda**: más oscuro y suave.
- La planta **crece con una animación** de 2–3 segundos. Ese tiempo también sirve para calcular los brotes en segundo plano (importante para el VAE).
- Las ramas **se mecen** levemente. Se respeta "reducir movimiento" del sistema.
- **Aspecto de los brotes: puntos** (decidido frente a la forma de onda). Al sonar, una **onda de agua** sale del punto. El brote elegido lleva un anillo.

## 5. Interacción

| Gesto | Resultado |
|---|---|
| Pasar el ratón por un brote | Suena, en escala. Más arriba en pantalla, más agudo, alrededor de la nota de la semilla |
| Clic en un brote | Lo elige y suena en su nota base. Es el sonido que toca el MIDI |
| Doble clic / **Plant this** | El brote pasa a ser la semilla y crece una planta nueva |
| Clic en una semilla enterrada | Vuelve a esa planta |
| **Back** / ⌘Z | Vuelve a la semilla anterior |
| **Regrow** | Otra planta distinta a partir de la misma semilla |
| **New seed** | Biblioteca o "usa tu sonido" |
| Arrastrar un audio | Ese sonido pasa a ser la semilla |

Sin brote elegido, el MIDI toca la semilla.

## 6. La semilla

- **Tu sonido**: one-shot tonal (WAV, AIFF, FLAC, OGG, MP3, CAF), hasta ~10 s (*provisional*).
  - **La semilla suena exactamente como el archivo**: sin filtros ni envolvente.
  - **Nota base detectada automáticamente** (YIN) y editable. Si no tiene altura clara, C4.
- **Biblioteca**: unas pocas semillas de partida (Glass, Keys, Pluck, Bell, Breath…) para que suene sin cargar nada. *Provisional*: sinte interno o samples propios incluidos.

## 7. Motor de variaciones

Pieza aislada del resto del plugin. Contrato:

- **Mutar**: a partir de un nodo (semilla o brote), una dirección (izquierda/derecha), una cantidad (Wild × profundidad) y una semilla aleatoria, devuelve un nodo hijo.
- **Renderizar**: a partir de un nodo y una nota, devuelve el audio del one-shot.
- Se ejecuta **fuera del hilo de audio**. El hilo de audio solo reproduce buffers ya calculados.
- **Determinista**: la misma semilla y la misma semilla aleatoria dan la misma planta (para recuperar la sesión).

**Beta (DSP)**: cada nodo es un vector de rasgos 0–1 que se aplican al sonido de la semilla:

| Rasgo | Efecto sobre tu sonido |
|---|---|
| Bright | Low-pass más cerrado (oscuro) o high-shelf + high-pass (brillante) |
| Metal | Modulación en anillo |
| Body | Resonancia (filtro peaking) |
| Attack | Fundido de entrada |
| Decay | Recorta la cola |
| Air | Capa de ruido filtrado |
| Wide | Copia desafinada y abierta en estéreo |
| Shift | Capa una octava arriba o abajo |
| Reverse | Versión invertida |

Para las semillas de la biblioteca, los mismos rasgos controlan el sinte interno.

**Futuro (VAE)**: el nodo es un punto del espacio latente. Ver `VAE.md`.

## 8. Controles

| Control | Qué hace |
|---|---|
| Key · Scale | Escala del barrido y del teclado |
| Wild | Cuánto se alejan los brotes de la semilla |
| Space | Reverb |
| Volume | Volumen de salida |
| Regrow · New seed · Plant this · Back | Acciones de la planta |

- **MIDI**: polifonía de 16 voces (*provisional*), velocity → volumen.
- **Automatizable**: Wild, Space, Volume.

## 9. Sesión del DAW

Se guarda: la semilla (ruta del archivo, *provisional*: incrustar el audio), el historial de semillas, las semillas aleatorias, el brote elegido y los parámetros.

## 10. Qué se deja del diseño anterior

- **Fuera**: esfera, motor granular, trayectorias, knobs de arco, pestañas, mapa congelado.
- **Se reaprovecha** del código v0/v0.2: detección de nota (YIN), escalas, carga de audio, tema y fuentes, CI e instalador.

## 11. Plan

| Fase | Contenido |
|---|---|
| **Beta** | Planta con puntos, semilla de tu sonido o de la biblioteca, variaciones DSP, historial, MIDI, Key/Scale, Wild, Space, Volume |
| **En paralelo** | Investigación del VAE (`VAE.md`) |
| **v1** | Motor de variaciones con VAE detrás de la misma interfaz |

## 12. Preguntas abiertas

- ¿La planta debe sonar sola a veces (como Seed) o solo es un instrumento?
- Biblioteca de semillas: ¿sinte interno o samples incluidos?
- ¿Incrustar el audio de la semilla en la sesión o guardar la ruta?
- ¿Guardar brotes favoritos (un kit) además de la semilla actual?
- Nombre definitivo del producto.
