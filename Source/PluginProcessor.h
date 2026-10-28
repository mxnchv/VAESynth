#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>

// Parameter IDs are stored in DAW sessions and automation lanes:
// never rename one once a build has been shared.
namespace ParamIDs
{
    inline const juce::ParameterID azimuth   { "azimuth",   1 };
    inline const juce::ParameterID elevation { "elevation", 1 };
    inline const juce::ParameterID radius    { "radius",    1 };
    inline const juce::ParameterID gain      { "gain",      1 };
}

class VAESynthProcessor final : public juce::AudioProcessor
{
public:
    VAESynthProcessor();
    ~VAESynthProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() noexcept { return state; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState state;

    // Temporary test tone so the skeleton can be checked in a DAW:
    // a plain sine per MIDI note. Replaced by the granular engine in v0.
    juce::Synthesiser testSynth;
    juce::SmoothedValue<float> outputGain;
    std::atomic<float>* gainParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VAESynthProcessor)
};
