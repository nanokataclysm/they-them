# they-them

A small, original C++ vocal effect for live experiments and recording. Builds a
**VST3 audio effect** and a **standalone JUCE application** on Ubuntu. This is an
MVP, not a finished voice-conversion product or a recreation of a commercial
plugin. No AI, cloud inference, models, accounts, telemetry, MIDI functionality,
or licensing service is used by the application.

## Build on Ubuntu

Requirements: CMake 3.22+, a C++20-capable compiler, and the usual JUCE Linux development
packages. Install missing packages through Ubuntu's normal package manager:

```bash
sudo apt install build-essential cmake pkg-config ca-certificates \
  libasound2-dev libx11-dev libxext-dev libxinerama-dev libxrandr-dev \
  libxcursor-dev libfreetype-dev libfontconfig1-dev libgl1-mesa-dev

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Use `-j2` instead on a machine with limited free memory; JUCE's GUI translation
units are large. The first configure also builds JUCE's `juceaide` helper and can
take several minutes. No global JUCE installation or Projucer project is needed.

CMake downloads **JUCE 9.0.2** at a pinned commit and verifies the archive's SHA256.
Network access is needed only for that first source acquisition. To use an
existing copy or configure offline, supply its source directory:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DTHEY_THEM_JUCE_SOURCE_DIR=/path/to/JUCE-9.0.2
```

The small MIT DSP dependency is already in `ThirdParty/`. A DSP-only build
does not need JUCE or audio-device development packages:

```bash
cmake -S . -B build-dsp -DCMAKE_BUILD_TYPE=Release -DTHEY_THEM_BUILD_PLUGIN=OFF
cmake --build build-dsp -j2
ctest --test-dir build-dsp --output-on-failure
```

Build just one format with `--target TheyThem_Standalone` or
`--target TheyThem_VST3` after configuring. The default build also includes tests.

### Outputs

- VST3 bundle: `build/TheyThem_artefacts/Release/VST3/they-them.vst3`
- Linux x86-64 plugin binary inside the bundle:
  `Contents/x86_64-linux/they-them.so`
- Standalone: `build/TheyThem_artefacts/Release/Standalone/they-them`
- DSP test/measurement executable: `build/TheyThemDspTests`
- JUCE processor tests: `build/TheyThemPluginTests`

## First microphone test

1. Connect **headphones**, turn their level down, and disable the interface's
   direct monitor if you want to hear only the processed voice. Keep loudspeakers
   off for this test: output limiting cannot prevent acoustic feedback.
2. Run `./build/TheyThem_artefacts/Release/Standalone/they-them`.
3. Use JUCE's **Options > Audio/MIDI Settings** to select the input device,
   microphone input channel, output device, sample rate, and buffer size. The
   standard JUCE dialog's title mentions MIDI; this effect does not process MIDI.
4. Start with 48 kHz and 128 samples. JUCE initially mutes the input to prevent
   accidental feedback. After choosing the devices and checking headphone
   volume, use **Unmute Input**.
5. Speak, adjust Pitch and Formant independently, then try 64 samples if stable.
   If there are crackles, use 256 samples and check the audio system's xruns.

The default state is Pitch **+12 st**, Formant **0 st**, Character **65%**, Wet
**100%**, and input/output gain **0 dB**. Character adds a gentle formant offset;
set it to zero to evaluate pitch compensation with a neutral formant setting.
Stereo inputs remain separate. A mono input is transformed once and sent to both
stereo outputs, avoiding a duplicate spectral processing pass.
For a single microphone, enable only that input channel in the device dialog.

## Load in Ardour

Copy the whole VST3 bundle to the standard per-user plugin directory:

```bash
mkdir -p ~/.vst3
cp -a build/TheyThem_artefacts/Release/VST3/they-them.vst3 ~/.vst3/
```

Rescan plugins in Ardour's Plugin Manager (or restart Ardour), then insert
**they-them** on an audio track. Select the microphone as the track input and
enable **input monitoring** in Ardour. Route the track to the headphone output.
Ardour controls devices, sample rate, and buffer size when using the VST3.
Avoid monitoring the microphone simultaneously through the interface's dry
monitor and the delayed plugin unless that blend is intentional.

## Controls and signal path

```text
Input gain → 80 Hz high-pass → gentle compressor
           → pitch + compensated formant transformation
           → latency-aligned dry/wet mix → output gain → sample safety clamp
```

| Control | Range | Default | Behavior |
| --- | --- | --- | --- |
| Pitch | −12 to +12 semitones, continuous | +12 | Changes musical pitch |
| Formant | −12 to +12 semitones, continuous | 0 | Shifts the spectral envelope independently of pitch |
| Character | 0–100% | 65% | Adds 0 to +2 semitones of conservative formant offset |
| Dry / Wet | 0–100% | 100% | Linear blend; dry path follows input gain and is delay-aligned |
| Input | −24 to +24 dB | 0 | Before the utility chain |
| Output | −48 to +12 dB | 0 | After dry/wet mixing |

