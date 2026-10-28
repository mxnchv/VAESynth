# VAESynth — Encaje del VAE

Para quien investigue el modelo. Explica **qué necesita el plugin** del VAE, no cómo hacerlo:
eso es lo que hay que investigar. Leer antes [`DESIGN.md`](DESIGN.md).

---

## 1. Qué papel tiene

En el plugin, el VAE es **una implementación del motor de variaciones** (DESIGN §7).
La beta funciona con DSP; el VAE lo sustituye sin que cambie el resto.

| Pieza del VAE | Uso en VAESynth |
|---|---|
| Encoder | Coloca la semilla (tu sonido) en el espacio latente |
| Espacio latente | Cada brote es un punto. Mutar = moverse desde el punto del padre |
| Decoder | Genera el audio de cada brote |
| Prior continuo | Que los puntos cercanos suenen parecido y el camino sea suave |

## 2. Contrato con el plugin

```cpp
// Pseudocódigo. El estado del nodo es opaco para el resto del plugin.
struct Node;                       // DSP: vector de rasgos. VAE: vector latente z

struct MutationRequest
{
    float lean;                    // -1 izquierda (oscuro, suave) … +1 derecha (brillante, metálico)
    float amount;                  // Wild × profundidad, 0..1 aprox.
    uint64_t rngSeed;              // determinismo
};

class VariationEngine
{
public:
    virtual Node seedFromAudio (const AudioBuffer& audio, double sampleRate, int rootNote) = 0; // encoder
    virtual Node mutate (const Node& parent, const MutationRequest&) = 0;
    virtual AudioBuffer render (const Node&, int midiNote, double sampleRate) = 0;              // decoder
};
```

Reglas:

- **Nunca en el hilo de audio.** Todo corre en un hilo de fondo; el audio solo reproduce buffers ya renderizados.
- **La semilla no pasa por el decoder.** Suena el archivo original. El VAE solo genera los brotes. Pero los brotes cercanos **tienen que parecerse** a la semilla, así que la codificación de sonidos ajenos al dataset importa.
- **Determinista**: misma semilla + mismo `rngSeed` + misma versión del modelo = misma planta.
- **`lean` debe significar algo**: derecha más brillante/metálico, izquierda más oscuro/suave. Hace falta alguna dirección del latente ligada a descriptores (ver §5).

## 3. Presupuesto

| | Objetivo | Por qué |
|---|---|---|
| Brotes por planta | 40–70 | DESIGN §4 |
| Planta completa | ≤ 2–3 s en CPU, en segundo plano | Lo que dura la animación de crecer |
| Un brote | ≤ ~30–50 ms en CPU | Se deduce de lo anterior |
| Duración del sonido | One-shots de hasta ~4 s (*provisional*) | Sonidos tonales que decaen |
| Frecuencia de muestreo de salida | ≥ 32 kHz, idealmente 44,1/48 kHz | A 16 kHz suena apagado |
| Canales | Mono basta | El estéreo lo pone el plugin |
| Altura | Condicionado por nota (MIDI) | El plugin pide la nota; timbre y altura separados |
| Tamaño del modelo | ≤ ~50 MB | Va dentro del plugin |
| Plataformas | macOS universal (arm64 + x86_64) y Windows x64 | Sin GPU |

## 4. Runtime en el plugin

| Opción | A favor | En contra |
|---|---|---|
| ONNX Runtime | Exportar desde PyTorch es directo; casi cualquier arquitectura | Librería grande; empaquetarla en Mac universal y Windows |
| RTNeural | Ligero, pensado para audio en C++ | Solo capas sencillas (dense, conv1d, GRU/LSTM) |
| libtorch | Todo PyTorch | Pesadísimo para un plugin. Descartado salvo sorpresa |

Decidirlo según la arquitectura elegida.

## 5. Líneas a investigar

- **Arquitecturas**
  - **VAE + sintetizador diferenciable (DDSP)**: el decoder predice amplitudes de armónicos y ruido filtrado, y la nota se da explícita. Es ligero, rápido en CPU y muy adecuado para one-shots tonales. **Buen primer candidato.** DDSP de Magenta es Apache 2.0.
  - **VAE sobre espectrograma + vocoder**: es flexible, pero el vocoder suele ser lo caro.
  - **VAE sobre forma de onda** (estilo RAVE): da buena calidad. **La licencia de RAVE es no comercial (CC BY-NC)**, así que solo se pueden tomar ideas, no su código ni sus modelos.
- **Dirección `lean`**: regresión de descriptores (centroide espectral, inarmonicidad, decaimiento) sobre el latente, o regularización por atributos (AR-VAE).
- **Sonidos fuera del dataset**: cómo se comporta el encoder con samples del usuario. Una idea: renderizar el brote como *semilla + (decoder(z') − decoder(z))* en dominio espectral, para que conserve el carácter del original.
- **Datos**
  - **NSynth**: ~300.000 notas de instrumentos con su nota MIDI. La licencia es CC BY 4.0, a confirmar.
  - Otros conjuntos de one-shots tonales, con la licencia revisada caso por caso.

## 6. Qué entregar para integrarlo

1. El modelo exportado (ONNX o pesos para RTNeural) y su versión.
2. Un script de Python de referencia: `encode`, `mutate` y `decode`.
3. **Vectores de prueba**: entradas y salidas esperadas, para verificar que el port a C++ da lo mismo.
4. Una página o cuaderno para escuchar reconstrucciones, plantas generadas y recorridos.
5. Las mediciones de tiempo por brote en CPU (Mac y Windows).
