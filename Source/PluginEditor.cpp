#include "PluginEditor.h"

namespace theythem
{
LevelMeter::LevelMeter (juce::String name) : title (std::move (name))
{
    setTitle (title + " level meter");
    setDescription ("Stereo sample peaks in dBFS. Click to clear peak hold and clip indication.");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void LevelMeter::update (const std::array<float, 2>& peaks, float elapsed)
{
    clipTime = juce::jmax (0.0f, clipTime - elapsed);
    for (size_t i = 0; i < levels.size(); ++i)
    {
        const float db = juce::Decibels::gainToDecibels (peaks[i], -60.0f);
        levels[i] = juce::jmax (db, levels[i] - 28.0f * elapsed);
        holdTime[i] -= elapsed;
        if (db >= held[i]) { held[i] = db; holdTime[i] = 1.0f; }
        else if (holdTime[i] <= 0.0f) held[i] = juce::jmax (levels[i], held[i] - 14.0f * elapsed);
        if (peaks[i] >= 1.0f) clipTime = 2.0f;
    }
    repaint();
}

void LevelMeter::mouseDown (const juce::MouseEvent&)
{
    held = levels;
    holdTime.fill (0.0f);
    clipTime = 0.0f;
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    const float width = static_cast<float> (getWidth());
    const auto toX = [width] (float db) { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f) * width; };
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.setColour (palette::muted);
    g.drawText (title, 0, 0, getWidth() / 2, 20, juce::Justification::centredLeft);
    const auto peak = juce::jmax (held[0], held[1]);
    g.setColour (clipTime > 0.0f ? palette::red : palette::text);
    g.drawText (clipTime > 0.0f ? "CLIP" : peak <= -60.0f ? "-inf dB" : juce::String (peak, 1) + " dB",
                getWidth() / 2, 0, getWidth() / 2, 20, juce::Justification::centredRight);
    for (size_t i = 0; i < levels.size(); ++i)
    {
        const float y = 27.0f + static_cast<float> (i) * 12.0f;
        g.setColour (palette::background);
        g.fillRoundedRectangle (0.0f, y, width, 7.0f, 2.0f);
        g.setGradientFill (juce::ColourGradient (palette::mint, width * 0.5f, y, palette::amber, width, y, false));
        g.fillRoundedRectangle (0.0f, y, toX (levels[i]), 7.0f, 2.0f);
        if (held[i] > -60.0f)
        {
            g.setColour (held[i] >= 0.0f ? palette::red : palette::text);
            const float marker = juce::jlimit (0.0f, width - 2.0f, toX (held[i]));
            g.fillRect (marker, y - 1.0f, 2.0f, 9.0f);
        }
    }
    g.setFont (juce::FontOptions (10.0f));
    g.setColour (palette::muted);
    for (int db : { -60, -36, -18, 0 })
    {
        const int x = juce::jlimit (0, getWidth() - 25, static_cast<int> (toX (static_cast<float> (db))) - 12);
        g.drawText (juce::String (db), x, 52, 25, 16, juce::Justification::centred);
    }
}

TheyThemAudioProcessorEditor::TheyThemAudioProcessorEditor (TheyThemAudioProcessor& owner)
    : AudioProcessorEditor (owner), processor (owner)
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);
    const std::array<const char*, 6> ids { "pitch", "formant", "character", "mix", "input", "output" };
    const std::array<const char*, 6> names { "Pitch", "Formant", "Character", "Dry / Wet", "Input trim", "Output trim" };
    for (size_t index = 0; index < sliders.size(); ++index)
    {
        auto& slider = sliders[index];
        slider.setSliderStyle (index < 2 ? juce::Slider::RotaryHorizontalVerticalDrag : juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (index < 2 ? juce::Slider::TextBoxBelow : juce::Slider::TextBoxRight,
                               false, index < 2 ? 130 : 85, 28);
        slider.setName (ids[index]);
        slider.setTitle (names[index]);
        slider.setWantsKeyboardFocus (true);
        slider.setScrollWheelEnabled (false);
        slider.setColour (juce::Slider::thumbColourId, index == 1 || index == 2 ? palette::lilac : palette::mint);
        labels[index].setText (names[index], juce::dontSendNotification);
        labels[index].setFont (juce::FontOptions (index < 4 ? 18.0f : 12.0f, index < 4 ? juce::Font::bold : juce::Font::plain));
        labels[index].setColour (juce::Label::textColourId, index < 4 ? palette::text : palette::muted);
        labels[index].setBorderSize (juce::BorderSize<int> (0));
        addAndMakeVisible (labels[index]);
        addAndMakeVisible (slider);
        sliderAttachments[index] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.parameterState(), ids[index], slider);
        const auto* parameter = processor.parameterState().getParameter (ids[index]);
        slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));

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
    sliders[0].setTooltip ("Change musical pitch independently of the vocal envelope. Double-click restores +12 st; type 0 for neutral.");
    sliders[1].setTooltip ("Shift the vocal envelope without changing musical pitch. Double-click restores 0 st.");
    sliders[2].setTooltip ("Adds up to +2 semitones to the formant shift. Double-click restores 65%.");
    sliders[3].setTooltip ("Blend delayed dry input and processed voice. Both paths follow input trim. Double-click restores 100% wet.");
    sliders[4].setTooltip ("Level before processing. The input meter follows this trim. Double-click restores 0 dB.");
    sliders[5].setTooltip ("Level after dry/wet mixing. The output meter detects peaks before the safety clamp. Double-click restores 0 dB.");

    const std::array<const char*, 4> toggleIds { "highPass", "compressor", "transform", "bypass" };
    const std::array<const char*, 4> toggleNames { "Low cut", "Compress", "Transform", "Bypass" };
    const std::array<const char*, 4> toggleTips {
        "Remove low-frequency rumble with an 80 Hz high-pass filter.",
        "Gentle 2:1 compression above -18 dBFS. No makeup gain.",
        "Enable pitch, formant, and character processing. The utility effects still work when this is off.",
        "Return the latency-aligned original input. Input/output trims and effects are bypassed."
    };
    for (size_t index = 0; index < toggles.size(); ++index)
    {
        toggles[index].setName (toggleIds[index]);
        toggles[index].setButtonText (toggleNames[index]);
        toggles[index].setTooltip (toggleTips[index]);
        toggles[index].setColour (juce::ToggleButton::tickColourId, index == 3 ? palette::amber : palette::mint);
        addAndMakeVisible (toggles[index]);
        buttonAttachments[index] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            processor.parameterState(), toggleIds[index], toggles[index]);
    }

    presets.setName ("factoryPresets");
    presets.setTitle ("Factory voice presets");
    presets.setTextWhenNothingSelected ("Custom settings");
    presets.setTooltip ("Factory presets keep your input/output trims and bypass setting.");
    for (size_t i = 0; i < factoryPresets.size(); ++i)
        presets.addItem (factoryPresets[i].name, static_cast<int> (i) + 1);
    presets.onChange = [this]
    {
        if (presets.getSelectedId() > 0)
        {
            processor.loadFactoryPreset (presets.getSelectedId() - 1);
            messageUntil = 0.0;
            displayedPreset = -2;
            timerCallback();
        }
    };
    previous.setTitle ("Previous factory preset");
    next.setTitle ("Next factory preset");
    previous.setTooltip ("Previous factory preset; keeps trim levels and bypass.");
    next.setTooltip ("Next factory preset; keeps trim levels and bypass.");
    previous.onClick = [this] { selectRelativePreset (-1); };
    next.onClick = [this] { selectRelativePreset (1); };
    load.setTooltip ("Load a .ttvoice file. Restores all controls, including trims and bypass.");
    save.setTooltip ("Save all current controls as a portable .ttvoice preset.");
    load.onClick = [this] { choosePresetFile (false); };
    save.onClick = [this] { choosePresetFile (true); };
    for (auto* component : std::array<juce::Component*, 11> {
             &presets, &previous, &next, &load, &save, &presetDescription,
             &status, &latency, &compression, &inputMeter, &outputMeter })
        addAndMakeVisible (*component);
    for (auto* label : { &presetDescription, &status, &latency, &compression })
    {
        label->setColour (juce::Label::textColourId, palette::muted);
        label->setFont (juce::FontOptions (12.0f));
        label->setBorderSize (juce::BorderSize<int> (0));
    }
    presetDescription.setMinimumHorizontalScale (0.85f);
    latency.setTooltip ("Processing delay only. Device buffers and audio routing add more latency.");
    compression.setTooltip ("Peak gain reduction from the compressor. Releases smoothly on this display.");
    setResizable (true, true);
    setResizeLimits (800, 620, 1440, 1000);
    setSize (960, 680);
    (void) processor.takeMeterReadings();
    timerCallback();
    startTimerHz (30);
}

