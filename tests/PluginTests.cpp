#include "PluginProcessor.h"
#include "AllocationProbe.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
using Processor = theythem::TheyThemAudioProcessor;

void check (bool condition, const char* message)
{
    if (! condition)
        throw std::runtime_error (message);
}

bool near (double a, double b, double tolerance = 1.0e-5)
{
    return std::abs (a - b) <= tolerance;
}

void set (Processor& processor, const char* id, float value)
{
    auto* parameter = processor.parameterState().getParameter (id);
    check (parameter != nullptr, "Missing parameter");
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

void parametersAndState()
{
    Processor processor;
    struct Expected { const char* id; float minimum, maximum, initial; };
    const Expected expected[] {
        { "pitch", -12, 12, 12 }, { "formant", -12, 12, 0 },
        { "character", 0, 1, 0.65f }, { "mix", 0, 1, 1 },
        { "input", -24, 24, 0 }, { "output", -48, 12, 0 },
        { "highPass", 0, 1, 1 }, { "compressor", 0, 1, 1 },
        { "transform", 0, 1, 1 }, { "bypass", 0, 1, 0 }
    };
    for (const auto& value : expected)
    {
        auto* parameter = processor.parameterState().getParameter (value.id);
        check (parameter != nullptr, "Parameter missing from host");
        const auto& range = parameter->getNormalisableRange();
        check (near (range.start, value.minimum) && near (range.end, value.maximum), "Incorrect parameter range");
        check (near (parameter->convertFrom0to1 (parameter->getDefaultValue()), value.initial), "Incorrect parameter default");
    }
    check (std::fpclassify (processor.parameterState().getParameter ("pitch")->getNormalisableRange().interval) == FP_ZERO,
           "Pitch must be continuous");
    check (! processor.acceptsMidi() && ! processor.producesMidi() && ! processor.isMidiEffect(), "Unexpected MIDI feature");
    check (processor.getBypassParameter() == processor.parameterState().getParameter ("bypass"), "Host bypass parameter not exposed");

    set (processor, "pitch", 4.25f);
    set (processor, "formant", -3.75f);
    set (processor, "character", 0.37f);
    juce::MemoryBlock saved;
    processor.getStateInformation (saved);
    Processor restored;
    restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    auto state = restored.currentParameters();
    check (near (state.pitchSemitones, 4.25) && near (state.formantSemitones, -3.75)
           && near (state.character, 0.37), "Parameter state did not round-trip");

    restored.setStateInformation ("invalid", 7);
    check (near (restored.currentParameters().pitchSemitones, 4.25), "Malformed state changed parameters");
    auto incoming = restored.parameterState().copyState();
    incoming.getChildWithProperty ("id", "pitch").setProperty ("value", "nan", nullptr);
    incoming.getChildWithProperty ("id", "formant").setProperty ("value", "9999", nullptr);
    incoming.getChildWithProperty ("id", "output").setProperty ("value", "garbage", nullptr);
    auto xml = incoming.createXml();
    juce::MemoryBlock corrupt;
    juce::AudioProcessor::copyXmlToBinary (*xml, corrupt);
    restored.setStateInformation (corrupt.getData(), static_cast<int> (corrupt.getSize()));
    state = restored.currentParameters();
    check (near (state.pitchSemitones, 4.25) && near (state.formantSemitones, 12)
           && near (state.outputGainDb, 0), "Invalid state was not rejected/clamped");
}

void presetsAndFiles()
{
    Processor processor;
    check (processor.matchingFactoryPreset() == 0, "Original default voice lost its factory preset");
    set (processor, "input", -7.0f);
    set (processor, "output", -13.0f);
    set (processor, "bypass", 1.0f);
    struct Listener final : juce::AudioProcessorParameter::Listener
    {
        int changes = 0, gestures = 0;
        void parameterValueChanged (int, float) override { ++changes; }
        void parameterGestureChanged (int, bool) override { ++gestures; }
    } listener;
    auto* pitchParameter = processor.parameterState().getParameter ("pitch");
    pitchParameter->addListener (&listener);
    for (size_t i = 0; i < theythem::factoryPresets.size(); ++i)
    {
        processor.loadFactoryPreset (static_cast<int> (i));
        const auto p = processor.currentParameters();
        check (processor.matchingFactoryPreset() == static_cast<int> (i), "Factory preset was not recognised");
        check (near (p.inputGainDb, -7) && near (p.outputGainDb, -13) && p.bypassed,
               "Factory preset changed monitoring trims or bypass");
    }
    check (listener.changes >= 6 && listener.gestures == 16, "Factory presets did not notify host parameter listeners");
    pitchParameter->removeListener (&listener);
    set (processor, "pitch", 2.25f);
    check (processor.matchingFactoryPreset() == -1, "Edited voice was labelled as an unchanged factory preset");
    processor.loadFactoryPreset (-1);
    processor.loadFactoryPreset (999);
    check (near (processor.currentParameters().pitchSemitones, 2.25), "Invalid factory index changed settings");

    juce::TemporaryFile temporary (".ttvoice");
    const auto file = temporary.getFile();
    check (processor.savePresetToFile (file).wasOk(), "Preset save failed");
    const auto validText = file.loadFileAsString();
    const auto before = processor.currentParameters();
    Processor restored;
    check (restored.loadPresetFromFile (file).wasOk(), "Preset load failed");
    for (auto* parameter : processor.getParameters())
    {
        auto* identified = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter);
        check (identified != nullptr, "Unexpected anonymous parameter");
        const auto id = identified->paramID;
        check (near (parameter->getValue(), restored.parameterState().getParameter (id)->getValue()),
               "Preset did not round-trip every parameter");
    }
    // A user preset includes trims and bypass, unlike factory browsing.
    check (near (restored.currentParameters().inputGainDb, before.inputGainDb)
           && restored.currentParameters().bypassed, "File preset omitted session controls");
    juce::MemoryBlock restoredState;
    restored.getStateInformation (restoredState);
    const auto rejected = [&] (const juce::String& text)
    {
        check (file.replaceWithText (text), "Could not write invalid preset fixture");
        check (restored.loadPresetFromFile (file).failed(), "Invalid preset accepted");
        juce::MemoryBlock after;
        restored.getStateInformation (after);
        check (after == restoredState, "Rejected preset partially modified parameters");
    };
    rejected ("not a preset");
    rejected (validText.substring (0, validText.length() / 2));
    rejected (validText.replace ("presetVersion=\"1\"", "presetVersion=\"999\""));
    rejected (validText.replace ("presetVersion=\"1\"", "presetVersion=\"1garbage\""));
    rejected (validText.replace ("TheyThemParameters", "OtherPlugin"));
    for (const auto* invalid : { "nan", "inf", "garbage", "99", "" })
    {
        auto tree = juce::ValueTree::fromXml (*juce::parseXML (validText));
        tree.getChildWithProperty ("id", "pitch").setProperty ("value", invalid, nullptr);
        rejected (tree.createXml()->toString());
    }
    auto incomplete = juce::ValueTree::fromXml (*juce::parseXML (validText));
    incomplete.removeChild (incomplete.getChildWithProperty ("id", "output"), nullptr);
    rejected (incomplete.createXml()->toString());
    auto duplicate = juce::ValueTree::fromXml (*juce::parseXML (validText));
    duplicate.getChildWithProperty ("id", "output").setProperty ("id", "input", nullptr);
    rejected (duplicate.createXml()->toString());
    auto fractionalBool = juce::ValueTree::fromXml (*juce::parseXML (validText));
    fractionalBool.getChildWithProperty ("id", "bypass").setProperty ("value", 0.25f, nullptr);
    rejected (fractionalBool.createXml()->toString());
    rejected (juce::String::repeatedString ("x", 1024 * 1024 + 1));
    check (restored.loadPresetFromFile (file.getSiblingFile ("missing-they-them-preset.ttvoice")).failed(),
           "Missing preset file reported success");
    // Saving over an existing file is also a complete round-trip.
    check (processor.savePresetToFile (file).wasOk() && restored.loadPresetFromFile (file).wasOk(),
           "Replacing an existing preset failed");
    const auto savedContents = file.loadFileAsString();
    check (processor.savePresetToFile (file.getChildFile ("unwritable.ttvoice")).failed(),
           "Saving to an invalid destination reported success");
    check (file.loadFileAsString() == savedContents, "Failed save damaged the existing preset");
    std::cout << "Factory presets, host notifications, file round-trip, and invalid-file rejection passed.\n";
}

