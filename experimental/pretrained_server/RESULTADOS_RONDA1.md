# Ronda 1 — Plantas de escucha con el VAE de Stable Audio Open

Fecha: 28–29 de octubre de 2026. Paso 1 del experimento (cuaderno offline, sin tocar el plugin).

## Decisión

**Aprobado por Fer y por Monchu (responsable del producto).** Wild 0 suena parecido a la semilla y Wild 1 suena a "demencia", que es lo que se busca. Se pasa al paso 2 (servidor local) y, si los tiempos en el M1 de Monchu son razonables, al paso 3 (adaptador en el plugin).

## Montaje

- **Modelo:** VAE de Stable Audio Open (`stabilityai/stable-audio-open-1.0`, subcarpeta `vae`). Solo el autoencoder. Licencia Stability AI Community.
- **Sonidos:** NSynth, parte `test` (4.096 notas, 16 kHz, CC BY 4.0). Se eligió NSynth en lugar de la librería de Monchu.
- **Configuración:** `config_v001.json`. Unas 400 notas repartidas entre familias de instrumento, MIDI 36–84, `SPREAD = 3`.
- **Mutación:** `sigma = 1.0`, `lean_k = 1.2`, `leash = 0` (sin correa hacia la semilla).
- **Semillas:** una nota por familia cerca de MIDI 60, en total 10. Cada una con Wild 0, 0,5 y 1, `rngSeed = 1`. Las 30 plantas tienen la misma forma (80 brotes), porque comparten `rngSeed`.
- **Entorno:** Google Colab con GPU.

## Cifras

La distancia es una distancia espectral logarítmica multirresolución (0 = idéntico). No tiene escala absoluta todavía (ver "Pendiente").

| Semilla | Salto semilla → reconstrucción | Wild 0: nivel 1 → nivel 4 | Wild 1: nivel 1 → nivel 4 | Correlación lean/brillo (Wild 0 · 0,5 · 1) |
|---|---|---|---|---|
| bass_synthetic_009-060-100 | 1,17 | 1,20 → 1,48 | 1,38 → 2,89 | 0,55 · 0,48 · 0,39 |
| brass_acoustic_006-060-100 | 1,17 | 1,22 → 1,63 | 1,59 → 3,09 | 0,61 · 0,02 · 0,00 |
| flute_synthetic_000-060-025 | 1,11 | 1,14 → 1,52 | 1,37 → 3,35 | 0,43 · −0,03 · −0,13 |
| guitar_acoustic_010-060-100 | 1,16 | 1,19 → 1,43 | 1,40 → 4,15 | 0,48 · −0,06 · −0,19 |
| keyboard_electronic_069-060-100 | 1,00 | 1,03 → 1,36 | 1,28 → 3,35 | 0,48 · −0,01 · −0,12 |
| mallet_acoustic_062-060-050 | 0,99 | 0,98 → 1,17 | 1,04 → 4,91 | −0,12 · −0,22 · −0,26 |
| organ_electronic_104-060-100 | 1,37 | 1,41 → 1,68 | 1,65 → 4,63 | 0,36 · −0,09 · −0,25 |
| reed_acoustic_037-060-100 | 1,35 | 1,42 → 1,82 | 1,77 → 3,12 | 0,50 · −0,03 · −0,08 |
| string_acoustic_057-060-100 | 1,18 | 1,21 → 1,59 | 1,44 → 3,21 | 0,52 · −0,01 · −0,18 |
| vocal_synthetic_003-060-100 | 1,51 | 1,53 → 1,65 | 1,71 → 3,01 | 0,55 · 0,51 · 0,47 |

## Conclusiones

**Lo que sí sabemos:**

1. **Moverse por un latente aprendido da el rango buscado**, de variaciones cercanas a sonidos muy lejanos, con un solo control. Confirmado por las cifras y por el oído.
2. **Wild y la profundidad funcionan como está diseñado.** La distancia a la semilla crece con el nivel y con Wild en las 10 semillas.
3. **El salto en la base de la planta es aceptable con notas de instrumentos.** Con Wild 0, los brotes se oyen parecidos a la semilla.
4. **El mecanismo del motor experimental es válido para llevarlo al plugin**: coordenadas en 11 direcciones, desplazamiento del latente y la misma planta que en C++.

**Lo que todavía no sabemos:**

1. **"Derecha = más brillante" solo funciona con Wild bajo.** La correlación es positiva con Wild 0 en 9 de 10 semillas, pero se pierde con Wild 0,5 y 1 (solo el bajo y la voz la mantienen). Hipótesis: la dirección "brillante" es lineal y solo vale cerca de la semilla. Con pasos grandes mandan el ruido aleatorio y lo que hace el decoder lejos de los sonidos plausibles.
2. **Saltos bruscos entre los niveles 2 y 3 con Wild alto** en mallet, guitarra y órgano. Con Wild 1 es lo que se busca, pero hay que vigilar que no suene roto.
3. **Si la "demencia" de Wild 1 conserva algo de la semilla** (decisión D2). Monchu lo ha aprobado escuchando; conviene seguir atentos en el plugin.
4. **Sonidos que no son notas de instrumento** (voces, objetos) y **calidad completa**: NSynth no tiene nada por encima de 8 kHz.
5. **Nada sobre el modelo final.** Este VAE tiene 156 M de parámetros y es lento en CPU; sirve para diseñar la experiencia, no para el producto.

## Pendiente para próximas rondas

- **Medir el salto sin la zona vacía.** La distancia actual compara también lo que hay por encima de 8 kHz, donde NSynth no tiene nada; cualquier ruido del decoder ahí infla la cifra.
- **Distancias de referencia** (misma nota en otro instrumento, otra familia...) para que las cifras tengan escala.
- **Ajustes para que `lean` funcione con Wild alto:** más `lean_k`, menos `sigma` y algo de `leash`; o elegir entre varios candidatos por brote (propuesta P3).
