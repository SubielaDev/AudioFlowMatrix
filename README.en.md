<h1 align="center"><img src="docs/assets/logo.jpg" alt="AudioFlow Matrix" width="640"></h1>

[Español](README.md) · **English**

Low-latency audio router for Windows. One master ASIO interface owns the clock;
Traktor/DAWs (through a virtual ASIO driver), a USB mic and a Discord/OBS output are
slaved to it through lock-free rings and drift-compensating resamplers.

```
Focusrite (ASIO, master clock) ──┐                 ┌──> Focusrite outputs
Traktor / DAW  (virtual ASIO) ───┼──> NxM matrix ──┼──> Traktor inputs (DVS timecode)
USB mic (WASAPI) ────────────────┘    (AVX2)       └──> Discord / OBS (WASAPI)
  └─ Denoise (RNNoise) + gate                            └─ Push-to-talk (MIDI)
```

## Features

- **Routing matrix**, NxM, with gain, mute and pan per crosspoint, plus faders and meters per channel.
- **Virtual ASIO driver** for Traktor and DAWs, locked to the master interface.
- **USB mic** over WASAPI (exclusive or shared) with clock-drift compensation.
- **Denoise** with RNNoise and a **noise gate** (threshold −60 to −20 dB, smooth hold and release).
- Optional **push-to-talk** from the mic to the Stream output, hold or toggle.
- **MIDI control**: a MIDI tab with MIDI Learn for push-to-talk and every channel's MUTE.
- **Diagnostics**: a MIDI/PTT/audio log and crash reports with the exact function and line.

## Using it

