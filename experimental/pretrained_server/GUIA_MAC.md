# Guía para el Mac de Monchu (M1): servidor experimental

> **Para el Claude de Monchu.** Monchu no tiene Python, PyTorch ni Hugging Face instalados. Esta guía deja su Mac listo para el paso 2 del experimento y explica qué es cada cosa. Antes de empezar, lee también:
> - `experimental/README.md`: las reglas de la rama, que mandan sobre todo lo demás.
> - `RESULTADOS_RONDA1.md`: lo que se ha probado y por qué se sigue adelante.
> - `README.md` (esta carpeta): cómo funciona el motor experimental.
>
> No toques nada fuera de `experimental/` y `Source/experimental/`. El token de Hugging Face lo escribe Monchu, nunca va en el repositorio ni en el chat.

## 1. Qué es esto, en corto

- **El experimento:** probar dentro del plugin una planta cuyos brotes salen de un **espacio latente aprendido**: el autoencoder (VAE) de Stable Audio Open, un modelo preentrenado de Stability AI. Es un placeholder: sirve para diseñar la experiencia antes de entrenar un modelo propio. Se enchufa y se quita como un USB, sin cambiar el plugin.
- **Ronda 1 (hecha):** Fer generó plantas en Google Colab con notas de NSynth y las escuchasteis. Wild 0 suena parecido a la semilla y Wild 1 suena a "demencia", que es lo buscado. Monchu lo aprobó. Detalles en `RESULTADOS_RONDA1.md`.
- **Paso 2 (ahora):** un pequeño **servidor** en Python que corre en el Mac de Monchu, solo en `127.0.0.1`. El plugin le enviará el audio de la semilla y le pedirá el audio de cada brote. Antes de conectar el plugin, hay que **medir cuánto tarda un brote en este Mac**: al soltar Wild se recalcula toda la planta (unos 65 brotes de media, hasta 120).
- **Paso 3 (después, si los tiempos son razonables):** el adaptador en C++ dentro del plugin, en `Source/experimental/` y detrás de la opción `VAESYNTH_EXPERIMENTAL_PRETRAINED` (apagada por defecto).

## 2. Qué se instala y dónde

| Qué | Para qué | Dónde queda |
|---|---|---|
| `uv` (con Homebrew) | Instala Python y las librerías sin tocar el Python del sistema | Homebrew |
| Python 3.12, PyTorch, diffusers… | Ejecutar el modelo; PyTorch usa la GPU del Mac (MPS) | `experimental/pretrained_server/.venv` (ignorado por git) |
| Token de Hugging Face | Descargar el modelo, que pide aceptar su licencia | `~/.cache/huggingface/token` |
| Pesos del autoencoder (~600 MB) | El modelo en sí | `~/.cache/huggingface/hub` |

Monchu ya tiene Homebrew: lo usa para `cmake`, según el README del plugin.

## 3. Pasos

### 3.1 Preparar una carpeta aparte para el experimento

Así la copia de trabajo de `claude/plant-beta` no se toca. Desde la carpeta del repositorio VAESynth:

```sh
git fetch origin
git worktree add ../VAESynth-experimento experiment/pretrained-engine
cd ../VAESynth-experimento
```

A partir de aquí, todo se hace en `VAESynth-experimento`. Para traer cambios nuevos de la rama: `git pull`.

### 3.2 Cuenta de Hugging Face (lo hace Monchu en el navegador)

1. Crear una cuenta en https://huggingface.co.
2. Entrar en https://huggingface.co/stabilityai/stable-audio-open-1.0 y **aceptar las condiciones** del formulario. Sin esto, la descarga falla con un error 401 o 403.
3. Crear un **token de lectura** en https://huggingface.co/settings/tokens (*Create new token → Read*). Tenerlo a mano para el paso siguiente.

La licencia del modelo es la Stability AI Community License (uso interno; Fer ya se ha registrado con Stability).

### 3.3 Instalar (Terminal, unos minutos)

```sh
bash experimental/pretrained_server/mac/instalar.sh
```

El script:
1. Instala `uv` si falta.
2. Crea el entorno `.venv` con Python 3.12.
3. Instala PyTorch y diffusers.
4. Comprueba que la GPU del Mac (MPS) está disponible.
5. **Pide el token de Hugging Face.** Lo pega Monchu; no se ve al escribirlo.
6. Descarga los pesos (~600 MB).
7. Comprueba que existe la configuración de la ronda 1.

Si pide el token, que lo escriba **Monchu en su Terminal**: su Claude no debe verlo ni guardarlo.

### 3.4 Configuración de la ronda 1

El servidor necesita `configs/config_v001.json`, el archivo con las direcciones del latente calculadas en la ronda 1. Fer lo sube a la rama. Si al instalar sale el aviso de que falta, haz `git pull` cuando Fer confirme que ya está.

### 3.5 Medir la velocidad (lo importante de este paso)

```sh
bash experimental/pretrained_server/mac/medir_velocidad.sh
```

Codifica un sonido de prueba de 4 s y renderiza 12 brotes, primero con la GPU del Mac y luego con la CPU. Imprime:

- `ms_per_bud_median`: milisegundos por brote.
- `plant_of_65_buds_s` y `plant_of_120_buds_s`: segundos para encender una planta media y una grande.

**Mandad estos resultados a Fer.** Como referencia, en la CPU de un servidor en la nube salió unos 5,3 s por brote (casi 6 minutos por planta). La GPU del M1 debería ir bastante más rápida, pero hay que medirlo. Con esas cifras se decide cómo hacer el paso 3: cuántos brotes calcular a la vez, si acortar los brotes, etc.

### 3.6 Arrancar el servidor (para el paso 3)

```sh
bash experimental/pretrained_server/mac/arrancar_servidor.sh
```

Se queda escuchando en `http://127.0.0.1:8765` hasta pulsar Ctrl+C. Por ahora no hace falta: el plugin todavía no sabe hablar con él.

## 4. Si algo falla

| Síntoma | Causa probable |
|---|---|
| `Falta Homebrew` | Instalar Homebrew desde https://brew.sh |
| Error 401/403 al descargar el modelo | Condiciones no aceptadas en Hugging Face, o el token es de otra cuenta |
| `MPS: NO` o la medición con MPS falla | Se puede usar la CPU (`--device cpu`), mucho más lenta. Copiar el error para Fer |
| `Falta configs/config_v001.json` | Esperar a que Fer lo suba y hacer `git pull` |
| `Address already in use` al arrancar | Ya hay un servidor abierto: cerrarlo, o usar `--port 8766` |

## 5. Cómo quitarlo todo

```sh
git worktree remove --force ../VAESynth-experimento                  # la carpeta del experimento, con su .venv
rm -rf ~/.cache/huggingface/hub/models--stabilityai--stable-audio-open-1.0   # los pesos
rm -f ~/.cache/huggingface/token                                     # el token
brew uninstall uv                                                    # opcional
```

El plugin y la rama `claude/plant-beta` no se ven afectados en ningún caso.