TheyThemAudioProcessorEditor::~TheyThemAudioProcessorEditor()
{
    stopTimer();
    fileChooser.reset();
    setLookAndFeel (nullptr);
}

void TheyThemAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (palette::background);
    g.setColour (palette::text);
    g.setFont (juce::FontOptions (34.0f, juce::Font::bold));
    g.drawText ("they-them", 24, 15, 300, 44, juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (12.0f));
    g.setColour (palette::muted);
    g.drawText ("YOUR VOICE. ROOM TO EXPLORE.", 26, 59, 360, 20, juce::Justification::centredLeft);
    const auto panel = [&g] (juce::Rectangle<int> area)
    {
        g.setColour (palette::panel);
        g.fillRoundedRectangle (area.toFloat(), 12.0f);
        g.setColour (palette::border);
        g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 12.0f, 1.0f);
    };
    for (const auto& area : voicePanels) panel (area);
    panel (monitorPanel);
    const std::array<const char*, 4> descriptions {
        "Lower register  /  Higher register", "Darker envelope  /  Brighter envelope",
        "A gentle lift in vocal brightness.", "Original voice  /  Transformed voice"
    };
    for (size_t i = 0; i < voicePanels.size(); ++i)
    {
        const auto area = voicePanels[i];
        g.setColour (i == 1 || i == 2 ? palette::lilac : palette::mint);
        g.fillRoundedRectangle (static_cast<float> (area.getRight() - 39), static_cast<float> (area.getY() + 25), 19.0f, 3.0f, 1.5f);
        g.setColour (palette::muted);
        g.setFont (juce::FontOptions (11.5f));
        g.drawFittedText (descriptions[i], area.withTrimmedTop (area.getHeight() - 38).reduced (16, 0),
                          juce::Justification::centred, 2);
    }
    g.setColour (palette::muted);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("LEVELS & DYNAMICS", monitorPanel.reduced (18).removeFromTop (20), juce::Justification::centredLeft);
    const auto dividerY = static_cast<float> (getHeight() - 49);
    g.setColour (palette::border);
    g.drawLine (24.0f, dividerY, static_cast<float> (getWidth() - 24), dividerY);
    g.setColour (palette::muted);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("v0.2.0", getWidth() - 85, getHeight() - 39, 60, 25, juce::Justification::centredRight);
    g.drawText ("Double-click to reset  /  Click values to type", 315, getHeight() - 39,
                getWidth() - 420, 25, juce::Justification::centredRight);
}

