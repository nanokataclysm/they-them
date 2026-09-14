#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace theythem
{
namespace
{
juce::AudioParameterFloatAttributes units (const juce::String& label)
{
    return juce::AudioParameterFloatAttributes().withLabel (label)
        .withStringFromValueFunction ([] (float value, int) { return juce::String (value, 1); });
}

juce::AudioParameterFloatAttributes percent()
{
    return juce::AudioParameterFloatAttributes().withLabel ("%")
        .withStringFromValueFunction ([] (float value, int) { return juce::String (value * 100.0f, 1); })
        .withValueFromStringFunction ([] (const juce::String& text) { return text.getFloatValue() / 100.0f; });
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout TheyThemAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto addFloat = [&layout] (const char* id, const char* name, float minimum,
                                     float maximum, float initial,
                                     const juce::AudioParameterFloatAttributes& attributes)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> { minimum, maximum },
            initial, attributes));
    };

    addFloat ("pitch", "Pitch", -12.0f, 12.0f, 12.0f, units ("st"));
    addFloat ("formant", "Formant", -12.0f, 12.0f, 0.0f, units ("st"));
    addFloat ("character", "Character", 0.0f, 1.0f, 0.65f, percent());
    addFloat ("mix", "Dry / Wet", 0.0f, 1.0f, 1.0f, percent());
    addFloat ("input", "Input Gain", -24.0f, 24.0f, 0.0f, units ("dB"));
    addFloat ("output", "Output Gain", -48.0f, 12.0f, 0.0f, units ("dB"));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "highPass", 1 }, "High-pass", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "compressor", 1 }, "Compressor", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "transform", 1 }, "Transformation", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "bypass", 1 }, "Bypass", false));
    return layout;
}

TheyThemAudioProcessor::TheyThemAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "TheyThemParameters", createParameterLayout()),
      pitch (parameters.getRawParameterValue ("pitch")),
      formant (parameters.getRawParameterValue ("formant")),
      character (parameters.getRawParameterValue ("character")),
      mix (parameters.getRawParameterValue ("mix")),
      input (parameters.getRawParameterValue ("input")),
      output (parameters.getRawParameterValue ("output")),
      highPass (parameters.getRawParameterValue ("highPass")),
      compressor (parameters.getRawParameterValue ("compressor")),
      transform (parameters.getRawParameterValue ("transform")),
      bypass (parameters.getRawParameterValue ("bypass")),
      bypassParameter (parameters.getParameter ("bypass"))
{
}

void TheyThemAudioProcessor::prepareToPlay (double sampleRate, int maximumBlockSize)
{
    // A mono microphone needs one transformation, even with stereo monitoring.
    const int processingChannels = std::min (getTotalNumInputChannels(), getTotalNumOutputChannels());
    engine.prepare (sampleRate, std::max (1, maximumBlockSize), processingChannels);
    preparedSampleRate.store (engine.sampleRate(), std::memory_order_relaxed);
    reportedLatency.store (engine.latencySamples(), std::memory_order_relaxed);
    setLatencySamples (engine.latencySamples());
}

void TheyThemAudioProcessor::releaseResources() { engine.reset(); }
void TheyThemAudioProcessor::reset() { engine.reset(); }

bool TheyThemAudioProcessor::isBusesLayoutSupported (const BusesLayout& layout) const
{
    const auto in = layout.getMainInputChannelSet();
    const auto out = layout.getMainOutputChannelSet();
    return (in == juce::AudioChannelSet::mono() && (out == in || out == juce::AudioChannelSet::stereo()))
        || (in == juce::AudioChannelSet::stereo() && out == in);
}

Parameters TheyThemAudioProcessor::currentParameters() const noexcept
{
    Parameters result;
    result.pitchSemitones = pitch->load (std::memory_order_relaxed);
    result.formantSemitones = formant->load (std::memory_order_relaxed);
    result.character = character->load (std::memory_order_relaxed);
    result.mix = mix->load (std::memory_order_relaxed);
    result.inputGainDb = input->load (std::memory_order_relaxed);
    result.outputGainDb = output->load (std::memory_order_relaxed);
    result.highPassEnabled = highPass->load (std::memory_order_relaxed) >= 0.5f;
    result.compressorEnabled = compressor->load (std::memory_order_relaxed) >= 0.5f;
    result.transformEnabled = transform->load (std::memory_order_relaxed) >= 0.5f;
    result.bypassed = bypass->load (std::memory_order_relaxed) >= 0.5f;
    return VocalEngine::sanitise (result);
}

void TheyThemAudioProcessor::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, bool hostBypass) noexcept
{
    const juce::ScopedNoDenormals noDenormals;
    midi.clear();

    const int outputChannels = std::min (buffer.getNumChannels(), getTotalNumOutputChannels());
    const int processingChannels = std::min (getTotalNumInputChannels(), outputChannels);
    if (processingChannels <= 0 || buffer.getNumSamples() == 0)
    {
        buffer.clear();
        return;
    }
    for (int channel = processingChannels; channel < buffer.getNumChannels(); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    auto settings = currentParameters();
    settings.bypassed = settings.bypassed || hostBypass;
    engine.process (buffer.getArrayOfWritePointers(), processingChannels, buffer.getNumSamples(), settings);
    if (getTotalNumInputChannels() == 1 && outputChannels == 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());
}

void TheyThemAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    process (buffer, midi, false);
}

void TheyThemAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    process (buffer, midi, true);
}

juce::AudioProcessorParameter* TheyThemAudioProcessor::getBypassParameter() const { return bypassParameter; }

double TheyThemAudioProcessor::latencyMilliseconds() const noexcept
{
    const auto rate = preparedSampleRate.load (std::memory_order_relaxed);
    return rate > 0.0 ? 1000.0 * reportedLatency.load (std::memory_order_relaxed) / rate : 0.0;
}

juce::AudioProcessorEditor* TheyThemAudioProcessor::createEditor()
{
    return new TheyThemAudioProcessorEditor (*this);
}

void TheyThemAudioProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destination);
}

void TheyThemAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // State parsing is a host/setup operation, never part of processBlock.
    if (data == nullptr || sizeInBytes <= 0 || sizeInBytes > 1024 * 1024)
        return;
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    const auto incoming = juce::ValueTree::fromXml (*xml);
    auto clean = parameters.copyState();
    for (auto child : clean)
    {
        const auto id = child.getProperty ("id").toString();
        const auto source = incoming.getChildWithProperty ("id", id);
        const auto* parameter = parameters.getParameter (id);
        if (! source.isValid() || parameter == nullptr || ! source.hasProperty ("value"))
            continue;

        const auto text = source.getProperty ("value").toString().trim().toStdString();
        float value = 0.0f;
        const auto parsed = std::from_chars (text.data(), text.data() + text.size(), value);
        if (parsed.ec != std::errc() || parsed.ptr != text.data() + text.size() || ! std::isfinite (value))
            continue;

        const auto& range = parameter->getNormalisableRange();
        value = range.snapToLegalValue (std::clamp (value, range.start, range.end));
        child.setProperty ("value", value, nullptr);
    }
    parameters.replaceState (clean);
}

} // namespace theythem

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new theythem::TheyThemAudioProcessor();
}
