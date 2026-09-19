# Version 0.2.0 verification — 2026-09-15

Project: `/home/nanokat/dev/they-them`. This pass adds the editor, factory/file
presets, and meters. The pitch/formant algorithm, parameter IDs/ranges/defaults,
bus layouts, bypass behavior, and reported latency are preserved.

## Build and outputs

The Linux x86-64 Release build completed for VST3, standalone, DSP tests,
processor tests, and editor tests. The final incremental build reported no
compiler warnings or errors in `build-next/build-final.log`. The initial dependency
compilation emitted warnings from the Ubuntu ALSA headers.

The previous `build/` output is retained. **The new version is in `build-next/`:**

- `build-next/TheyThem_artefacts/Release/VST3/they-them.vst3`
- `build-next/TheyThem_artefacts/Release/Standalone/they-them`

`file` identifies both binaries as x86-64 ELF. The standalone's `ldd` output
resolves all dependencies to installed libraries. Its dynamic section has no
build-directory RPATH/RUNPATH. The VST3 manifest advertises version 0.2.0; the
Steinberg SDK generates this `.json` file in its documented JSON5 format.

The original build referenced a deleted `/tmp` dependency directory. This pass
downloaded the same pinned JUCE archive, checked its SHA256 against CMake, and
extracted it into the ignored `.build-deps/` directory. The missing Ubuntu ALSA
development package was downloaded and extracted there too. No system package
installation was required. Its local pkg-config prefix and linker symlink point
to these headers and the installed ALSA runtime. The final configuration was:

```bash
PKG_CONFIG_PATH="$PWD/.build-deps/sysroot/usr/lib/x86_64-linux-gnu/pkgconfig" \
cmake -S . -B build-next -DCMAKE_BUILD_TYPE=Release \
  -DTHEY_THEM_JUCE_SOURCE_DIR="$PWD/.build-deps/JUCE-72782788ce18c2d4d760b28e0921d6ffc6431102" \
  -DCMAKE_LIBRARY_PATH="$PWD/.build-deps/sysroot/usr/lib/x86_64-linux-gnu" \
  -DCMAKE_SKIP_BUILD_RPATH=TRUE
cmake --build build-next -j3
```

With the README's normal Ubuntu prerequisites installed, a standard CMake build
does not need this local dependency workaround.

## Automated checks

| Gate | Result | Evidence |
| --- | --- | --- |
| Complete Release CTest | **2/2 suites passed**, 74.43 seconds | `build-next/qa/ctest-release.log` |
| DSP suite | **1,399 checks passed** | Same log |
| Processor contracts | Parameter/state, routing, bypass, presets, and metering passed | Same log |
| Editor smoke | Resize/bounds/overlap, attachments, presets, bypass, rendering, and eight reopen cycles passed | `build-next/qa/editor-smoke.log` |
| Ardour VST3 scanner | Exit **0**; version 0.2.0 found as an audio effect | `build-next/qa/ardour-scanner.log` |

The processor suite verifies:

- All eight factory presets notify parameter listeners with change gestures;
  input/output trim and global bypass remain unchanged while browsing.
- Editing a voice removes its factory match. Invalid factory indices have no effect.
- All ten parameters round-trip through a `.ttvoice` file, including trims and bypass.
- Malformed, truncated, incomplete, duplicate-ID, nonfinite, out-of-range,
  fractional-boolean, oversized, missing, and unsupported-version preset files
  are rejected without partially changing any parameter.
- Saving over an existing preset succeeds, while an invalid destination reports
  failure and leaves the existing preset intact. Disk-full failure is handled
  in code by checking write/flush status before replacement; a full disk was not simulated.
- Input meters reflect input trim and independent stereo levels. Output meters
  detect a 1.2-amplitude signal before the audio clamp returns a bounded 1.0 signal.
- Short peaks survive between UI reads; reads drain old data; reset clears meters.
- NaN/Inf input does not poison meter data. Compression and host bypass are reflected.
- Cold and repeated full processor callbacks, including metering, perform **zero
  instrumented C++ new/delete calls**. The original DSP allocation and automation
  checks also pass. This instrumentation does not intercept every possible C allocation API.

The existing parameter/state tests retain the original ten controls and state
format, including tolerant restoration of partial/malformed host state. User
preset files use stricter validation than host state.

## Editor evidence