void TheyThemAudioProcessorEditor::resized()
{
    toggles[2].setBounds (getWidth() - 284, 28, 128, 34);
    toggles[3].setBounds (getWidth() - 144, 28, 120, 34);
    previous.setBounds (24, 96, 36, 36);
    next.setBounds (66, 96, 36, 36);
    presets.setBounds (114, 96, getWidth() - 310, 36);
    load.setBounds (getWidth() - 184, 96, 74, 36);
    save.setBounds (getWidth() - 102, 96, 78, 36);
    presetDescription.setBounds (26, 138, getWidth() - 52, 26);
    auto body = getLocalBounds().withTrimmedTop (180).withTrimmedBottom (64).reduced (24, 0);
    monitorPanel = body.removeFromRight (juce::jmax (228, body.getWidth() / 4));
    body.removeFromRight (12);
    auto top = body.removeFromTop (static_cast<int> (static_cast<float> (body.getHeight()) * 0.61f));
    body.removeFromTop (12);
    voicePanels[0] = top.removeFromLeft ((top.getWidth() - 12) / 2);
    top.removeFromLeft (12);
    voicePanels[1] = top;
    voicePanels[2] = body.removeFromLeft ((body.getWidth() - 12) / 2);
    body.removeFromLeft (12);
    voicePanels[3] = body;
    for (size_t i = 0; i < 4; ++i)
    {
        const auto area = voicePanels[i];
        labels[i].setBounds (area.getX() + 18, area.getY() + 16, area.getWidth() - 68, 24);
        sliders[i].setBounds (i < 2 ? area.reduced (16, 0).withTrimmedTop (45).withTrimmedBottom (38)
                                    : area.reduced (18, 0).withTrimmedTop (48).withTrimmedBottom (40));
    }
    const int mx = monitorPanel.getX() + 18, mw = monitorPanel.getWidth() - 36;
    const int my = monitorPanel.getY();
    // Keep both meter/trim groups above the utility controls at the minimum size.
    const int groupHeight = (monitorPanel.getHeight() - 132) / 2;
    for (int group = 0; group < 2; ++group)
    {
        const int y = my + 42 + group * groupHeight;
        (group == 0 ? inputMeter : outputMeter).setBounds (mx, y, mw, 68);
        labels[static_cast<size_t> (group + 4)].setBounds (mx, y + 68, mw, 18);
        sliders[static_cast<size_t> (group + 4)].setBounds (mx, y + 86, mw, 28);
    }
    const int utilityY = monitorPanel.getBottom() - 77;
    toggles[0].setBounds (mx, utilityY, (mw - 8) / 2, 30);
    toggles[1].setBounds (mx + (mw + 8) / 2, utilityY, (mw - 8) / 2, 30);
    compression.setBounds (mx, utilityY + 38, mw, 22);
    status.setBounds (26, getHeight() - 39, 138, 25);
    latency.setBounds (164, getHeight() - 39, 150, 25);
}

