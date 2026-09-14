#include "PluginEditor.h"

namespace theythem
{

TheyThemAudioProcessorEditor::TheyThemAudioProcessorEditor (TheyThemAudioProcessor& owner)
    : AudioProcessorEditor (owner), processor (owner)
{
    const std::array<const char*, 6> ids { "pitch", "formant", "character", "mix", "input", "output" };
    const std::array<const char*, 6> names { "Pitch", "Formant", "Character", "Dry / Wet", "Input", "Output" };
    for (size_t index = 0; index < sliders.size(); ++index)
    {
        auto& slider = sliders[index];
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 96, 28);
        slider.setName (names[index]);
        slider.setTitle (names[index]);
        slider.setColour (juce::Slider::thumbColourId, juce::Colour (0xffacd1c8));
        slider.setColour (juce::Slider::trackColourId, juce::Colour (0xff65867e));
        labels[index].setText (names[index], juce::dontSendNotification);
        labels[index].setColour (juce::Label::textColourId, juce::Colour (0xffe8edeb));
        addAndMakeVisible (labels[index]);
        addAndMakeVisible (slider);
        sliderAttachments[index] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.parameterState(), ids[index], slider);

        // Display units without quantising the continuously adjustable parameter.
        if (index == 2 || index == 3)
        {
            slider.textFromValueFunction = [] (double value) { return juce::String (value * 100.0, 1) + " %"; };
            slider.valueFromTextFunction = [] (const juce::String& text) { return text.getDoubleValue() / 100.0; };
        }
        else
        {
            const auto suffix = index < 2 ? juce::String (" st") : juce::String (" dB");
            slider.textFromValueFunction = [suffix] (double value)
            {
                return (value > 0.0 ? juce::String ("+") : juce::String()) + juce::String (value, 1) + suffix;
            };
            slider.valueFromTextFunction = [] (const juce::String& text) { return text.getDoubleValue(); };
        }
        slider.updateText();
    }

    sliders[0].setTooltip ("Musical pitch, independent of the formant envelope.");
    sliders[1].setTooltip ("Shift the spectral envelope to change vocal character without the same pitch shift.");
    sliders[2].setTooltip ("A conservative timbre macro. This does not identify or clone a voice.");
    sliders[3].setTooltip ("Blend the delayed dry path with the processed voice.");

    const std::array<const char*, 4> toggleIds { "highPass", "compressor", "transform", "bypass" };
    const std::array<const char*, 4> toggleNames { "High-pass", "Compressor", "Transform", "Bypass" };
    for (size_t index = 0; index < toggles.size(); ++index)
    {
        toggles[index].setButtonText (toggleNames[index]);
        addAndMakeVisible (toggles[index]);
        buttonAttachments[index] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            processor.parameterState(), toggleIds[index], toggles[index]);
    }

    latency.setColour (juce::Label::textColourId, juce::Colour (0xffacd1c8));
    addAndMakeVisible (latency);
    monitoring.setColour (juce::Label::textColourId, juce::Colour (0xffaab4b0));
    monitoring.setText (juce::JUCEApplicationBase::isStandaloneApp()
                           ? "Headphones first. Options > Audio/MIDI Settings selects devices and buffer size; then Unmute Input."
                           : "Monitor through headphones. Select and arm a microphone input in your host.",
                       juce::dontSendNotification);
    addAndMakeVisible (monitoring);
    setSize (600, 510);
    timerCallback();
    startTimerHz (2);
}

void TheyThemAudioProcessorEditor::paint (juce::Graphics& graphics)
{
    graphics.fillAll (juce::Colour (0xff202725));
    graphics.setColour (juce::Colour (0xffe8edeb));
    graphics.setFont (juce::FontOptions (27.0f, juce::Font::bold));
    graphics.drawText ("THEY-THEM", 24, 16, 550, 42, juce::Justification::centredLeft);
    graphics.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    graphics.setColour (juce::Colour (0xffacd1c8));
    graphics.drawText ("VOICE", 28, 70, 540, 24, juce::Justification::centredLeft);
    graphics.drawText ("CONTROL", 28, 235, 540, 24, juce::Justification::centredLeft);
}

void TheyThemAudioProcessorEditor::resized()
{
    for (size_t index = 0; index < sliders.size(); ++index)
    {
        const int y = index < 3 ? 98 + static_cast<int> (index) * 42
                                : 263 + static_cast<int> (index - 3) * 42;
        labels[index].setBounds (24, y, 95, 32);
        sliders[index].setBounds (122, y, getWidth() - 150, 32);
    }
    for (size_t index = 0; index < toggles.size(); ++index)
        toggles[index].setBounds (24 + static_cast<int> (index) * 140, 394, 138, 28);
    latency.setBounds (24, 430, getWidth() - 48, 25);
    monitoring.setBounds (24, 460, getWidth() - 48, 42);
}

void TheyThemAudioProcessorEditor::timerCallback()
{
    const auto mode = processor.currentParameters().bypassed ? "BYPASSED" : "LIVE";
    latency.setText (juce::String (mode) + "  |  DSP latency: "
                         + juce::String (processor.latencyMilliseconds(), 2)
                         + " ms (device buffers are additional)",
                     juce::dontSendNotification);
}

} // namespace theythem
