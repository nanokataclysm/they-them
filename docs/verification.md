# MVP verification — 2026-09-14

Project root: `they-them`. This record distinguishes executable
and numerical checks from a microphone/headphone listening session.

## Build environment

Ubuntu 26.04.1 LTS, x86-64, Intel Core i7-7820HQ (4 cores / 8 threads), GCC 15.2.0,
CMake 4.2.3. JUCE 9.0.2 and stftPitchShift 2.0 are pinned in the project.
The Release build started with a new build directory. Both standalone and VST3
were subsequently rebuilt after replacing the rejected prototype DSP backend.

The host had the ALSA runtime but lacked `libasound2-dev`. No system packages or
system configuration were changed. The Ubuntu development package was downloaded and
extracted into `/tmp/they-them-dependency-review/sysroot`; its build-local
pkg-config prefix and linker symlinks point at those headers and the installed
ALSA runtime. JUCE was likewise downloaded to a temporary local source directory.
Normal users with README prerequisites installed can use the two standard CMake
commands without this workaround. The exact final configure here was:

```bash
PKG_CONFIG_PATH=/tmp/they-them-dependency-review/sysroot/usr/lib/x86_64-linux-gnu/pkgconfig \
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DTHEY_THEM_JUCE_SOURCE_DIR=/tmp/they-them-dependency-review/JUCE-72782788ce18c2d4d760b28e0921d6ffc6431102 \
  -DCMAKE_LIBRARY_PATH=/tmp/they-them-dependency-review/sysroot/usr/lib/x86_64-linux-gnu \
  -DCMAKE_SKIP_BUILD_RPATH=TRUE
cmake --build build -j3
ctest --test-dir build -V
```

Build outputs exist and `file` identifies both as x86-64 ELF binaries:

- `build/TheyThem_artefacts/Release/Standalone/they-them`
- `build/TheyThem_artefacts/Release/VST3/they-them.vst3/Contents/x86_64-linux/they-them.so`

The VST3 bundle also contains its generated `moduleinfo.json`. `ldd` resolves
standalone dependencies to installed system libraries, with no missing libraries
and no temporary build RPATH required.

## Latency methodology

The engine reports 768 samples. Neutral transformation (pitch/formant/Character
zero, utility filters disabled) places a unit-test impulse's largest output
sample at that same delay. The measurement was repeated at 44.1 and 48 kHz with
64/128/256-sample buffers, and a separate development probe varied impulse offset
within the host block (0, 31, 64, 127 samples).

| Sample rate | Reported latency | Measured neutral impulse peak | DSP delay |
| --- | ---: | ---: | ---: |
| 44,100 Hz | 768 samples | 768 samples | 17.41497 ms |
| 48,000 Hz | 768 samples | 768 samples | 16.00000 ms |

The asymmetric phase-vocoder window uses 4096 samples of causal analysis history,
256 synthesis samples, and a 64-sample hop. The upstream virtual phase offset
is retained: dropping it shortened the peak delay but failed pitch correctness.
Some small spectral impulse energy precedes the peak; a development probe found
initial nonzero response around 116–122 samples. The neutral peak-amplitude ripple
is about +0.877%, tested below 1%. This is not a transparent identity transform.

Dry, utility-only transformation bypass, and global/host bypass use an explicit
768-sample delay. Their gain/mix tests use tighter tolerances than the spectral
neutral impulse. The final ±1 sample clamp remains active in global bypass.
Shifted transients need not have the same shape or frequency-dependent delay as
a neutral impulse. None of these measurements includes ADC/DAC conversion,
device buffers, audio-server scheduling, DAW routing, or physical round-trip delay.

## Automated test result

Final Release CTest: **2/2 suites passed**, **40.57 seconds** total. The DSP suite
passed **1,399 checks**; the JUCE processor suite passed parameter/state, bus,
host-bypass, transformed mono fanout, and independent stereo routing checks.
The exact run is retained locally in `build/qa/ctest-release.log`.

Coverage includes 44.1/48 kHz with 64/128/256 samples, irregular/oversized host
blocks, cold and automated processing with **zero C++ new/delete calls**, silence,
reset, finite-value recovery, gain limits, dry/wet math, and utility/host bypass.
Near-unity pitch/formant values cover the patched Debug resampler assertion.

Selected numerical measurements, with utilities and Character disabled:

| Input / control | 44.1 kHz output | 48 kHz output |
| --- | ---: | ---: |
| 100 Hz tone, pitch +12 | 200.001 Hz | 199.999 Hz |
| 220 Hz tone, pitch +12 | 439.991 Hz | 439.996 Hz |
| 220 Hz tone, pitch −12 | 110.351 Hz | 110.791 Hz |
| 220 Hz tone, pitch +3.5 | 269.285 Hz | 269.299 Hz |
| 100 Hz synthetic vowel, pitch +12 | 200.004 Hz | 200.000 Hz |
| Same vowel, formant +6 or −6, pitch zero | 100.000 Hz | 100.000 Hz |

