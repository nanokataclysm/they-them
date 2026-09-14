#pragma once

// The spectral processing sequence below follows stftPitchShift by Juergen Hock.
// Portions Copyright (c) 2022 Juergen Hock, distributed under the MIT License.
// The full notice is retained in ThirdParty/stftpitchshift/LICENSE.
// The streaming rings, preparation, and control integration are project code.

#include <StftPitchShift/STFT.h>
#include <StftPitchShift/Vocoder.h>
#include <StftPitchShift/Pitcher.h>
#include <StftPitchShift/Cepster.h>
#include <StftPitchShift/Resampler.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <functional>
#include <memory>
#include <span>
#include <tuple>
#include <vector>

namespace theythem::detail
{
// A long causal analysis history resolves low vocal harmonics; the short
// asymmetric synthesis window keeps the measured output delay at 768 samples.
// No future input is requested. The delay includes the upstream Vocoder's
// asymmetric-window phase compensation, not just the synthesis window length.
class StreamingTransform
{
public:
    static constexpr int analysisSamples = 4096;
    static constexpr int synthesisSamples = 256;
    static constexpr int hopSamples = 64;
    static constexpr int reportedLatency = 768;

    void prepare(double sampleRate, int channels)
    {
        channelCount = channels;
        for (int channel = 0; channel < channelCount; ++channel)
            state[channel] = std::make_unique<Channel>(sampleRate);
    }

    void reset() noexcept
    {
        for (int channel = 0; channel < channelCount; ++channel)
            state[channel]->reset();
    }

    void setPitch(float semitones) noexcept
    {
        const double factor = std::pow(2.0, static_cast<double>(semitones) / 12.0);
        for (int channel = 0; channel < channelCount; ++channel)
            state[channel]->setPitch(factor);
    }

    void setFormant(float semitones) noexcept
    {
        const double factor = std::pow(2.0, static_cast<double>(semitones) / 12.0);
        for (int channel = 0; channel < channelCount; ++channel)
            state[channel]->formantResampler.factor(factor);
    }

    void process(float* const* input, float* const* output, int samples) noexcept
    {
        for (int sample = 0; sample < samples; ++sample)
            for (int channel = 0; channel < channelCount; ++channel)
                output[channel][sample] = state[channel]->processSample(input[channel][sample]);
    }

private:
    struct Channel
    {
        // Double precision also avoids the upstream float path's loss of phase
        // resolution during long sessions. No phase reset occurs on automation.
        std::shared_ptr<stftpitchshift::RFFT> fft = std::make_shared<stftpitchshift::RFFT>();
        stftpitchshift::STFT<double> stft;
        stftpitchshift::Vocoder<double> vocoder;
        stftpitchshift::Pitcher<double> pitcher;
        stftpitchshift::Cepster<double> cepster;
        stftpitchshift::Resampler<double> formantResampler;

        std::vector<double> inputHistory, overlap, frameInput, frameOutput, envelope;
        std::vector<double> pitchFactors { 1.0 };
        std::function<void(std::span<std::complex<double>>)> spectrumCallback;
        int inputPosition = 0, outputPosition = 0, samplesSinceFrame = 0;
        double pitchFactor = 1.0;

        explicit Channel(double sampleRate)
            : stft(fft, std::make_tuple(analysisSamples, synthesisSamples), hopSamples),
              vocoder(std::make_tuple(analysisSamples, synthesisSamples), hopSamples, sampleRate),
              pitcher(analysisSamples, sampleRate),
              cepster(fft, analysisSamples, sampleRate),
              inputHistory(analysisSamples), overlap(2 * analysisSamples),
              frameInput(analysisSamples + 1), frameOutput(analysisSamples + 1),
              envelope(analysisSamples / 2 + 1)
        {
            pitcher.factors(pitchFactors); // Allocate exactly one voice during setup.
            cepster.quefrency(0.0015);      // 1.5 ms cepstral lifter; not a lookahead.
            formantResampler.factor(1.0);
            // One pointer capture fits std::function's inline storage. The test
            // suite also intercepts callback heap allocation, including cold use.
            spectrumCallback = [this](auto spectrum) { processSpectrum(spectrum); };

            // RFFT creates its plan lazily. Exercise both STFT and cepstral FFT
            // paths here, then discard the priming signal and phase state.
            stft(frameInput, frameOutput, spectrumCallback);
            reset();
        }

        void reset() noexcept
        {
            std::fill(inputHistory.begin(), inputHistory.end(), 0.0);
            std::fill(overlap.begin(), overlap.end(), 0.0);
            std::fill(frameInput.begin(), frameInput.end(), 0.0);
            std::fill(frameOutput.begin(), frameOutput.end(), 0.0);
            vocoder.reset();
            inputPosition = outputPosition = samplesSinceFrame = 0;
        }

        void setPitch(double factor) noexcept
        {
            if (factor == pitchFactor)
                return;
            pitchFactor = factor;
            pitchFactors[0] = factor;
            // The public Pitcher reuses its existing one-voice buffers. Calling
            // StftPitchShiftCore::factors instead would reset vocoder phase.
            pitcher.factors(pitchFactors);
        }

        void processSpectrum(std::span<std::complex<double>> spectrum) noexcept
        {
            vocoder.encode(spectrum);
            for (std::size_t bin = 0; bin < spectrum.size(); ++bin)
                envelope[bin] = spectrum[bin].real();
            cepster.lifter(envelope);

            // Separate the envelope from the excitation before pitch shifting.
            for (std::size_t bin = 0; bin < spectrum.size(); ++bin)
            {
                if (std::isnormal(envelope[bin]))
                    spectrum[bin].real(spectrum[bin].real() / envelope[bin]);
                else
                {
                    envelope[bin] = 0.0;
                    spectrum[bin].real(0.0);
                }
            }
            formantResampler.linear(envelope);
            pitcher.shiftpitch(spectrum);
            for (std::size_t bin = 0; bin < spectrum.size(); ++bin)
                spectrum[bin].real(spectrum[bin].real() * envelope[bin]);
            vocoder.decode(spectrum);
        }

        float processSample(float input) noexcept
        {
            const double output = overlap[outputPosition];
            overlap[outputPosition] = 0.0;
            inputHistory[inputPosition] = input;
            inputPosition = (inputPosition + 1) % analysisSamples;

            if (++samplesSinceFrame == hopSamples)
            {
                samplesSinceFrame = 0;
                for (int sample = 0; sample < analysisSamples; ++sample)
                    frameInput[sample] = inputHistory[(inputPosition + sample) % analysisSamples];
                frameInput[analysisSamples] = 0.0;
                std::fill(frameOutput.begin(), frameOutput.end(), 0.0);

                // Upstream STFT processes one frame when span size is N+1.
                stft(frameInput, frameOutput, spectrumCallback);
                constexpr int firstSynthesisSample = analysisSamples - synthesisSamples;
                for (int sample = firstSynthesisSample; sample < analysisSamples; ++sample)
                {
                    const int destination = (outputPosition + 1 + sample - firstSynthesisSample)
                                            % (2 * analysisSamples);
                    overlap[destination] += frameOutput[sample];
                }
            }

            outputPosition = (outputPosition + 1) % (2 * analysisSamples);
            return static_cast<float>(output);
        }
    };

    int channelCount = 0;
    std::array<std::unique_ptr<Channel>, 2> state;
};
} // namespace theythem::detail