1. Download `AudioFlowMatrix-Portable-<version>.exe` from
   [Releases](https://github.com/SubielaDev/AudioFlowMatrix/releases) and keep it in a fixed folder.
   It is a single file, no install: it carries the virtual ASIO driver and unpacks it to
   `%LOCALAPPDATA%\AudioFlowMatrix\driver` when you register it. Windows shows "Windows protected
   your PC" because the program is not signed: *More info > Run anyway*.
2. Run AudioFlow Matrix and open **Devices**:
   - pick the master ASIO driver (e.g. *Focusrite USB ASIO*). **Sample rate** *Driver setting*
     keeps whatever rate the interface is running at; the buffer size is the one in the
     interface's control panel (256 samples if the driver does not report it);
   - press **Register driver (administrator)** once, which installs "AudioFlow Matrix ASIO". If the
     driver is registered from another copy (another folder or version), the app says so and
     offers **Use this copy**;
   - optionally enable the USB mic and the stream output (for Discord/OBS use a virtual
     cable such as VB-CABLE: render to *CABLE Input*, use *CABLE Output* as the mic there).
3. Press **Start**. In Traktor: *Preferences > Audio Setup > AudioFlow Matrix ASIO*, at the
   interface's sample rate.
4. **Matrix** tab: click a cell to route, right-click for gain / mute / pan.
   **Mixer** tab: input and output-bus faders with peak meters.

### Mic: denoise and gate

In **Devices**, under the mic: **Denoise (RNNoise)**, **Gate** and **Threshold**, applied live.
Denoise needs the mic at 48 kHz (with denoise on, the mic asks for it even when the interface
runs at another rate) and adds 30 ms of latency to the mic.

### Push-to-talk

**Devices > Push-to-talk**: while enabled, the mic only reaches the Stream output when PTT is
open; the mic to the interface is untouched. **Hold to talk** or **Toggle**. A toolbar button turns
red **ON AIR**. Opening and closing fade over ~10 ms.

### MIDI

**MIDI** tab:

- **MIDI input**: the controller, with **Refresh** to pick up one plugged in while the app runs.
  On Windows 10 a MIDI input can be open in only one program at a time: if Traktor holds it,
  free it in its *Controller Manager* or use another port of the controller.
- **Mappable controls**: push-to-talk and every input and output channel's MUTE. **Learn**, then the
  pad, key or button; **Clear** removes it. Notes and CCs (≥ 64 = pressed). One pad drives one
  control. Mappings follow channel names and keep working while the window is minimised.
- **Last MIDI messages** shows what arrives and what each message did.

### Files

| What | Where |
|---|---|
| Settings | `%APPDATA%\AudioFlowMatrix\settings.ini` |
| Diagnostics log (current and previous run) | `%APPDATA%\AudioFlowMatrix\logs` |
| Crash reports (`.txt` + `.dmp`) | `%APPDATA%\AudioFlowMatrix\crashes` |
| Virtual ASIO driver (unpacked from the `.exe`) | `%LOCALAPPDATA%\AudioFlowMatrix\driver` |

Crash reports give the module and offset of the fault; with the same build's `.pdb` files next
to the executable they also name the function and line. `AudioFlowMatrix.exe --list` prints the
detected ASIO and WASAPI devices and which driver DLL is in use.

## Latency budget

| Path | Added by AudioFlow Matrix |
|---|---|
| ASIO in → matrix → ASIO out | 0 (same callback) |
| Traktor ↔ matrix | 1 block (e.g. 64 samples = 1.33 ms @ 48 kHz) |
| USB mic → matrix | WASAPI period + ~1.5 blocks of ring headroom |
| USB mic with denoise available (at 48 kHz) | + 30 ms (1440 samples: 480 re-blocking + 960 RNNoise) |

The engine itself is allocation-free, lock-free and `noexcept` on the audio thread. Denoise and
the gate run on the mic's capture thread, not the ASIO one.

## Layout

```
src/core      lock-free SPSC rings (heap and shared-memory), triple buffer, aligned memory
src/dsp       AVX2/FMA matrix mixer (runtime-dispatched), cubic drift resampler + PI loop,
              VoiceProcessor (RNNoise + noise gate)
src/engine    AudioEngine (bus layout, process, telemetry, PTT gate), RoutingModel (dB/mute/pan), PushToTalk
src/midi      MIDI decoding, bindings and MidiMap (portable) + WinMM input
src/platform  Win32 helpers: COM pointer, MMCSS, UTF-8
src/asio      ASIO ABI, host for the master driver, virtual driver DLL + shared-memory bridge
src/wasapi    WASAPI capture/render endpoints (exclusive with shared fallback)
src/app       Dear ImGui front-end (Win32 + OpenGL 3), PTT/MIDI, log and crash reports
tests         core and engine, voice (RNNoise/gate), PTT/MIDI
tools         brand_assets.py: builds the icon and embedded logo from docs/assets
third_party   Dear ImGui 1.91.8 (MIT), RNNoise 0.2 (BSD-3-Clause)
```

The ASIO ABI is declared independently in `src/asio/include/afm/asio/AsioAbi.h`, so the
Steinberg SDK is not needed to build. "ASIO" is a trademark of Steinberg Media Technologies.

## Building

Visual Studio 2022 + CMake 3.25+:

```
cmake --preset msvc-x64
cmake --build --preset release
ctest --preset release
```

Any Clang/GCC MinGW toolchain (including `zig c++`) also works with the Ninja generator. With
zig, the project pins `-mcpu=x86_64` (`AFM_TARGET_CPU_FLAGS`): otherwise zig targets the build
machine's CPU and the binaries die with an illegal instruction on other CPUs. AVX2 kernels are
picked at runtime.

The executable embeds a copy of `AudioFlowMatrixASIO.dll` (resource 101) and uses it when the
DLL is not next to it, so the same `.exe` doubles as the portable version. It is built as a
GUI-subsystem application, so it opens no console window.

## License

MIT, see [LICENSE](LICENSE). Dear ImGui is MIT licensed by Omar Cornut. RNNoise is
BSD-3-Clause licensed (see `third_party/rnnoise/COPYING`).
