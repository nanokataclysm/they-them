#include "VocalEngine.h"
#include "StreamingTransform.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace theythem
{
namespace
{
constexpr int maximumChannels = 2;
constexpr int processingQuantum = 64;
constexpr double pi = 3.14159265358979323846;

float bounded(float value, float minimum, float maximum, float fallback) noexcept
{
    return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}

float clean(float value) noexcept
{
    return std::isfinite(value) && std::abs(value) >= 1.0e-20f ? value : 0.0f;
}

float dbToGain(float value) noexcept { return std::pow(10.0f, value * 0.05f); }

struct SmoothValue
{
    float value = 0.0f;
    float target = 0.0f;
    void initialise(float v) noexcept { value = target = v; }
    float next(float pole) noexcept
    {
        value = target + pole * (value - target);
        if (std::abs(value - target) < 1.0e-7f)
            value = target;
        return value;
    }
};

// Fixed 80 Hz, second-order Butterworth high-pass; coefficients are prepared once.
struct HighPass
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    std::array<float, maximumChannels> z1{}, z2{};

    void prepare(double rate) noexcept
    {
        const double omega = 2.0 * pi * 80.0 / rate;
        const double cosine = std::cos(omega);
        const double alpha = std::sin(omega) / std::sqrt(2.0);
        const double denominator = 1.0 + alpha;
        b0 = static_cast<float>((1.0 + cosine) * 0.5 / denominator);
        b1 = static_cast<float>(-(1.0 + cosine) / denominator);
        b2 = b0;
        a1 = static_cast<float>(-2.0 * cosine / denominator);
        a2 = static_cast<float>((1.0 - alpha) / denominator);
        reset();
    }

    void reset() noexcept { z1.fill(0.0f); z2.fill(0.0f); }

    float process(float input, int channel) noexcept
    {
        const float output = clean(b0 * input + z1[channel]);
        z1[channel] = clean(b1 * input - a1 * output + z2[channel]);
        z2[channel] = clean(b2 * input - a2 * output);
        return output;
    }
};

struct Compressor
{
    float attack = 0.0f, release = 0.0f, envelope = 0.0f;
    static constexpr float threshold = 0.12589254f; // -18 dBFS; 2:1, no makeup gain.

    void prepare(double rate) noexcept
    {
        attack = static_cast<float>(std::exp(-1.0 / (rate * 0.010)));
        release = static_cast<float>(std::exp(-1.0 / (rate * 0.100)));
        envelope = 0.0f;
    }

    float gain(float stereoPeak) noexcept
    {
        const float pole = stereoPeak > envelope ? attack : release;
        envelope = clean(stereoPeak + pole * (envelope - stereoPeak));
        return envelope > threshold ? std::sqrt(threshold / envelope) : 1.0f;
    }
};
} // namespace

struct VocalEngine::Impl
{
    detail::StreamingTransform transform;
    double rate = 48000.0;
    int channels = 1;
    int chunkSize = processingQuantum;
    int latency = 0;
    int delayPosition = 0;
    bool initialisedControls = false;
    float controlPole = 0.0f;
    HighPass highPass;
    Compressor compressor;
    MeterReadings meters;

    SmoothValue pitch, formant, character, inputGain, outputGain, mix;
    SmoothValue highPassAmount, compressorAmount, transformAmount, bypassAmount;

    std::array<std::array<float, processingQuantum>, maximumChannels> input{}, wet{};
    std::array<std::array<float, processingQuantum>, maximumChannels> delayedRaw{}, delayedDry{}, delayedUtility{};
    std::array<float, processingQuantum> outputGains{}, mixAmounts{}, transformAmounts{}, bypassAmounts{};
    std::array<float*, maximumChannels> inputPointers{}, outputPointers{};

    // Three paths share a write cursor and have exactly the transform's latency.
    std::vector<float> delay;

