# Pinned dependency and license review

The redistributed DSP dependency is **stftPitchShift 2.0**, by Juergen Hock,
under the **MIT license**. Its license was inspected before adoption and is
retained in [stftpitchshift/LICENSE](stftpitchshift/LICENSE).

- Source: [upstream revision](https://github.com/jurihock/stftPitchShift/tree/1a4f21f6c300a785cce860e374fd63cb5e9ba154)
- Commit: `1a4f21f6c300a785cce860e374fd63cb5e9ba154`
- Included headers under `stftpitchshift/StftPitchShift/`: `STFT.h`, `FFT.h`,
  `RFFT.h`, `Timer.h`, `Vocoder.h`, `Arctangent.h`, `Pitcher.h`, `Cepster.h`,
  `Resampler.h`.
- One local patch: `Resampler.h` asserts `m >= n` instead of `m > n` in the
  upsampling branch. Integer truncation can legitimately produce `m == n`
  for continuous factors just above one. The release algorithm is unchanged;
  this prevents a false assertion during near-unity automation in Debug builds.
  All other included header contents are unchanged from the pinned revision.

`Source/StreamingTransform.h` carries attribution for the composed upstream
spectral processing sequence. It adds persistent preallocated rings, setup-time
FFT preparation, and phase-preserving control updates. The upstream offline
wrapper and CLI are not included. The built-in FFT needs no extra dependency.
The DSP implementation requires C++20; the public engine API and JUCE wrapper
use C++17. No runtime network access, downloads, or models are needed.

## Algorithm selection

Signalsmith Stretch 1.3.2 (MIT) and its Linear 0.3.1 dependency (MIT) were also
inspected and prototyped. The tested short-window configuration failed the
pure-tone and dense-vowel octave-shift tests. Larger tested configurations did
not provide an acceptable accuracy/latency combination. Those dependencies are
not shipped in this project.

The selected stftPitchShift adapter uses a 4096/256 asymmetric analysis/synthesis
window and 64-sample hop. This resolves low vocal harmonics while retaining a
measured 768-sample impulse-peak delay. Functional pitch/formant tests determine
acceptance; subjective live quality still requires microphone/headphone testing.

## JUCE and VST3

CMake separately acquires **JUCE 9.0.2** at revision
`72782788ce18c2d4d760b28e0921d6ffc6431102`, with archive SHA256
`28ec8ae0626a4d1c6ca8c32ad13f95868b147779d4f3df4a7868f333a38e0551`.
Its modules are dual-licensed under **AGPLv3 or a commercial JUCE license**.
The inspected terms are [JUCE LICENSE.md](https://github.com/juce-framework/JUCE/blob/72782788ce18c2d4d760b28e0921d6ffc6431102/LICENSE.md).
The bundled Steinberg VST3 SDK is **MIT**, copyright 2025 Steinberg Media
Technologies GmbH. Its notice remains in the JUCE source at
`modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt`.
JUCE's complete third-party inventory is `JUCE.spdx.json` there.

Distribution must comply with the chosen JUCE license and all applicable
notices. The permissive DSP license does not make the combined plugin MIT.
This project is licensed under AGPLv3 for open-source distribution in compliance
with JUCE licensing terms.
