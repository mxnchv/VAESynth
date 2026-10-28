# Motor experimental con un modelo preentrenado

Rama de Fer para probar, dentro del plugin real, una planta cuyos brotes salen de un
espacio latente aprendido (el VAE de Stable Audio Open), sin tocar el resto del plugin.

> **Para el Claude que trabaje aquí.** El plan completo es el documento de Fer
> "VAESynth — Motor experimental enchufable con un modelo preentrenado" (pídeselo si
> no lo tienes). Este archivo recoge los ajustes acordados con el responsable del producto
> y con la sesión que mantiene la beta. Si algo de aquí contradice al plan, manda esto.

## Punto de partida

- Esta rama sale de `claude/plant-beta` en el commit `4cfe6cb`. La beta ya incluye, respecto a
  `0bc4eec` (en el que se basaba el plan):
  - **Wild con mínimo**: `Plant::grow` usa `minimumWild + (1 − minimumWild) · wild`.
  - **Estirar al soltar Wild** (`PlantSession::applyWild`): misma `rngSeed`, misma forma; se recalcula
    toda la planta.
  - **Luces** al tocar (solo interfaz).
  - **El audio de la semilla se guarda dentro de la sesión** (FLAC en base64).
  - **Brotes estéreo**: `renderSample` devuelve 2 canales si la semilla los tiene.
  - **`render()` puede devolver `nullptr`**: el brote se queda apagado y en silencio, sin colgar nada.
- Para traer cambios nuevos de la beta: `git merge claude/plant-beta` en esta rama.

## Reglas para no pisarse

| Quién | Dónde |
|---|---|
| Esta rama (Fer) | `Source/experimental/`, `experimental/` y las dos ediciones protegidas del plan (`CMakeLists.txt` y la línea del motor en `Source/PlantSession.h`), siempre detrás de `VAESYNTH_EXPERIMENTAL_PRETRAINED`, apagada por defecto |
| `claude/plant-beta` (la beta) | Todo lo demás. No toca estas carpetas |

Si hace falta cambiar algo fuera de esa lista, se pregunta antes.

## Ajustes al plan

1. **Si el servidor falla o tarda demasiado, `render()` devuelve `nullptr`**, no la semilla. Devolver la
   semilla engañaría al oído; un brote apagado dice la verdad.
2. **Estéreo**: si la semilla es estéreo, el adaptador debe devolver audio estéreo (el VAE de Stable
   Audio Open lo es). Con semillas mono, mono.
3. **Usar `127.0.0.1`, no `localhost`.** El plugin vive dentro del DAW y macOS puede bloquear HTTP según
   la configuración de la aplicación anfitriona; a una IP local no suele aplicarlo (sin verificar en
   Live). En Windows, escuchar solo en `127.0.0.1` evita el aviso del firewall.
4. **Versionar la configuración** (`B`, `s`, `d`): las sesiones guardan las coordenadas de cada brote.
   Nunca sobrescribir un archivo de configuración; crear uno nuevo con otra versión.
5. **Esperas**: al soltar Wild se recalcula la planta entera (~65 brotes de media, hasta ~120). Con un
   decoder lento la planta tarda en encenderse. Anotarlo en las pruebas, no confundirlo con un fallo.
6. **Sin RAVE de momento** (licencia no comercial). Solo Stable Audio Open.
7. **Lo que no se sube nunca al repo**: pesos del modelo, tokens de Hugging Face, audios de la librería.

## Orden de trabajo

1. **Cuaderno de escucha** (`experimental/pretrained_server/`), sin tocar el plugin: codificar unos
   cientos de sonidos, calcular `B`, `s` y `d`, renderizar 10–20 plantas como WAV y escucharlas junto
   con las mismas semillas pasadas por la beta DSP. **Si no convence, se para aquí.**
2. Servidor mínimo (`info`, `render`) con caché de latentes.
3. Adaptador C++ en `Source/experimental/` y las dos ediciones protegidas.
4. Pruebas en el DAW y notas (ver §8 del plan).

## Lo que Fer tiene que hacer con su cuenta (no lo puede hacer Claude)

1. Registrarse en Stability AI (licencia Community, gratuita por debajo de 1 M$ de ingresos).
2. Crear una cuenta en Hugging Face, aceptar las condiciones de *stable-audio-open-1.0* y crear un
   token de lectura. El token se guarda en su ordenador, nunca en el repo.
