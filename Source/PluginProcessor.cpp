#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    struct TestToneSound final : juce::SynthesiserSound
    {
        bool appliesToNote (int) override    { return true; }
        bool appliesToChannel (int) override { return true; }
    };

    class TestToneVoice final : public juce::SynthesiserVoice
    {
    public:
        bool canPlaySound (juce::SynthesiserSound* s) override
        {
            return dynamic_cast<TestToneSound*> (s) != nullptr;
        }

        void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int) override
        {
            phase = 0.0;
            phaseDelta = juce::MathConstants<double>::twoPi
                         * juce::MidiMessage::getMidiNoteInHertz (midiNote) / getSampleRate();
            level = 0.2f * velocity;
            adsr.setSampleRate (getSampleRate());
            adsr.setParameters ({ 0.01f, 0.1f, 0.8f, 0.3f });
            adsr.noteOn();
        }

        void stopNote (float, bool allowTailOff) override
        {
            if (allowTailOff)
            {
                adsr.noteOff();
            }
            else
            {
                adsr.reset();
                clearCurrentNote();
            }
        }

        void pitchWheelMoved (int) override {}
        void controllerMoved (int, int) override {}

        void renderNextBlock (juce::AudioBuffer<float>& out, int start, int numSamples) override
        {
            if (! isVoiceActive())
                return;

            for (int i = start; i < start + numSamples; ++i)
            {
                const auto sample = (float) std::sin (phase) * level * adsr.getNextSample();
                phase += phaseDelta;

                for (int ch = 0; ch < out.getNumChannels(); ++ch)
                    out.addSample (ch, i, sample);

                if (! adsr.isActive())
                {
                    clearCurrentNote();
                    break;
                }
            }
        }

    private:
        double phase = 0.0, phaseDelta = 0.0;
        float level = 0.0f;
        juce::ADSR adsr;
    };
}

VAESynthProcessor::VAESynthProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "VAESynth", createParameterLayout())
{
    gainParam = state.getRawParameterValue (ParamIDs::gain.getParamID());

    for (int i = 0; i < 8; ++i)
        testSynth.addVoice (new TestToneVoice());

    testSynth.addSound (new TestToneSound());
}

juce::AudioProcessorValueTreeState::ParameterLayout VAESynthProcessor::createParameterLayout()
{
    using Range = juce::NormalisableRange<float>;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::azimuth, "Azimuth", Range { -180.0f, 180.0f }, 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("deg")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::elevation, "Elevation", Range { -90.0f, 90.0f }, 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("deg")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::radius, "Radius", Range { -1.5f, 1.5f }, 1.0f,
        juce::AudioParameterFloatAttributes().withLabel ("R")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::gain, "Gain", Range { -24.0f, 6.0f }, 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    return layout;
}

void VAESynthProcessor::prepareToPlay (double sampleRate, int)
{
    testSynth.setCurrentPlaybackSampleRate (sampleRate);
    outputGain.reset (sampleRate, 0.02);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gainParam->load()));
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
    testSynth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (gainParam->load()));
    outputGain.applyGain (buffer, buffer.getNumSamples());
}

juce::AudioProcessorEditor* VAESynthProcessor::createEditor()
{
    return new VAESynthEditor (*this);
}

void VAESynthProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void VAESynthProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
            state.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VAESynthProcessor();
}
