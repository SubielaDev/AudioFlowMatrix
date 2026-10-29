<h1 align="center"><img src="docs/assets/logo.jpg" alt="AudioFlow Matrix" width="640"></h1>

**Español** · [English](README.en.md)

Router de audio de baja latencia para Windows. Una interfaz ASIO maestra marca el reloj;
Traktor y los DAWs (a través de un driver ASIO virtual), un micrófono USB y una salida
para Discord/OBS se sincronizan con ella mediante colas lock-free y resamplers que
compensan la deriva de reloj.

```
Focusrite (ASIO, reloj maestro) ──┐                  ┌──> Salidas de la Focusrite
Traktor / DAW (ASIO virtual) ─────┼──> matriz NxM ───┼──> Entradas de Traktor (timecode DVS)
Micro USB (WASAPI) ───────────────┘     (AVX2)       └──> Discord / OBS (WASAPI)
  └─ Denoise (RNNoise) + Gate                              └─ Push-to-talk (MIDI)
```

## Funciones

- **Matriz de ruteo** NxM con ganancia, mute y paneo por cruce, y faders con medidores por canal.
- **Driver ASIO virtual** para Traktor y DAWs, sincronizado con la tarjeta maestra.
- **Micrófono USB** por WASAPI (exclusivo o compartido) con compensación de deriva de reloj.
- **Denoise** con RNNoise y **puerta de ruido** (umbral de −60 a −20 dB, hold y release suaves).
- **Push-to-talk** opcional del micro hacia la salida Stream, en modo mantener o conmutar.
- **Control MIDI**: pestaña MIDI con MIDI Learn para el PTT y el MUTE de cada canal.
- **Diagnóstico**: log de MIDI/PTT/audio e informes de crash con la función y línea exactas.

## Uso