    void prepare(double sampleRate, int maxBlockSize, int channelCount)
    {
        rate = std::isfinite(sampleRate) && sampleRate >= 8000.0 && sampleRate <= 384000.0
                   ? sampleRate : 48000.0;
        channels = std::clamp(channelCount, 1, maximumChannels);
        chunkSize = std::clamp(maxBlockSize, 1, processingQuantum);
        transform.prepare(rate, channels);
        latency = detail::StreamingTransform::reportedLatency;
        delay.resize(static_cast<std::size_t>(3 * channels * std::max(latency, 1)), 0.0f);
        highPass.prepare(rate);
        compressor.prepare(rate);
        controlPole = static_cast<float>(std::exp(-1.0 / (rate * 0.020)));
        for (int channel = 0; channel < channels; ++channel)
        {
            inputPointers[channel] = input[channel].data();
            outputPointers[channel] = wet[channel].data();
        }
        reset();
    }

    void reset() noexcept
    {
        transform.reset();
        highPass.reset();
        compressor.envelope = 0.0f;
        std::fill(delay.begin(), delay.end(), 0.0f);
        delayPosition = 0;
        initialisedControls = false;
        meters = {};
        for (int channel = 0; channel < maximumChannels; ++channel)
        {
            input[channel].fill(0.0f);
            wet[channel].fill(0.0f);
        }
    }

    void setTargets(const Parameters& parameters) noexcept
    {
        auto set = [this](SmoothValue& destination, float value)
        {
            if (!initialisedControls)
                destination.initialise(value);
            else
                destination.target = value;
        };
        set(pitch, parameters.pitchSemitones);
        set(formant, parameters.formantSemitones);
        set(character, parameters.character);
        set(inputGain, dbToGain(parameters.inputGainDb));
        set(outputGain, dbToGain(parameters.outputGainDb));
        set(mix, parameters.mix);
        set(highPassAmount, parameters.highPassEnabled ? 1.0f : 0.0f);
        set(compressorAmount, parameters.compressorEnabled ? 1.0f : 0.0f);
        set(transformAmount, parameters.transformEnabled ? 1.0f : 0.0f);
        set(bypassAmount, parameters.bypassed ? 1.0f : 0.0f);
        initialisedControls = true;
    }

    float delaySample(float sample, int path, int channel) noexcept
    {
        if (latency == 0)
            return sample;
        const auto index = static_cast<std::size_t>((path * channels + channel) * latency + delayPosition);
        const float previous = delay[index];
        delay[index] = sample;
        return previous;
    }

    void process(float* const* audio, int activeChannels, int samples, const Parameters& parameters) noexcept
    {
        meters = {};
        float minimumCompressionGain = 1.0f;
        setTargets(parameters);
        for (int offset = 0; offset < samples;)
        {
            const int count = std::min(chunkSize, samples - offset);
            const float chunkPole = std::pow(controlPole, static_cast<float>(count));
            const float pitchValue = pitch.next(chunkPole);
            const float formantValue = formant.next(chunkPole);
            const float characterValue = character.next(chunkPole);
            transform.setPitch(pitchValue);
            // The envelope is removed before pitch shifting, then restored separately.
            // Character supplies only a conservative independent 0..+2 st offset.
            transform.setFormant(formantValue + 2.0f * characterValue);

            for (int sample = 0; sample < count; ++sample)
            {
                const float inGain = inputGain.next(controlPole);
                const float hpAmount = highPassAmount.next(controlPole);
                const float compAmount = compressorAmount.next(controlPole);
                outputGains[sample] = outputGain.next(controlPole);
                mixAmounts[sample] = mix.next(controlPole);
                transformAmounts[sample] = transformAmount.next(controlPole);
                bypassAmounts[sample] = bypassAmount.next(controlPole);
                float peak = 0.0f;

                for (int channel = 0; channel < channels; ++channel)
                {
                    // Bound even invalid host input before it can poison DSP state.
                    const float raw = channel < activeChannels && audio[channel] != nullptr
                                        ? bounded(audio[channel][offset + sample], -8.0f, 8.0f, 0.0f) : 0.0f;
                    const float gained = raw * inGain;
                    const float meteredInput = gained + bypassAmounts[sample] * (raw - gained);
                    meters.input[static_cast<size_t>(channel)] = std::max(
                        meters.input[static_cast<size_t>(channel)], std::abs(meteredInput));
                    const float filtered = highPass.process(gained, channel);
                    const float utility = gained + hpAmount * (filtered - gained);
                    input[channel][sample] = utility;
                    peak = std::max(peak, std::abs(utility));
                    delayedRaw[channel][sample] = delaySample(raw, 0, channel);
                    delayedDry[channel][sample] = delaySample(gained, 1, channel);
                }

                const float compressionGain = 1.0f + compAmount * (compressor.gain(peak) - 1.0f);
                minimumCompressionGain = std::min(minimumCompressionGain,
                    compressionGain + bypassAmounts[sample] * (1.0f - compressionGain));
                for (int channel = 0; channel < channels; ++channel)
                {
                    input[channel][sample] *= compressionGain;
                    delayedUtility[channel][sample] = delaySample(input[channel][sample], 2, channel);
                }
                if (++delayPosition >= latency)
                    delayPosition = 0;
            }

            // All state is prepared in advance. Keep the transform warm during bypass.
            transform.process(inputPointers.data(), outputPointers.data(), count);

            for (int channel = 0; channel < std::min(channels, activeChannels); ++channel)
            {
                if (audio[channel] == nullptr)
                    continue;
                for (int sample = 0; sample < count; ++sample)
                {
                    const float transformed = clean(wet[channel][sample]);
                    const float utility = delayedUtility[channel][sample];
                    const float effectiveWet = utility + transformAmounts[sample] * (transformed - utility);
                    const float dry = delayedDry[channel][sample];
                    const float processed = (dry + mixAmounts[sample] * (effectiveWet - dry)) * outputGains[sample];
                    const float output = processed + bypassAmounts[sample] * (delayedRaw[channel][sample] - processed);
                    meters.output[static_cast<size_t>(channel)] = std::max(
                        meters.output[static_cast<size_t>(channel)], std::abs(clean(output)));
                    // Last-resort sample protection, not a lookahead limiter or a
                    // guarantee against acoustic microphone/speaker feedback.
                    audio[channel][offset + sample] = bounded(clean(output), -1.0f, 1.0f, 0.0f);
                }
            }
            offset += count;
        }
        meters.gainReductionDb = -20.0f * std::log10(std::max(minimumCompressionGain, 1.0e-6f));
        meters.hasAudio = true;
        meters.bypassed = parameters.bypassed;
    }
};