void meterContract()
{
    theythem::MeterBridge bridge;
    theythem::MeterReadings transient;
    transient.input = { 0.8f, 0.2f };
    transient.output = { 1.3f, 0.1f };
    transient.gainReductionDb = 6.0f;
    bridge.push (transient);
    bridge.push ({});
    const auto peak = bridge.take();
    check (near (peak.input[0], 0.8) && near (peak.output[0], 1.3) && near (peak.gainReductionDb, 6),
           "Short peaks were lost between meter refreshes");
    check (peak.hasAudio && ! bridge.take().hasAudio && near (bridge.take().output[0], 0),
           "Meter reads did not drain stale peaks/activity");

    Processor processor;
    set (processor, "input", 6.0206f);
    set (processor, "transform", 0);
    set (processor, "highPass", 0);
    set (processor, "compressor", 0);
    processor.prepareToPlay (48000, 128);
    juce::AudioBuffer<float> buffer (2, 128);
    juce::MidiBuffer midi;
    for (int block = 0; block < 20; ++block)
    {
        for (int sample = 0; sample < 128; ++sample)
        {
            buffer.setSample (0, sample, 0.6f);
            buffer.setSample (1, sample, 0.25f);
        }
        {
            allocationProbe::Scope scope;
            processor.processBlock (buffer, midi);
        }
        check (allocationProbe::allocations == 0 && allocationProbe::deallocations == 0,
               "Processor metering allocated/deallocated on the audio thread");
    }
    const auto reading = processor.takeMeterReadings();
    check (near (reading.input[0], 1.2) && near (reading.input[1], 0.5), "Input meters did not follow trim/stereo routing");
    check (near (reading.output[0], 1.2) && near (reading.output[1], 0.5), "Meters missed pre-clamp output overload");
    check (near (buffer.getMagnitude (0, 0, 128), 1) && near (reading.gainReductionDb, 0),
           "Metering changed the sample clamp or disabled compression");
    processor.reset();
    check (! processor.takeMeterReadings().hasAudio, "Reset retained old meter data");
    buffer.clear();
    buffer.setSample (0, 0, std::numeric_limits<float>::quiet_NaN());
    buffer.setSample (1, 0, std::numeric_limits<float>::infinity());
    processor.processBlock (buffer, midi);
    const auto invalid = processor.takeMeterReadings();
    check (near (invalid.input[0], 0) && near (invalid.input[1], 0)
           && std::isfinite (invalid.output[0]), "Invalid input poisoned meter data");
    set (processor, "compressor", 1);
    processor.reset();
    for (int block = 0; block < 80; ++block)
    {
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < 128; ++sample)
                buffer.setSample (channel, sample, 0.4f);
        processor.processBlock (buffer, midi);
    }
    check (processor.takeMeterReadings().gainReductionDb > 7.0f, "Compression meter failed to show attenuation");
    processor.reset();
    processor.processBlockBypassed (buffer, midi);
    const auto bypass = processor.takeMeterReadings();
    check (bypass.bypassed && near (bypass.gainReductionDb, 0), "Meter/status data ignored host bypass");
    std::cout << "Pre-clamp stereo meters, peak hold, compression, bypass, reset, and callback allocation checks passed.\n";
}