1. Descarga `AudioFlowMatrix.zip` desde [Releases](https://github.com/SubielaDev/AudioFlowMatrix/releases)
   y descomprime todos los archivos en la misma carpeta. La release v0.2.0 aún no incluye
   Denoise, Gate, Push-to-talk ni MIDI: para tenerlos, compila desde el código (abajo).
2. Abre `AudioFlowMatrix.exe` y ve a la pestaña **Devices**:
   - elige el driver ASIO maestro (p. ej. *Focusrite USB ASIO*). En **Sample rate**,
     *Driver setting* mantiene la frecuencia que ya tenga la tarjeta; el tamaño de búfer es
     el del panel de la tarjeta (256 muestras si el driver no lo informa);
   - pulsa una vez **Register driver (administrator)**, que instala "AudioFlow Matrix ASIO";
   - opcionalmente activa el micro USB y la salida de stream (para Discord/OBS usa un cable
     virtual como VB-CABLE: envía el audio a *CABLE Input* y elige *CABLE Output* como micro allí).
3. Pulsa **Start**. En Traktor: *Preferences > Audio Setup > AudioFlow Matrix ASIO*, a la misma
   frecuencia que la tarjeta.
4. Pestaña **Matrix**: clic en una celda para rutear, clic derecho para ganancia / mute / paneo.
   Pestaña **Mixer**: faders de entrada y de bus de salida con medidores de pico.

### Micrófono: Denoise y Gate

En **Devices**, debajo del micro: **Denoise (RNNoise)**, **Gate** y **Threshold**. Se aplican al
momento. El Denoise necesita el micro a 48 kHz (con Denoise activado el micro lo pide aunque
la tarjeta vaya a otra frecuencia) y añade 30 ms de latencia al micro.

### Push-to-talk

En **Devices > Push-to-talk**: con PTT activado, el micro solo llega a la salida Stream mientras
está abierto; el micro hacia la tarjeta no se toca. Modo **Hold to talk** o **Toggle**. Aparece un
botón en la barra superior que se pone en rojo **ON AIR**. La apertura y el cierre tienen un
fundido de ~10 ms.

### MIDI

Pestaña **MIDI**:

- **MIDI input**: el controlador, con **Refresh** para detectar uno conectado con la app abierta.
  En Windows 10 cada entrada MIDI solo la puede abrir un programa a la vez: si Traktor la usa,
  libérala en su *Controller Manager* o usa otro puerto del controlador.
- **Mappable controls**: Push-to-talk y el MUTE de cada canal (entradas y salidas). **Learn** y
  luego el pad, tecla o botón; **Clear** para quitarlo. Acepta notas y CC (≥ 64 = pulsado). Un
  pad controla una sola cosa. Las asignaciones van por nombre de canal y funcionan con la
  ventana minimizada.
- **Last MIDI messages** muestra lo que llega y qué hizo cada mensaje.

### Archivos

| Qué | Dónde |
|---|---|
| Ajustes | `%APPDATA%\AudioFlowMatrix\settings.ini` |
| Log de diagnóstico (sesión actual y anterior) | `%APPDATA%\AudioFlowMatrix\logs` |
| Informes de crash (`.txt` + `.dmp`) | `%APPDATA%\AudioFlowMatrix\crashes` |

Los `.pdb` que acompañan al ejecutable permiten que los informes de crash muestren nombres de
función y líneas; déjalos en la misma carpeta. `AudioFlowMatrix.exe --list` muestra los
dispositivos ASIO y WASAPI detectados.

Para probarlo paso a paso, consulta la [guía de prueba](docs/guia-de-prueba.md).

## Latencia

| Ruta | Latencia que añade AudioFlow Matrix |
|---|---|
| Entrada ASIO → matriz → salida ASIO | 0 (mismo callback) |
| Traktor ↔ matriz | 1 bloque (p. ej. 64 muestras = 1,33 ms a 48 kHz) |
| Micro USB → matriz | Periodo WASAPI + ~1,5 bloques de margen en la cola |
| Micro USB con Denoise disponible (a 48 kHz) | + 30 ms (1440 muestras: 480 de reagrupado + 960 de RNNoise) |

En el hilo de audio el motor no reserva memoria, no usa locks y es `noexcept`. El Denoise y la
puerta corren en el hilo de captura del micro, no en el de ASIO.

## Estructura

```
src/core      colas SPSC lock-free (en heap y en memoria compartida), triple buffer, memoria alineada
src/dsp       mezclador matricial AVX2/FMA (elegido en tiempo de ejecución), resampler cúbico + lazo PI,
              VoiceProcessor (RNNoise + puerta de ruido)
src/engine    AudioEngine (bus, process, telemetría, puerta PTT), RoutingModel (dB/mute/paneo), PushToTalk
src/midi      decodificación MIDI, asignaciones y MidiMap (portables) + entrada WinMM
src/platform  utilidades Win32: puntero COM, MMCSS, UTF-8
src/asio      ABI de ASIO, host del driver maestro, DLL del driver virtual + puente de memoria compartida
src/wasapi    endpoints WASAPI de captura y reproducción (exclusivo con fallback a compartido)
src/app       interfaz Dear ImGui (Win32 + OpenGL 3), PTT/MIDI, log e informes de crash
tests         núcleo y motor, voz (RNNoise/puerta), PTT/MIDI
tools         brand_assets.py: genera el icono y el logo embebido desde docs/assets
third_party   Dear ImGui 1.91.8 (MIT), RNNoise 0.2 (BSD-3-Clause)
```

La ABI de ASIO está declarada de forma independiente en `src/asio/include/afm/asio/AsioAbi.h`,
así que no hace falta el SDK de Steinberg para compilar. "ASIO" es una marca registrada de
Steinberg Media Technologies.

## Compilar

Visual Studio 2022 + CMake 3.25 o superior:

```
cmake --preset msvc-x64
cmake --build --preset release
ctest --preset release
```

También funciona cualquier toolchain MinGW con Clang/GCC (incluido `zig c++`) con el generador
Ninja. Con zig, el proyecto fija `-mcpu=x86_64` (variable `AFM_TARGET_CPU_FLAGS`): sin eso zig
compila para la CPU del equipo que compila y el programa falla con "instrucción ilegal" en otras
CPUs. Las rutinas AVX2 se eligen en tiempo de ejecución.

## Licencia

MIT, ver [LICENSE](LICENSE). Dear ImGui tiene licencia MIT de Omar Cornut. RNNoise tiene
licencia BSD-3-Clause (ver `third_party/rnnoise/COPYING`).
