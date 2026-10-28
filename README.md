# AudioFlow Matrix

**English** · [Español](README.es.md)

Low-latency audio router for Windows. One master ASIO interface owns the clock;
Traktor/DAWs (through a virtual ASIO driver), a USB mic and a Discord/OBS output are
slaved to it through lock-free rings and drift-compensating resamplers.

```
Focusrite (ASIO, master clock) ──┐                 ┌──> Focusrite outputs
Traktor / DAW  (virtual ASIO) ───┼──> NxM matrix ──┼──> Traktor inputs (DVS timecode)
USB mic (WASAPI exclusive) ──────┘    (AVX2)       └──> Discord / OBS (WASAPI)
```

## Using it

1. Download `AudioFlowMatrix.zip` from [Releases](https://github.com/SubielaDev/AudioFlowMatrix/releases)
   and unzip `AudioFlowMatrix.exe` and `AudioFlowMatrixASIO.dll` into the same folder.
2. Run `AudioFlowMatrix.exe`, open **Devices**:
   - pick the master ASIO driver (e.g. *Focusrite USB ASIO*) and the sample rate;
   - press **Register driver (administrator)** once, which installs "AudioFlow Matrix ASIO";
   - optionally enable the USB mic and the stream output (for Discord/OBS use a virtual
     cable such as VB-CABLE: render to *CABLE Input*, use *CABLE Output* as the mic there).
3. Press **Start**. In Traktor: *Preferences > Audio Setup > AudioFlow Matrix ASIO*.
4. **Matrix** tab: click a cell to route, right-click for gain / mute / pan.
   **Mixer** tab: input and output-bus faders with peak meters.

Settings are saved in `%APPDATA%\AudioFlowMatrix\settings.ini`.
`AudioFlowMatrix.exe --list` prints the detected ASIO and WASAPI devices.

## Latency budget

| Path | Added by AudioFlow Matrix |
|---|---|
| ASIO in → matrix → ASIO out | 0 (same callback) |
| Traktor ↔ matrix | 1 block (e.g. 64 samples = 1.33 ms @ 48 kHz) |
| USB mic → matrix | WASAPI period + ~1.5 blocks of ring headroom |

The engine itself is allocation-free, lock-free and `noexcept` on the audio thread.

## Layout

```
src/core      lock-free SPSC rings (heap and shared-memory), triple buffer, aligned memory
src/dsp       AVX2/FMA matrix mixer (runtime-dispatched), cubic drift resampler + PI loop
src/engine    AudioEngine (bus layout, process, telemetry), RoutingModel (dB/mute/pan)
src/platform  Win32 helpers: COM pointer, MMCSS, UTF-8
src/asio      ASIO ABI, host for the master driver, virtual driver DLL + shared-memory bridge
src/wasapi    WASAPI capture/render endpoints (exclusive with shared fallback)
src/app       Dear ImGui front-end (Win32 + OpenGL 3)
tests         unit, multithreaded stress, SIMD-vs-reference, drift-lock simulation
third_party   Dear ImGui 1.91.8 (MIT)
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

Any Clang/GCC MinGW toolchain (including `zig c++`) also works with the Ninja generator.

## License

MIT, see [LICENSE](LICENSE). Dear ImGui is MIT licensed by Omar Cornut.