void audioContract (double rate, int block)
{
    Processor processor;
    auto layout = processor.getBusesLayout();
    layout.inputBuses.set (0, juce::AudioChannelSet::mono());
    layout.outputBuses.set (0, juce::AudioChannelSet::stereo());
    check (processor.setBusesLayout (layout), "Mono input to stereo output layout rejected");
    auto unsupported = layout;
    unsupported.outputBuses.set (0, juce::AudioChannelSet::quadraphonic());
    check (! processor.isBusesLayoutSupported (unsupported), "Unsupported output layout accepted");

    processor.prepareToPlay (rate, block);
    check (processor.getLatencySamples() == 768, "Host latency disagrees with configured DSP");
    check (near (processor.latencyMilliseconds(), 768000.0 / rate), "UI latency units incorrect");
    set (processor, "input", 24);
    set (processor, "output", 12);

    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    int peakIndex = -1;
    float peak = 0;
    // Host bypass must preserve unity level and reported delay, including mono fanout.
    for (int position = 0; position < 1024; position += block)
    {
        buffer.clear();
        if (position == 0)
            buffer.setSample (0, 0, 0.25f);
        processor.processBlockBypassed (buffer, midi);
        for (int sample = 0; sample < block; ++sample)
        {
            const float left = buffer.getSample (0, sample);
            check (near (left, buffer.getSample (1, sample)), "Mono input was not duplicated correctly");
            check (std::isfinite (left), "Host bypass returned nonfinite output");
            if (std::abs (left) > peak)
            {
                peak = std::abs (left);
                peakIndex = position + sample;
            }
        }
    }
    check (peakIndex == 768 && near (peak, 0.25), "Host bypass latency/gain mismatch");

    processor.reset();
    set (processor, "input", 0);
    set (processor, "output", 0);
    for (int round = 0; round < 20; ++round)
    {
        buffer.clear();
        processor.processBlock (buffer, midi);
        check (std::fpclassify (buffer.getMagnitude (0, block)) == FP_ZERO, "Reset/silence produced audio");
    }
    juce::AudioBuffer<float> empty (2, 0);
    processor.processBlock (empty, midi);
    juce::AudioBuffer<float> noChannels (0, block);
    processor.processBlock (noChannels, midi);
    processor.releaseResources();
    std::cout << "Plugin host contract: " << rate << " Hz / " << block
              << " samples; latency=" << processor.getLatencySamples() << " samples\n";
}

