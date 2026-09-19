#pragma once

#include "PluginProcessor.h"
#include "PluginLookAndFeel.h"

#include <array>
#include <memory>

namespace theythem
{
class LevelMeter final : public juce::Component
{
public:
    explicit LevelMeter (juce::String name);
    void update (const std::array<float, 2>& peaks, float elapsed);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
private:
    juce::String title;
    std::array<float, 2> levels { -60.0f, -60.0f }, held { -60.0f, -60.0f }, holdTime {};
    float clipTime = 0.0f;
};

class TheyThemAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    explicit TheyThemAudioProcessorEditor (TheyThemAudioProcessor&);
    ~TheyThemAudioProcessorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void selectRelativePreset (int direction);
    void choosePresetFile (bool save);
    void showPresetResult (const juce::Result&, const juce::String& success);

    VocalLookAndFeel lookAndFeel;
    TheyThemAudioProcessor& processor;
    std::array<juce::Slider, 6> sliders;
    std::array<juce::Label, 6> labels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 6> sliderAttachments;
    std::array<juce::ToggleButton, 4> toggles;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, 4> buttonAttachments;
    juce::ComboBox presets;
    juce::TextButton previous { "<" }, next { ">" }, load { "Load" }, save { "Save" };
    juce::Label presetDescription, status, latency, compression;
    LevelMeter inputMeter { "INPUT" }, outputMeter { "OUTPUT" };
    juce::TooltipWindow tooltips { this, 650 };
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::array<juce::Rectangle<int>, 4> voicePanels;
    juce::Rectangle<int> monitorPanel;
    double lastTick = 0.0, audioUntil = 0.0, messageUntil = 0.0;
    float reductionDb = 0.0f;
    bool hostBypassed = false;
    int displayedPreset = -2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TheyThemAudioProcessorEditor)
};
} // namespace theythem
