#pragma once

#include <memory>
#include "Metering.h"

namespace theythem
{
struct Parameters
{
    float pitchSemitones = 12.0f;
    float formantSemitones = 0.0f;
    float character = 0.65f;
    float mix = 1.0f;
    float inputGainDb = 0.0f;
    float outputGainDb = 0.0f;
    bool highPassEnabled = true;
    bool compressorEnabled = true;
    bool transformEnabled = true;
    bool bypassed = false;
};

// Prepare/reset on the audio lifecycle thread, never concurrently with process().
// process() accepts mono/stereo buffers of arbitrary length and allocates nothing.
class VocalEngine
{
public:
    VocalEngine();
    ~VocalEngine();
    VocalEngine(const VocalEngine&) = delete;
    VocalEngine& operator=(const VocalEngine&) = delete;

    void prepare(double sampleRate, int maxBlockSize, int channels);
    void reset() noexcept;
    void process(float* const* audio, int numChannels, int numSamples,
                 const Parameters&) noexcept;
    int latencySamples() const noexcept;
    double sampleRate() const noexcept;
    // Audio/lifecycle thread only; the processor publishes this through MeterBridge.
    MeterReadings meterReadings() const noexcept;
    static Parameters sanitise(Parameters) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace theythem