VocalEngine::VocalEngine() = default;
VocalEngine::~VocalEngine() = default;

void VocalEngine::prepare(double rate, int maxBlockSize, int channels)
{
    auto prepared = std::make_unique<Impl>();
    prepared->prepare(rate, maxBlockSize, channels);
    impl = std::move(prepared);
}

void VocalEngine::reset() noexcept
{
    if (impl)
        impl->reset();
}

void VocalEngine::process(float* const* audio, int numChannels, int numSamples,
                          const Parameters& parameters) noexcept
{
    if (audio == nullptr || numChannels <= 0 || numSamples <= 0)
        return;
    if (impl)
        impl->process(audio, numChannels, numSamples, sanitise(parameters));

    // Fail silent if not prepared, or if the caller supplies unsupported channels.
    const int firstSilentChannel = impl ? impl->channels : 0;
    for (int channel = firstSilentChannel; channel < numChannels; ++channel)
        if (audio[channel] != nullptr)
            std::fill_n(audio[channel], numSamples, 0.0f);
}

int VocalEngine::latencySamples() const noexcept { return impl ? impl->latency : 0; }
double VocalEngine::sampleRate() const noexcept { return impl ? impl->rate : 0.0; }
MeterReadings VocalEngine::meterReadings() const noexcept { return impl ? impl->meters : MeterReadings {}; }

Parameters VocalEngine::sanitise(Parameters parameters) noexcept
{
    const Parameters defaults;
    parameters.pitchSemitones = bounded(parameters.pitchSemitones, -12.0f, 12.0f, defaults.pitchSemitones);
    parameters.formantSemitones = bounded(parameters.formantSemitones, -12.0f, 12.0f, defaults.formantSemitones);
    parameters.character = bounded(parameters.character, 0.0f, 1.0f, defaults.character);
    parameters.mix = bounded(parameters.mix, 0.0f, 1.0f, defaults.mix);
    parameters.inputGainDb = bounded(parameters.inputGainDb, -24.0f, 24.0f, defaults.inputGainDb);
    parameters.outputGainDb = bounded(parameters.outputGainDb, -48.0f, 12.0f, defaults.outputGainDb);
    return parameters;
}
} // namespace theythem
