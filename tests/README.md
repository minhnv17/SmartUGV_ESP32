# Host checks

Build the standalone CMake project in Debug mode so C assertions remain active.
`managed_components/espressif__cjson` must already be installed by ESP-IDF.

```powershell
cmake -S tests -B build/host_music_tests -G "Visual Studio 17 2022" -A x64
cmake --build build/host_music_tests --config Debug
ctest --test-dir build/host_music_tests -C Debug --output-on-failure
```

The catalog test compiles the production catalog parser and LCD text converter.
The speaker tests compile the production speaker task with a deterministic,
single-threaded FreeRTOS/I2S substitute. Tests cover voice priority and drain,
pause preserving queued PCM, track replacement rejecting old epochs, voice
flush during music, and short music clips releasing prefill on end-of-stream.
They do not exercise the real scheduler, DMA, HTTP, MP3 decoder or hardware.
The headers under `host/` are only included by this standalone test project.

Scenario 6 uses a model of a 90ms DMA queue and 60 ten-millisecond PCM
packets. It reproduces 59 underruns with the old per-packet 20ms voice wait
and checks zero underruns after the fix. Scenario 7 checks volume ramping,
played-sample counters and the meter returning to idle. These are scheduler
models, not measurements of the real DAC or speaker.

The touch test compiles the production FT6336 coordinate parser, landscape
transform and gesture handler. It covers both orientations, button edges,
startup with a finger down, press/release debounce, holding without repeat,
drag cancellation, UI changes, read errors and multitouch. It does not replace
checking I2C operation and physical alignment on the real touch panel.
It also checks volume buttons and slider drags clamped at 0/100 percent.

The microphone test compiles the production 16 kHz frontend. Synthetic PCM
checks amplification of weak input, first-frame limiting on a sudden loud
transition, a low-level signal remaining nonzero without driving gain to its
maximum, DC rejection, signed extrema, odd tails, measurement windows and
reset of filter/gain history after speaker mute. This does not measure the
microphone, acoustic signal-to-noise ratio, recognition accuracy or distance.
