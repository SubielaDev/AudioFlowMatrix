<p align="center"><img src="assets/logo.jpg" alt="AudioFlow Matrix" width="480"></p>

# AudioFlow Matrix — Guía de prueba

AudioFlow Matrix sustituye a ASIO Link Pro / Voicemeeter: une tu tarjeta ASIO, Traktor, un
micro USB y la salida para Discord/OBS en una sola matriz de ruteo de baja latencia. Esta guía
explica cómo instalarlo, qué probar y qué contarnos.

## Cómo fluye el audio

```
ENTRADAS                                          SALIDAS
Tarjeta ASIO (entradas) ──┐                   ┌──> Tarjeta ASIO (salidas)
Traktor (salidas) ────────┼──> Matriz de ruteo ┼──> Traktor (entradas, timecode DVS)
Micro USB (WASAPI) ───────┘                   └──> Discord / OBS (cable virtual)
```

La tarjeta ASIO marca el reloj: Traktor y el micro USB se sincronizan con ella, y cualquier
entrada puede ir a cualquier salida.

## Requisitos

| Qué | Detalle |
| --- | --- |
| Sistema | Windows 10 u 11, 64 bits |
| Tarjeta de audio | Cualquiera con driver ASIO (diseñado con Focusrite Scarlett 18i8) |
| Driver ASIO | El oficial del fabricante, p. ej. "Focusrite USB ASIO" |
| Permisos | Administrador una sola vez, para registrar el driver virtual |
| Traktor | Traktor Pro 3 o 4 de 64 bits (o cualquier DAW con ASIO) |
| Discord/OBS (opcional) | Un cable de audio virtual, p. ej. [VB-CABLE](https://vb-audio.com/Cable/) |
| CPU | Cualquier x64; si tiene AVX2 la matriz usa SIMD automáticamente |

## Instalación

1. Descarga `AudioFlowMatrix.zip` desde [Releases](https://github.com/SubielaDev/AudioFlowMatrix/releases)
   y descomprímelo en una carpeta fija, p. ej. `C:\AudioFlowMatrix`. Los dos archivos deben
   quedar juntos y no moverse después: Windows recuerda la ruta de la DLL.
2. Abre `AudioFlowMatrix.exe`. Si aparece "Windows protegió su PC", pulsa **Más información**
   y luego **Ejecutar de todas formas** (el programa no está firmado).
3. Ve a la pestaña **Devices**.
4. En **Driver**, elige el de tu tarjeta. En **Sample rate**, elige **48000 Hz**.
5. Pulsa **Register driver (administrator)** y acepta el aviso de Windows. Debe aparecer en verde
   "AudioFlow Matrix ASIO is registered". Solo se hace una vez.
6. Pulsa **Start**. La barra superior debe mostrar el driver, la frecuencia y el tamaño de búfer.

El tamaño de búfer se cambia en el panel de la propia tarjeta (botón **Open driver control panel**).
Empieza con 128 muestras y baja a 64 si va estable.

## Configurar Traktor

Abre siempre AudioFlow Matrix y pulsa Start antes de abrir Traktor.

1. **Preferences > Audio Setup > Audio Device**: elige **AudioFlow Matrix ASIO**.
2. **Sample Rate**: 48000 Hz. El tamaño de búfer lo fija la tarjeta.
3. **Output Routing**: Traktor ve 8 salidas ("AFM Out 1" a "AFM Out 8"), p. ej. Master en 1-2 y Monitor en 3-4.
4. **Input Routing** (DVS): Traktor ve 8 entradas ("AFM In 1" a "AFM In 8") para el timecode.

La primera vez, AudioFlow Matrix trae un ruteo básico: salidas 1-8 de Traktor → salidas 1-8 de la
tarjeta, y entradas 1-8 de la tarjeta → Traktor. El puente añade un bloque de latencia
(64 muestras = 1,33 ms a 48 kHz).

## Usar la matriz y el mixer

**Pestaña Matrix** (filas = entradas, columnas = salidas)

- **Clic** en una celda: rutea o desrutea. Verde = ruteado, rojo = en mute, gris = sin ruta.
- **Clic derecho**: ganancia (-60 a +12 dB), mute y paneo (solo en salidas estéreo).

**Pestaña Mixer**

- Un fader por entrada arriba y uno por salida abajo, con medidor de pico de -60 a +6 dBFS.
- **Doble clic** en un fader lo devuelve a 0 dB. **MUTE** silencia el canal.

Los cambios suenan al instante y sin clics. Todo se guarda en `%APPDATA%\AudioFlowMatrix\settings.ini`.

## Micrófono USB y salida para Discord/OBS

Ambos se activan en Devices y se aplican con **Apply and restart audio**.

**Micrófono USB**

1. Marca **Enable** y elige el micro en **Capture device**.
2. Deja **Exclusive mode** marcado. Si otra app usa el micro, pasa solo a modo compartido.
3. Es normal ver una deriva entre -300 y +300 ppm: se compensa sola. Debe decir "locked".

**Salida para Discord/OBS**

1. Instala [VB-CABLE](https://vb-audio.com/Cable/) y reinicia.
2. Salida de stream: **Enable** y **Render device** = "CABLE Input". Exclusive desmarcado.
3. En Discord u OBS, elige **CABLE Output** como micrófono.
4. Por defecto recibe el micro y el Master de Traktor; ajústalo en las columnas "Stream L/R".

## Qué probar

- [ ] La app abre y Devices lista la tarjeta ASIO
- [ ] Register driver termina en verde
- [ ] Start arranca sin texto rojo y el DSP % se queda por debajo del 30 %
- [ ] Traktor abre con "AudioFlow Matrix ASIO" y la app dice "ASIO client connected"
- [ ] Un deck de Traktor suena por la tarjeta sin clics durante 30 minutos
- [ ] DVS: el timecode llega a Traktor y el control responde sin retraso apreciable
- [ ] Con búfer de 64 muestras sigue sin cortes
- [ ] El micro USB aparece en la matriz, marca "locked" y dropouts = 0
- [ ] Discord u OBS reciben el audio por CABLE Output limpio
- [ ] Cambiar ganancia, mute o paneo no produce clics
- [ ] Cerrar y reabrir la app conserva el ruteo
- [ ] Cerrar y reabrir Traktor con la app en marcha reconecta solo

## Problemas comunes

| Síntoma | Causa probable | Qué hacer |
| --- | --- | --- |
| "Windows protegió su PC" o el antivirus lo bloquea | El programa no está firmado | Más información > Ejecutar de todas formas |
| La lista de drivers ASIO sale vacía | Falta el driver del fabricante | Instalarlo y pulsar Refresh device lists |
| Traktor: "AudioFlow Matrix is not running" | Traktor se abrió antes que la app | Abrir la app, pulsar Start, reabrir Traktor |
| Traktor: "audio engine is stopped" | La app está abierta sin Start | Pulsar Start |
| Traktor no deja elegir 44,1 kHz | La frecuencia la impone la tarjeta | Usar 48000 Hz en Traktor |
| Clics o cortes en todo | Búfer demasiado pequeño | Subir el búfer a 128 o 256 |
| Clics solo en el micro | No está "locked" o hay dropouts | Probar sin Exclusive; cerrar apps que usen el micro |
| El micro sale "shared" | Otra app lo está usando | Cerrar Discord/navegador y Apply and restart audio |
| Discord no oye nada | Falta CABLE Output o rutas a Stream | Revisar el micro en Discord y Stream L/R en la matriz |
| Register driver falla | Se canceló el aviso de administrador | Repetir y aceptar |

## Qué reportar

Abre un [issue](https://github.com/SubielaDev/AudioFlowMatrix/issues) con:

- Modelo de tarjeta, versión de su driver y de Windows.
- Frecuencia y tamaño de búfer.
- El texto rojo o amarillo de la barra inferior, tal cual.
- El DSP % y los dropouts del micro/stream.
- Qué ruta falla, cuándo y qué se oyó (clic, corte, silencio).

## Desinstalar

1. En Devices, pulsa **Unregister**.
2. Borra la carpeta del programa.
3. Opcional: borra `%APPDATA%\AudioFlowMatrix`.