High-pass, compressor, and transformation have separate enable switches. Global
Bypass (including host bypass) ignores the gains and effects and returns delayed
unity input within normal ±1 sample bounds; the final safety clamp remains active.
Bypass changes and continuous controls are smoothed over a 20 ms
time constant. This smoothing does not add audio lookahead.

The high-pass is a second-order 80 Hz Butterworth filter. Compression is linked
across stereo channels, 2:1 above −18 dBFS, with 10 ms attack, 100 ms release,
and no makeup gain. Dry is taken after input gain and before those utilities.
The output has a final finite-value/sample clamp to ±1; it is a last-resort
clipping guard, not a transparent limiter or a feedback suppressor.

## Algorithm, latency, and real-time operation

A persistent streaming adapter composes **stftPitchShift 2.0**'s deterministic
phase vocoder and cepstral envelope processing. It estimates a smooth spectral
envelope, removes it from the excitation, transposes the excitation, then restores
the independently shifted envelope. Pitch and formant share one analysis/synthesis
pipeline. The cepstral lifter is 1.5 ms; no pitch detector or voice classifier is
used. The upstream offline wrapper is not called from the audio callback.

The asymmetric window uses **4096 analysis samples, 256 synthesis samples, and a
64-sample hop**. Analysis uses past input, and upstream phase compensation is
retained. The host is told **768 samples** through `setLatencySamples()`; the UI
shows the corresponding **17.415 ms at 44.1 kHz / 16.000 ms at 48 kHz**. Neutral
transform impulse-peak measurements match that delay. This is a spectral effect:
small impulse energy arrives before the peak, and the neutral impulse has about
0.88% peak-amplitude ripple. Dry/wet alignment uses the measured peak delay.
See [current verification](docs/verification.md) for measurements and test results.

Dry, transformation bypass, and global bypass retain that same latency. Hardware
conversion, device buffers, audio-server routing, and other DAW effects add
further delay. The DSP impulse test is **not** a measurement of microphone-to-
headphone round-trip latency.

Buffers, FFT state, delay lines, and coefficients are prepared outside the audio
callback. The callback uses cached parameter atomics, fixed-size chunks, and
preallocated state. It performs no file/network operations, logging, mutex
locking, or heavyweight initialization. Automated tests instrument C++ heap
allocation/deallocation during processing and stress parameter changes. Offline
callback timing is diagnostic; it does not certify a real-time Linux scheduler.

## Tests and current limits

`ctest` checks parameter ranges/defaults, state round-trip and malformed state,
dry/wet math, utility/global/host bypass, mono/stereo behavior, silence/reset,
NaN/Inf recovery, gain bounds, reported versus measured DSP latency, and common
44.1/48 kHz rates with 64/128/256-sample buffers. The DSP executable additionally
reports synthetic-vowel pitch/envelope measurements and callback timing.

- The analysis history spans 92.9 ms at 44.1 kHz / 85.3 ms at 48 kHz even though
  output peak delay is much shorter. Transients can smear; breathy/noisy input,
  consonants, and extreme shifts need listening tests.
- True stereo processing is CPU-heavy on the tested older laptop; some offline
  callback timings exceeded buffer deadlines. Prefer one microphone input and
  begin at 128 samples; use 256 if needed. No glitch-free live claim is made.
- Downward octave shifts showed up to 0.72% pure-tone frequency error in the
  measured cases; upward octaves were substantially more accurate.
- Formant correction is approximate; Character is a small timbral offset, not
  an identity or voice model. Pitch/formant changes cannot guarantee a particular
  perceived identity.
- Large boosts can hit the sample clamp and audibly distort; reduce input/output
  gain if necessary. A hot microphone preamp can clip before the plugin receives
  any samples.
- Only Linux is exercised in this pass. No installers, preset manager, other
  plugin formats, or custom Linux audio backend are included.
- Saved parameters are supported through normal host/JUCE state handling.
- See [verification](docs/verification.md) for exactly what was exercised and
  what still requires a person with a microphone and headphones.

## Licenses

stftPitchShift 2.0 is **MIT**; its notice
and exact revision are in [ThirdParty/README.md](ThirdParty/README.md).
JUCE 9.0.2 is **AGPLv3 or commercial**, not permissively licensed. The included
Steinberg VST3 SDK is **MIT**. Do not assume that the MIT DSP dependency license
makes the combined plugin MIT: distribution must comply with the selected JUCE
license. This MVP does not assign a distribution license to the original code.
