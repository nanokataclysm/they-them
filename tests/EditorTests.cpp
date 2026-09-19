#include "PluginEditor.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
void check (bool condition, const char* message)
{
    if (! condition) throw std::runtime_error (message);
}

template <typename T> T& control (juce::Component& parent, const juce::String& name)
{
    for (auto* child : parent.getChildren())
        if (auto* match = dynamic_cast<T*> (child); match != nullptr && match->getName() == name)
            return *match;
    throw std::runtime_error ("Editor control missing: " + name.toStdString());
}

void tick()
{
    std::this_thread::sleep_for (std::chrono::milliseconds (40));
    juce::Timer::callPendingTimersSynchronously();
}

void snapshot (juce::AudioProcessorEditor& editor, const juce::File& folder, const char* name)
{
    const auto image = editor.createComponentSnapshot (editor.getLocalBounds());
    juce::MemoryOutputStream encoded;
    check (juce::PNGImageFormat().writeImageToStream (image, encoded), "Editor PNG encoding failed");
    check (folder.getChildFile (name).replaceWithData (encoded.getData(), encoded.getDataSize()), "Editor snapshot write failed");
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    try
    {
        const auto folder = argc > 1 ? juce::File (juce::String::fromUTF8 (argv[1]))
                                    : juce::File::getCurrentWorkingDirectory().getChildFile ("qa");
        check (folder.createDirectory().wasOk(), "Cannot create editor evidence directory");
        theythem::TheyThemAudioProcessor processor;
        processor.prepareToPlay (48000, 128);
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
        editor->setVisible (true);
        const std::array<juce::Point<int>, 3> sizes {{ { 800, 620 }, { 960, 680 }, { 1440, 1000 } }};
        for (const auto& size : sizes)
        {
            editor->setSize (size.x, size.y);
            for (auto* child : editor->getChildren())
            {
                if (! child->isVisible()) continue;
                check (! child->getBounds().isEmpty() && editor->getLocalBounds().contains (child->getBounds()),
                       "Visible control escaped editor bounds during resize");
                // These direct children are the actual interactive and labelled
                // controls, not decorative panels. Catch clipped/overlapping UI.
                if (dynamic_cast<juce::ResizableCornerComponent*> (child) != nullptr) continue;
                for (auto* other : editor->getChildren())
                    if (other != child && other->isVisible()
                        && dynamic_cast<juce::ResizableCornerComponent*> (other) == nullptr)
                        check (! child->getBounds().intersects (other->getBounds()), "Editor controls overlap after resize");
            }
            snapshot (*editor, folder, ("editor-" + juce::String (size.x) + ".png").toRawUTF8());
        }
        editor->setSize (960, 680);
        auto& pitch = control<juce::Slider> (*editor, "pitch");
        pitch.setValue (4.5, juce::sendNotificationSync);
        check (std::abs (processor.currentParameters().pitchSemitones - 4.5f) < 0.0001f,
               "Slider gesture did not update processor");
        auto& presets = control<juce::ComboBox> (*editor, "factoryPresets");
        tick();
        check (presets.getSelectedId() == 0, "Edited settings did not display Custom");
        presets.setSelectedId (5, juce::sendNotificationSync);
        check (processor.matchingFactoryPreset() == 4 && std::abs (pitch.getValue() + 4.0) < 0.0001,
               "Preset selection did not update audio parameters and attached controls");
        auto& bypass = control<juce::ToggleButton> (*editor, "bypass");
        bypass.setToggleState (true, juce::sendNotificationSync);
        check (processor.currentParameters().bypassed, "Bypass button is not attached");
        bypass.setToggleState (false, juce::sendNotificationSync);
        processor.loadFactoryPreset (0);

        juce::AudioBuffer<float> buffer (2, 128);
        juce::MidiBuffer midi;
        for (int block = 0; block < 40; ++block)
        {
            for (int sample = 0; sample < 128; ++sample)
            {
                const float value = 0.28f * std::sin (juce::MathConstants<float>::twoPi * 180.0f
                                                     * static_cast<float> (block * 128 + sample) / 48000.0f);
                buffer.setSample (0, sample, value);
                buffer.setSample (1, sample, value * 0.8f);
            }
            processor.processBlock (buffer, midi);
        }
        tick();
        snapshot (*editor, folder, "editor-processing.png");
        // Host automation must propagate through a freshly reopened editor too.
        editor.reset();
        auto* parameter = processor.parameterState().getParameter ("formant");
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (-2.5f));
        for (int i = 0; i < 8; ++i)
        {
            editor.reset (processor.createEditor());
            check (std::abs (control<juce::Slider> (*editor, "formant").getValue() + 2.5) < 0.0001,
                   "Reopened editor lost parameter state");
            editor.reset();
        }
        processor.releaseResources();
        std::cout << "Editor resize/overlap, attachments, presets, bypass, metered rendering, and 8 reopen cycles passed.\n";
        std::cout << "Actual JUCE editor snapshots: " << folder.getFullPathName() << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
