#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "VocalEngine.h"
#include "Presets.h"

#include <atomic>

namespace theythem
{

class TheyThemAudioProcessor final : public juce::AudioProcessor
{
public:
    TheyThemAudioProcessor();
    ~TheyThemAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int maximumBlockSize) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "they-them"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& parameterState() noexcept { return parameters; }
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    Parameters currentParameters() const noexcept;
    double latencyMilliseconds() const noexcept;
    MeterReadings takeMeterReadings() noexcept { return meters.take(); }

    // Preset operations run on the message/setup thread, never in processBlock.
    void loadFactoryPreset (int index);
    int matchingFactoryPreset() const noexcept;
    juce::Result savePresetToFile (const juce::File&);
    juce::Result loadPresetFromFile (const juce::File&);

private:
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&, bool hostBypass) noexcept;

    juce::AudioProcessorValueTreeState parameters;
    VocalEngine engine;
    MeterBridge meters;

    // Resolve these once, never perform string lookups on the audio thread.
    std::atomic<float>* const pitch;
    std::atomic<float>* const formant;
    std::atomic<float>* const character;
    std::atomic<float>* const mix;
    std::atomic<float>* const input;
    std::atomic<float>* const output;
    std::atomic<float>* const highPass;
    std::atomic<float>* const compressor;
    std::atomic<float>* const transform;
    std::atomic<float>* const bypass;
    juce::AudioProcessorParameter* const bypassParameter;

    std::atomic<double> preparedSampleRate { 0.0 };
    std::atomic<int> reportedLatency { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TheyThemAudioProcessor)
};

} // namespace theythem