`TheyThemEditorTests` creates the actual JUCE editor without opening audio devices.
It renders at **800 × 620**, **960 × 680**, and **1440 × 1000**, checking that direct
controls remain inside the window and do not overlap. All three renders were
visually inspected. It exercises slider-to-processor updates, factory menu
selection, the Custom label, bypass, and state displayed after repeated reopening.

The metered render uses a synthetic 180 Hz tone, with a lower level on the right
channel. It is saved in `build-next/qa/editor-processing.png` and copied into
[the README preview](images/editor.png). The other renders are in the same QA folder.

This is component rendering and programmatic UI interaction. Native file-picker
interaction and embedding/resizing the editor inside a DAW were not exercised.
Preset file I/O itself is covered by the processor suite. No new sanitizer-suite
or leak-check result is claimed for this pass.

## Audio behavior and processing cost

Neutral impulse-peak latency remains **768 samples** at both 44.1 and 48 kHz,
including 64/128/256-sample blocks. This corresponds to **17.415 ms / 16.000 ms**
of DSP delay. Pitch and synthetic-vowel results agree with the
[original verification](verification.md); for example, a 220 Hz tone at +12 st
measures 439.991 Hz at 44.1 kHz and 439.996 Hz at 48 kHz.

Full-suite offline callback timing varied substantially; several callbacks
exceeded their buffer deadlines. A focused comparison then linked the same probe
against the retained original DSP archive and the new archive. Each run used
128 warm-up blocks followed by 512 stereo blocks at 48 kHz / 128 samples, with
thread CPU timing and FTZ/DAZ enabled:

| Run order | CPU p50 | CPU p99 | CPU maximum |
| --- | ---: | ---: | ---: |
| Original | 1.663 ms | 2.679 ms | 2.887 ms |
| 0.2.0 | 1.710 ms | 2.545 ms | 2.958 ms |
| 0.2.0 | 1.633 ms | 2.691 ms | 3.065 ms |
| Original | 1.630 ms | 2.611 ms | 2.945 ms |

The focused distributions are similar. This suggests the large full-suite timing
swings were runtime variability rather than a large metering regression; it does
not establish a precise overhead bound. Both engines sometimes exceeded the
**2.667 ms** block deadline. Receipts and probe source are
`build-next/qa/meter-overhead.log` and `build-next/qa/meter-overhead-probe.cpp`.

## Host check and remaining listening work

Ardour's installed scanner loaded the actual VST3 module and identified two audio
inputs, two audio outputs, no MIDI buses, and the preserved plugin identifier.
This proves discovery; it does not prove track playback, DAW automation recording,
native file dialogs, or a microphone/headphone monitoring session.

The standalone was built; this pass used the device-free editor harness for GUI
validation. Microphone capture, consonant/transient quality, subjective preset
quality, xruns, acoustic feedback behavior, and actual round-trip latency still
need a connected interface and listening session. Begin with the README's
headphone setup, a single enabled microphone input, and a 128- or 256-sample buffer.

This is a local Linux build. Installation into a user's plugin directory,
macOS/Windows builds, installers, signing, and publication are outside this pass.

## Requested repeat QA — 2026-09-15

A fresh build check, complete CTest run, editor smoke, and uncached Ardour scan
passed against the same v0.2.0 binaries. Both binary SHA256 values matched the
previous verification receipt. No source-code fixes were needed.

- **CTest: 2/2 passed in 44.20 seconds**, including all **1,399 DSP checks** and
  the preset, state, meter, routing, and bypass contracts.
- **Editor: passed** resizing, overlap checks, parameter attachments, preset
  selection, bypass, metered rendering, and eight reopen cycles. The newly
  rendered processing view was visually inspected.
- **Ardour scan: exit 0**, identifying v0.2.0 with two audio inputs/outputs and no MIDI.
- **Latency remains 768 samples**; instrumented callback C++ new/delete remains zero.

The existing real-time limitation remains: some offline callbacks exceeded their
buffer deadline (48 kHz / 128 samples: wall p99 2.590 ms, maximum 3.444 ms,
deadline 2.667 ms). Live microphone/headphone acceptance is still outstanding.

Repeat receipts are in `build-next/qa/recheck/`: `ctest.log`, `editor.log`,
`ardour-scanner.log`, `summary.json`, and fresh editor renders. The build check is
`build-next/qa/recheck-build.log`.