void transformedRouting (double rate)
{
    constexpr int block = 128;
    Processor fanout, mono, stereo;
    const auto monoLayout = [] (Processor& processor, const juce::AudioChannelSet& output)
    {
        auto layout = processor.getBusesLayout();
        layout.inputBuses.set (0, juce::AudioChannelSet::mono());
        layout.outputBuses.set (0, output);
        check (processor.setBusesLayout (layout), "Mono transformation layout rejected");
    };
    monoLayout (fanout, juce::AudioChannelSet::stereo());
    monoLayout (mono, juce::AudioChannelSet::mono());
    for (auto* processor : { &fanout, &mono, &stereo })
    {
        set (*processor, "pitch", 12);
        set (*processor, "formant", 3);
        set (*processor, "character", 0);
        set (*processor, "highPass", 0);
        set (*processor, "compressor", 0);
        processor->prepareToPlay (rate, block);
    }

    juce::AudioBuffer<float> fanoutBuffer (2, block), monoBuffer (1, block), stereoBuffer (2, block);
    juce::MidiBuffer midi;
    double transformedEnergy = 0;
    for (int position = 0; position < 8192; position += block)
    {
        for (int sample = 0; sample < block; ++sample)
        {
            const float voice = static_cast<float> (0.05 * std::sin (
                juce::MathConstants<double>::twoPi * 180.0 * (position + sample) / rate));
            fanoutBuffer.setSample (0, sample, voice);
            fanoutBuffer.setSample (1, sample, 0.8f); // Output-only memory must not enter the DSP.
            monoBuffer.setSample (0, sample, voice);
            stereoBuffer.setSample (0, sample, 0);
            stereoBuffer.setSample (1, sample, voice);
        }
        fanout.processBlock (fanoutBuffer, midi);
        mono.processBlock (monoBuffer, midi);
        stereo.processBlock (stereoBuffer, midi);
        for (int sample = 0; sample < block; ++sample)
        {
            const float expected = monoBuffer.getSample (0, sample);
            check (std::isfinite (expected), "Transformed mono returned nonfinite output");
            check (near (fanoutBuffer.getSample (0, sample), expected, 1.0e-6)
                   && near (fanoutBuffer.getSample (1, sample), expected, 1.0e-6),
                   "Mono-to-stereo transformation differs from one mono engine");
            check (std::fpclassify (stereoBuffer.getSample (0, sample)) == FP_ZERO,
                   "Stereo transformation leaked right input into silent left channel");
            check (near (stereoBuffer.getSample (1, sample), expected, 1.0e-6),
                   "Stereo transformation did not independently process the right input");
            transformedEnergy += static_cast<double> (expected) * expected;
        }
    }
    check (transformedEnergy > 0.001, "Transformed routing test produced no meaningful output");
    std::cout << "Plugin transformed mono fanout/stereo independence: " << rate << " Hz\n";
}
} // namespace

int main()
{
    try
    {
        parametersAndState();
        presetsAndFiles();
        meterContract();
        for (double rate : { 44100.0, 48000.0 })
        {
            for (int block : { 64, 128, 256 })
                audioContract (rate, block);
            transformedRouting (rate);
        }
        std::cout << "Plugin parameter/state/bus/bypass tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