The synthetic vowel's harmonic-amplitude centroid moved from about 2001 Hz to
2365–2372 Hz / 1424–1429 Hz with formant +6 / −6. With pitch +12 and neutral
formant, it stayed around 1963–1969 Hz rather than doubling with pitch. Combined
pitch +12 / formant +6 also passed. These fixtures demonstrate independent
controls, not subjective voice quality. The downward pure-tone octave case has
up to 0.72% frequency error; it is less accurate than the primary upward shift.

An additional assertions-enabled **Debug ASan/UBSan** build was configured:

```bash
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DTHEY_THEM_BUILD_PLUGIN=OFF \
  -DCMAKE_CXX_FLAGS_DEBUG='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build-sanitize -j2
```

The full instrumented DSP suite exceeded its 120-second CTest limit. A focused
probe instead passed **480 callbacks** across mono/stereo, 44.1/48 kHz, and
1/64/128/256/1025-sample blocks, exercising near-unity and extreme automation,
NaN/Inf input, reset, silence, and zero-sized calls. It reported no ASan/UBSan
errors. LeakSanitizer could not run in the restricted execution environment
(`LeakSanitizer does not work under ptrace`), so the passing focused run used
`ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1`.
This is focused sanitizer coverage, not a full sanitizer-suite or leak-check pass.
Its local source/receipt are `build/qa/sanitizer-safety-probe.cpp` and
`build/qa/sanitizer-focused.log`; the ordinary complete Release tests passed.

## Offline processing cost

Final Release full-stereo callback measurements on the i7-7820HQ:

| Rate / buffer | CPU p50 / p99 (ms) | Wall max (ms) | Buffer deadline (ms) |
| --- | ---: | ---: | ---: |
| 44.1 kHz / 64 | 0.807 / 1.291 | 1.575 | 1.451 |
| 44.1 kHz / 128 | 1.618 / 2.303 | 3.061 | 2.902 |
| 44.1 kHz / 256 | 3.149 / 4.876 | 5.353 | 5.805 |
| 48 kHz / 64 | 0.785 / 1.214 | 1.681 | 1.333 |
| 48 kHz / 128 | 1.573 / 2.315 | 3.143 | 2.667 |
| 48 kHz / 256 | 3.168 / 4.601 | 5.267 | 5.333 |

Some callbacks exceeded their deadline. This is an offline benchmark on an
active desktop, without real-time scheduling, and is not a glitch-free guarantee.
The final processor transforms a mono microphone only once before copying to
stereo headphones. A separate strict-math, FTZ/DAZ-enabled mono development probe
at 48 kHz / 64 samples measured CPU medians 0.428–0.596 ms, p99 0.691–0.749 ms,
and maxima 0.732–1.231 ms across two runs. No unsafe fast-math flags were adopted.
Prefer one selected microphone input and begin with a 128-sample buffer; try 256
if necessary. Keep host/interface xrun monitoring enabled during live acceptance.

## Ardour and standalone checks

The installed **Ardour 9.0.0** scanner loaded the final VST3 module, recognized
`they-them` as an audio effect, and recorded two audio inputs/outputs with no MIDI.
Scanner exit status was zero; the receipt is `build/qa/ardour-scanner.log`. This
is plugin discovery, not an Ardour track-processing or plugin-editor test.

```bash
XDG_CACHE_HOME="$PWD/build/qa/ardour-cache" \
XDG_CONFIG_HOME="$PWD/build/qa/ardour-config" \
LD_LIBRARY_PATH=/usr/lib/ardour9 \
/usr/lib/ardour9/ardour-vst3-scanner -f -v \
  "$PWD/build/TheyThem_artefacts/Release/VST3/they-them.vst3"
```

The standalone was launched on the KWin/Wayland desktop through X11. A debugger
snapshot found its main thread waiting in `juce::MessageManager::runDispatchLoop`
and ALSA/PipeWire audio threads running; no crash was observed. Automated window
matching did not locate its window, so no screenshot or visual acceptance is
claimed. A separate debugger launch exited normally. All probe-owned processes
were closed. Input mute remained enabled throughout; no microphone listening
session was performed. Local receipts are `build/qa/standalone-smoke.json`,
`standalone-startup-debug.log`, and `standalone-quit-debug.log`.

JUCE's stock Linux standalone writes its normal preferences to
`~/.config/they-them.settings`; it hardcodes that folder even when XDG_CONFIG_HOME
is set. This project-specific preference file was created by the smoke run and
retained. No custom audio subsystem or system audio configuration was added.

## Remaining acceptance

Actual microphone capture, headphone listening, timbral quality, acoustic feedback
behavior, hardware xruns, and microphone-to-headphone round-trip latency require
a person and a connected interface. Synthetic signals cannot establish those.
The smallest next step is the README headphone/microphone test at 48 kHz with a
128-sample buffer, using the standalone's normal device settings.
