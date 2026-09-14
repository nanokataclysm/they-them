#include "PluginProcessor.h"

#include <cmath>
#include <iostream>
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
