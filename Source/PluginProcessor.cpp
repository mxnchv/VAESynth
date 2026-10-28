#include "PluginProcessor.h"
#include "PluginEditor.h"

VAESynthProcessor::VAESynthProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "VAESynth", createParameterLayout()),
      session (*state.getRawParameterValue (ParamIDs::wild.getParamID()))
{
    spaceParam = state.getRawParameterValue (ParamIDs::space.getParamID());
    volumeParam = state.getRawParameterValue (ParamIDs::volume.getParamID());
    rootParam = state.getRawParameterValue (ParamIDs::scaleRoot.getParamID());
    scaleParam = state.getRawParameterValue (ParamIDs::scaleType.getParamID());

    // Sounds good the moment it opens: start from the first library seed.
    session.chooseLibrarySeed (0);
}

juce::AudioProcessorValueTreeState::ParameterLayout VAESynthProcessor::createParameterLayout()
{
    using Range = juce::NormalisableRange<float>;
    auto percent = juce::AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; });

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (ParamIDs::wild, "Wild", Range { 0.0f, 1.0f }, 0.5f, percent));
    layout.add (std::make_unique<juce::AudioParameterFloat> (ParamIDs::space, "Space", Range { 0.0f, 1.0f }, 0.35f, percent));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::volume, "Volume", Range { -30.0f, 6.0f }, 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; })));
    layout.add (std::make_unique<juce::AudioParameterChoice> (ParamIDs::scaleRoot, "Key", vae::Scale::rootNames(), 9));
    layout.add (std::make_unique<juce::AudioParameterChoice> (ParamIDs::scaleType, "Scale", vae::Scale::typeNames(),
                                                              vae::Scale::typeNames().indexOf ("Pentatonic minor")));
    return layout;
}

vae::Scale VAESynthProcessor::currentScale() const noexcept
{
    return vae::Scale::fromIndices ((int) rootParam->load(), (int) scaleParam->load());
}

void VAESynthProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampler.prepare (sampleRate);

    reverb.setSampleRate (sampleRate);
    reverb.reset();

    limiter.prepare ({ sampleRate, (juce::uint32) juce::jmax (1, samplesPerBlock), 2 });
    limiter.setThreshold (-1.0f);
    limiter.setRelease (120.0f);
    limiter.reset();

    outputGain.reset (sampleRate, 0.02);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (volumeParam->load()));
}

bool VAESynthProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void VAESynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    const auto numSamples = buffer.getNumSamples();
    const auto scale = currentScale();
    const auto playable = session.getPlayableSound();

    // Sweeps and clicks from the editor.
    session.drainAuditions ([this] (const vae::RenderedSound::Ptr& sound, int note, float velocity, float pan)
    {
        sampler.start (sound, note, velocity, pan, false);
    });

    // MIDI, sample-accurate. Notes are kept in key, like the sweeps.
    int position = 0;
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        const auto at = juce::jlimit (0, numSamples, metadata.samplePosition);

        if (at > position)
        {
            sampler.render (buffer, position, at - position);
            position = at;
        }

        if (message.isNoteOn())
        {
            sampler.start (playable, scale.snap ((float) message.getNoteNumber()), message.getFloatVelocity(), 0.0f, true);
            notesPlayed.fetch_add (1);
        }
        else if (message.isNoteOff())
        {
            sampler.noteOff (scale.snap ((float) message.getNoteNumber()));
        }
        else if (message.isSustainPedalOn())
        {
            sampler.setSustain (true);
        }
        else if (message.isSustainPedalOff())
        {
            sampler.setSustain (false);
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            sampler.allNotesOff();
        }
    }

    if (position < numSamples)
        sampler.render (buffer, position, numSamples - position);

    // Space: a soft hall.
    const auto space = spaceParam->load();
    juce::Reverb::Parameters parameters;
    parameters.roomSize = 0.86f;
    parameters.damping = 0.45f;
    parameters.wetLevel = space * 0.5f;
    parameters.dryLevel = 1.0f - space * 0.3f;
    parameters.width = 1.0f;
    reverb.setParameters (parameters);

    if (buffer.getNumChannels() > 1)
        reverb.processStereo (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);
    else
        reverb.processMono (buffer.getWritePointer (0), numSamples);

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (volumeParam->load()));
    outputGain.applyGain (buffer, numSamples);

    // Never louder than full scale.
    juce::dsp::AudioBlock<float> block (buffer);
    limiter.process (juce::dsp::ProcessContextReplacing<float> (block));
}

juce::AudioProcessorEditor* VAESynthProcessor::createEditor()
{
    return new VAESynthEditor (*this);
}

void VAESynthProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto tree = state.copyState();
    tree.removeChild (tree.getChildWithName ("Plant"), nullptr);
    tree.appendChild (session.toValueTree(), nullptr);

    if (auto xml = tree.createXml())
        copyXmlToBinary (*xml, destData);
}

void VAESynthProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (state.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    const auto plant = tree.getChildWithName ("Plant");
    tree.removeChild (plant, nullptr);
    state.replaceState (tree);

    if (plant.isValid())
        session.restore (plant);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VAESynthProcessor();
}
