<p align="center"><img src="assets/logo.jpg" alt="AudioFlow Matrix" width="480"></p>

# AudioFlow Matrix — Guía de prueba

AudioFlow Matrix sustituye a ASIO Link Pro / Voicemeeter: une tu tarjeta ASIO, Traktor, un
micro USB y la salida para Discord/OBS en una sola matriz de ruteo de baja latencia, con
supresor de ruido para el micro, push-to-talk y control por MIDI. Esta guía explica cómo
instalarlo, qué probar y qué contarnos.

La interfaz sale en el idioma de Windows. Esta guía cita los botones con su nombre en inglés: si
prefieres seguirla al pie de la letra, cambia el idioma en la pestaña **Settings** (en español,
**Ajustes**) > **Language**.

## Cómo fluye el audio

```
ENTRADAS                                          SALIDAS
Tarjeta ASIO (entradas) ──┐                   ┌──> Tarjeta ASIO (salidas)
Traktor (salidas) ────────┼──> Matriz de ruteo ┼──> Traktor (entradas, timecode DVS)
Micro USB (WASAPI) ───────┘                   └──> Discord / OBS (cable virtual)
  └─ Denoise + Gate                                  └─ Push-to-talk
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
| Controlador MIDI (opcional) | Cualquiera que envíe notas o CC (pads, teclado, Launchpad...) |
| CPU | Cualquier x64; si tiene AVX2 la matriz y el Denoise lo usan automáticamente |

## Instalación

1. Descarga `AudioFlowMatrix-Portable-<versión>.exe` y guárdalo en una carpeta fija (es un solo
   archivo, sin instalar; lleva dentro el driver ASIO, que al registrarlo se
   instala en `C:\Program Files\AudioFlow Matrix\driver`).
2. Ábrelo. Si aparece "Windows protegió su PC", pulsa **Más información** y luego **Ejecutar de
   todas formas** (el programa no está firmado).
3. Ve a la pestaña **Devices**. En **Driver**, elige el de tu tarjeta. En **Sample rate**, deja
   **Driver setting** para usar la frecuencia que ya tenga la tarjeta (o elige una concreta).
4. Pulsa **Register driver (administrator)** y acepta el aviso de Windows. Debe aparecer en verde
   "AudioFlow Matrix ASIO is registered". Solo se hace una vez. Si dice que el driver registrado es de
   otra versión, pulsa **Update driver**.
5. Pulsa **Start**. La barra superior debe mostrar el driver, la frecuencia y el tamaño de búfer.

El tamaño de búfer se cambia en el panel de la propia tarjeta (botón **Open driver control panel**).
Si el driver no lo informa, se usan 256 muestras y sale un aviso. Empieza con 128 muestras y
baja a 64 si va estable.

## Configurar Traktor

Abre siempre AudioFlow Matrix y pulsa Start antes de abrir Traktor.

1. **Preferences > Audio Setup > Audio Device**: elige **AudioFlow Matrix ASIO**.
2. **Sample Rate**: la misma que la tarjeta (la que muestra la barra superior de AudioFlow Matrix).
   El tamaño de búfer lo fija la tarjeta.
3. **Output Routing**: Traktor ve 8 salidas ("AFM Out 1" a "AFM Out 8"), p. ej. Master en 1-2 y Monitor en 3-4.
4. **Input Routing** (DVS): Traktor ve 8 entradas ("AFM In 1" a "AFM In 8") para el timecode.

La primera vez, AudioFlow Matrix trae un ruteo básico: salidas 1-8 de Traktor → salidas 1-8 de la
tarjeta, y entradas 1-8 de la tarjeta → Traktor. El puente añade un bloque de latencia
(64 muestras = 1,33 ms a 48 kHz).

## Usar la matriz y el mixer

**Pestaña Matrix** (filas = entradas, columnas = salidas)

- **Clic** en una celda: rutea o desrutea. Las rutas activas se ven con el degradado de la marca,
  rojo = en mute, gris = sin ruta.
- **Clic derecho**: volumen, mute y paneo (solo en salidas estéreo).
- **Volúmenes de 0 a 100**: 100 es el volumen original (0 dB) y 0 es silencio. Cada fila tiene el volumen de su
  entrada y la última fila el de cada salida. Se arrastran, o doble clic para escribir el número.
- **Clic derecho en un nombre** (o en el volumen de una salida) para renombrarlo. Solo cambia el nombre que ves:
  las rutas y los mapeos MIDI siguen funcionando, y se puede volver al nombre original.
- Los grupos de dispositivos se alternan como un tablero de ajedrez y la fila y la columna bajo el ratón se
  iluminan, para no perderse en matrices grandes.

**Pestaña Mixer**

- Un fader por entrada arriba y uno por salida abajo, con medidor de pico de -60 a +6 dBFS.
- **Doble clic** en un fader lo devuelve a 0 dB. **MUTE** silencia el canal.

Los cambios suenan al instante y sin clics. Todo se guarda en `%APPDATA%\AudioFlowMatrix\settings.ini`.

## Micrófono USB y salida para Discord/OBS

Ambos se activan en Devices y se aplican con **Apply and restart audio**.

**Micrófono USB**

1. Marca **Enable** y elige el micro en **Capture device**.
2. Deja **Exclusive mode** desmarcado (modo compartido, el de por defecto) para que juegos, Discord o el
   navegador también puedan usar el micro. Márcalo solo si ninguna otra app lo necesita: baja un poco la
   latencia, pero Windows le da el micro solo a AudioFlow Matrix.
3. Es normal ver una deriva entre -300 y +300 ppm: se compensa sola. Debe decir "locked".

**Denoise y Gate** (debajo del micro; se aplican al momento)

- **Denoise (RNNoise)** quita ruido de fondo (ventilador, teclado, aire acondicionado).
  Necesita el micro a 48 kHz: con Denoise activado el micro lo pide aunque la tarjeta vaya a
  otra frecuencia. Si no puede, sale un aviso amarillo.
- **Gate** silencia el micro cuando no hablas. **Threshold**: -60 dB muy sensible, -20 dB hay
  que hablar fuerte. Empieza en -45 dB.
- Debajo se ve si la puerta está abierta, **Voice %** (lo seguro que está de que es voz) y la
  latencia añadida (30 ms).

**Loopback del sonido de Windows**

Mete en la matriz lo que suena en un dispositivo de reproducción de Windows (juego, Discord, navegador).

1. En **Windows sound loopback**, marca **Enable** y elige el dispositivo en **Playback device**
   (normalmente el que tienes como predeterminado en Windows).
2. Pulsa **Apply and restart audio**: aparecen las entradas **Loopback L/R** en la matriz y el mixer.
3. Rutéalas a donde quieras, por ejemplo a la salida Stream para que Discord/OBS oigan el juego.

Las apps siguen sonando en ese dispositivo como siempre. No elijas el mismo dispositivo que la salida
Stream si vas a rutear Loopback a Stream: el sonido volvería a entrar en sí mismo (sale un aviso).

**Salida para Discord/OBS**

1. Instala [VB-CABLE](https://vb-audio.com/Cable/) y reinicia.
2. Salida de stream: **Enable** y **Render device** = "CABLE Input". Exclusive desmarcado.
3. En Discord u OBS, elige **CABLE Output** como micrófono.
4. Por defecto recibe el micro y el Master de Traktor; ajústalo en las columnas "Stream L/R".

Discord transmite la voz en mono: mezcla L y R. Para cambiar el volumen que oye Discord mueve
los dos faders de Stream, no solo uno.

## Push-to-talk

En **Devices > Push-to-talk** (opcional, viene desactivado):

- **Enable push-to-talk**: el micro solo llega a Stream mientras el PTT está abierto. El micro
  hacia la tarjeta (tu monitorización) y Traktor no se tocan.
- **Hold to talk**: hablas mientras mantienes pulsado. **Toggle**: un toque abre, otro cierra.
- En la barra superior aparece un botón gris "PTT (mic muted on stream)" que se pone en rojo
  **ON AIR**. Abrir y cerrar tiene un fundido de unos 10 ms, sin clics.

## MIDI

Pestaña **MIDI**:

1. **MIDI input**: elige tu controlador. Si lo conectas con la app abierta, pulsa **Refresh**.
2. **Mappable controls** lista todo lo que se puede mapear: Push-to-talk y el MUTE de cada canal
   (los canales aparecen con el audio en marcha). Pulsa **Learn** en una fila y aprieta el pad,
   tecla o botón. **Clear** lo quita. **Filter** busca por nombre.
3. Los MUTE se conmutan con cada pulsación. Un pad controla una sola cosa: si lo asignas a otra,
   se quita de la anterior. Funciona también con la ventana minimizada.
4. **Last MIDI messages** muestra lo que llega del controlador y qué hizo cada mensaje.

En Windows 10 cada entrada MIDI solo la puede abrir un programa a la vez. Si Traktor está usando
el controlador sale un aviso naranja: libéralo en Traktor (*Controller Manager*) o usa otro
puerto del mismo controlador.

## Aviso de nuevas versiones

Una vez al día, al abrir la app, AudioFlow Matrix pregunta a GitHub cuál es la última versión publicada.
Si hay una más nueva aparece un aviso azul bajo la barra superior:

- **Download** abre la página de esa versión en el navegador para descargarla (ahí está también su
  SHA-256 para comprobar el archivo). La app no descarga ni instala nada por su cuenta.
- **Ignore this version** deja de avisar de esa versión; **Later** lo oculta hasta el próximo arranque.
- Se desactiva con **Check for updates**, abajo a la derecha (junto a la versión y los enlaces a GitHub y Ko-fi) o en la pestaña **Settings**. Ahí también está **Check now** para comprobarlo al momento. Sin Internet no pasa nada: lo reintenta
  en el siguiente arranque.

## Qué probar

- [ ] La app abre y Devices lista la tarjeta ASIO
- [ ] Register driver termina en verde
- [ ] Start arranca sin texto rojo y el DSP % se queda por debajo del 30 %
- [ ] Con **Driver setting**, la frecuencia de la barra superior es la de la tarjeta
- [ ] Traktor abre con "AudioFlow Matrix ASIO" y la app dice "ASIO client connected"
- [ ] Un deck de Traktor suena por la tarjeta sin clics durante 30 minutos, y en Devices los "bloques tarde"
      se quedan en 0 (o dejan de subir tras crecer el búfer de seguridad)
- [ ] DVS: el timecode llega a Traktor y el control responde sin retraso apreciable
- [ ] Con búfer de 64 muestras sigue sin cortes
- [ ] El micro USB aparece en la matriz, marca "locked" y dropouts = 0
- [ ] Discord u OBS reciben el audio por CABLE Output limpio
- [ ] Loopback: el sonido de un juego o del navegador aparece en Loopback L/R, dropouts = 0, también tras pausarlo
- [ ] Denoise quita el ruido de fondo y la voz suena natural; activarlo/desactivarlo no hace clic
- [ ] Gate se cierra en silencio, no se come el principio de las palabras ni corta los finales
- [ ] PTT: en Discord/OBS no se te oye hasta pulsar; mantener/soltar sin clics; Toggle funciona
- [ ] MIDI: el controlador aparece, "Last MIDI messages" muestra mensajes, Learn asigna el pad
- [ ] Un pad asignado al PTT y otro al MUTE de un canal funcionan, también con la ventana minimizada
- [ ] Cambiar volumen, mute o paneo no produce clics
- [ ] En el Mezclador, doble clic en el número de dB permite escribir la ganancia; doble clic en el fader la
      vuelve a 0 dB; la tira del micro sube hasta +24 dB
- [ ] Renombrar una entrada y una salida: el nombre nuevo aparece en Matrix, Mixer y MIDI y se mantiene al reabrir
- [ ] Cerrar y reabrir la app conserva el ruteo, el dispositivo MIDI y las asignaciones
- [ ] Cerrar y reabrir Traktor con la app en marcha reconecta solo
- [ ] Con una versión antigua instalada, al abrir aparece el aviso de versión nueva y **Download** abre su página
- [ ] En **Settings > Language** el idioma cambia al momento y se mantiene al cerrar y reabrir la app
- [ ] Con **Settings > Minimize to the system tray**, al minimizar desaparece de la barra de tareas y queda el icono junto al reloj; el audio sigue sonando, clic lo abre y clic derecho > Salir lo cierra
- [ ] En **Settings > Interface smoothness**, 15/30/60 fps y "igual que la pantalla" cambian la suavidad de los medidores; con otra ventana delante la app gasta menos (baja a 15 fps)

## Problemas comunes

| Síntoma | Causa probable | Qué hacer |
| --- | --- | --- |
| "Windows protegió su PC" o el antivirus lo bloquea | El programa no está firmado | Más información > Ejecutar de todas formas |
| La lista de drivers ASIO sale vacía | Falta el driver del fabricante | Instalarlo y pulsar Refresh device lists |
| Traktor: "AudioFlow Matrix is not running" | Traktor se abrió antes que la app | Abrir la app, pulsar Start, reabrir Traktor |
| Traktor: "audio engine is stopped" | La app está abierta sin Start | Pulsar Start |
| Traktor no deja elegir otra frecuencia | La frecuencia la impone la tarjeta | Usar en Traktor la de la barra superior |
| Clics o cortes en todo | Búfer demasiado pequeño | Subir el búfer a 128 o 256 |
| Clics solo en el micro | No está "locked" o hay dropouts | Cerrar apps que usen el micro y Apply and restart audio |
| Un juego o Discord no detecta el micro | El micro está en Exclusive mode | Desmarcar Exclusive mode y Apply and restart audio |
| El micro sale "shared" | Otra app lo está usando | Cerrar Discord/navegador y Apply and restart audio |
| Denoise desactivado con aviso amarillo | El micro no va a 48 kHz | En Windows, Sonido > micro > Propiedades avanzadas: 48000 Hz (o marcar Exclusive mode, que bloquea el micro a otras apps) |
| Discord no oye nada | Falta CABLE Output, rutas a Stream o PTT cerrado | Revisar el micro en Discord, Stream L/R en la matriz y el PTT |
| Bajar un solo fader de Stream no cambia nada en Discord | Discord mezcla L y R en mono | Mover los dos faders de Stream |
| MIDI: aviso naranja "in use by another program" | Otro programa (p. ej. Traktor) tiene el puerto | Liberarlo en ese programa u otro puerto, y Refresh |
| El controlador MIDI no aparece | Se conectó con la app abierta | Pulsar Refresh en la pestaña MIDI |
| Register driver falla | Se canceló el aviso de administrador | Repetir y aceptar |

## Qué reportar

Abre un [issue](https://github.com/SubielaDev/AudioFlowMatrix/issues) con:

- Modelo de tarjeta, versión de su driver y de Windows.
- Frecuencia y tamaño de búfer.
- El texto rojo, amarillo o naranja de la app, tal cual.
- El DSP % y los dropouts del micro/stream.
- Qué ruta falla, cuándo y qué se oyó (clic, corte, silencio).
- Si falla el MIDI o el PTT: los archivos `AudioFlowMatrix.log` y `AudioFlowMatrix.previous.log`
  de `%APPDATA%\AudioFlowMatrix\logs` (botón **Open log folder** en la pestaña MIDI).
- Si el programa se cierra de golpe: los dos archivos `crash-...` (`.txt` y `.dmp`) de
  `%APPDATA%\AudioFlowMatrix\crashes`, y qué estabas haciendo justo antes.

## Desinstalar

1. En Devices, pulsa **Unregister**.
2. Borra el `.exe` y la carpeta `C:\Program Files\AudioFlow Matrix\driver` (pide permiso de administrador).
3. Opcional: borra `%APPDATA%\AudioFlowMatrix` (ajustes, log e informes).