void TheyThemAudioProcessorEditor::selectRelativePreset (int direction)
{
    const int count = static_cast<int> (factoryPresets.size());
    const int current = processor.matchingFactoryPreset();
    const int selected = current < 0 ? (direction < 0 ? count - 1 : 0) : (current + direction + count) % count;
    processor.loadFactoryPreset (selected);
    messageUntil = 0.0;
    displayedPreset = -2;
    timerCallback();
}

void TheyThemAudioProcessorEditor::choosePresetFile (bool saving)
{
    const auto suggested = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                               .getChildFile ("My Voice.ttvoice");
    fileChooser = std::make_unique<juce::FileChooser> (saving ? "Save voice preset" : "Load voice preset", suggested, "*.ttvoice");
    load.setEnabled (false);
    save.setEnabled (false);
    const int chooserFlags = saving ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting
                             : juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser->launchAsync (chooserFlags, [safe = juce::Component::SafePointer<TheyThemAudioProcessorEditor> (this), saving]
                             (const juce::FileChooser& chooser)
    {
        if (safe == nullptr) return;
        safe->load.setEnabled (true);
        safe->save.setEnabled (true);
        const auto file = chooser.getResult();
        if (file == juce::File()) return;
        const auto result = saving ? safe->processor.savePresetToFile (file) : safe->processor.loadPresetFromFile (file);
        safe->showPresetResult (result, (saving ? "Saved: " : "Loaded: ") + file.getFileName());
    });
}

void TheyThemAudioProcessorEditor::showPresetResult (const juce::Result& result, const juce::String& success)
{
    presetDescription.setText (result.wasOk() ? success : result.getErrorMessage(), juce::dontSendNotification);
    presetDescription.setColour (juce::Label::textColourId, result.wasOk() ? palette::mint : palette::red);
    messageUntil = juce::Time::getMillisecondCounterHiRes() + 7000.0;
    displayedPreset = -2;
    timerCallback();
}

void TheyThemAudioProcessorEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float elapsed = lastTick > 0.0 ? static_cast<float> (juce::jlimit (0.0, 2.0, (now - lastTick) * 0.001)) : 1.0f / 30.0f;
    lastTick = now;
    const auto reading = processor.takeMeterReadings();
    inputMeter.update (reading.input, elapsed);
    outputMeter.update (reading.output, elapsed);
    reductionDb = juce::jmax (reading.gainReductionDb, reductionDb - elapsed * 18.0f);
    compression.setText ("Compression   " + juce::String (reductionDb, 1) + " dB reduction", juce::dontSendNotification);
    if (reading.hasAudio) { audioUntil = now + 500.0; hostBypassed = reading.bypassed; }
    const bool bypassed = processor.currentParameters().bypassed || (now < audioUntil && hostBypassed);
    status.setText (bypassed ? "BYPASSED" : now < audioUntil ? "PROCESSING" : "AUDIO IDLE", juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, bypassed ? palette::amber : now < audioUntil ? palette::mint : palette::muted);
    const auto delay = processor.latencyMilliseconds();
    latency.setText (delay > 0.0 ? juce::String (delay, 2) + " ms DSP latency" : "Awaiting audio setup", juce::dontSendNotification);
    const int current = processor.matchingFactoryPreset();
    if (displayedPreset != current)
        presets.setSelectedId (current + 1, juce::dontSendNotification);
    if (now >= messageUntil && (displayedPreset != current || messageUntil > 0.0))
    {
        presetDescription.setColour (juce::Label::textColourId, palette::muted);
        presetDescription.setText (current >= 0 ? factoryPresets[static_cast<size_t> (current)].description
                                                : "Custom settings. Save this voice to keep it close.", juce::dontSendNotification);
        messageUntil = 0.0;
    }
    displayedPreset = current;
}
} // namespace theythem
