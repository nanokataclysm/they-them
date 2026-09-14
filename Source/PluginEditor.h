#pragma once

#include "PluginProcessor.h"

#include <array>
#include <memory>

namespace theythem
{

class TheyThemAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    explicit TheyThemAudioProcessorEditor (TheyThemAudioProcessor&);
    ~TheyThemAudioProcessorEditor() override = default;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    TheyThemAudioProcessor& processor;
    std::array<juce::Slider, 6> sliders;
    std::array<juce::Label, 6> labels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 6> sliderAttachments;
    std::array<juce::ToggleButton, 4> toggles;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, 4> buttonAttachments;
    juce::Label latency;
    juce::Label monitoring;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TheyThemAudioProcessorEditor)
};

} // namespace theythem
