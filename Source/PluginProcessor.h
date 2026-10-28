#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "PlantSession.h"
#include "engine/Sampler.h"
#include "engine/Scale.h"

// Parameter IDs are stored in DAW sessions and automation lanes:
// never rename one once a build has been shared.
namespace ParamIDs
{
    inline const juce::ParameterID wild      { "wild",      1 };
    inline const juce::ParameterID space     { "space",     1 };
    inline const juce::ParameterID volume    { "volume",    1 };
    inline const juce::ParameterID scaleRoot { "scaleRoot", 1 };
    inline const juce::ParameterID scaleType { "scaleType", 1 };
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
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() noexcept { return state; }
    PlantSession& getSession() noexcept                     { return session; }
    vae::Scale currentScale() const noexcept;

    // Counts MIDI notes played, so the editor can ripple the playing bud.
    int getNotesPlayed() const noexcept { return notesPlayed.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState state;
    std::atomic<float>* spaceParam = nullptr;
    std::atomic<float>* volumeParam = nullptr;
    std::atomic<float>* rootParam = nullptr;
    std::atomic<float>* scaleParam = nullptr;

    PlantSession session;
    vae::Sampler sampler;
    juce::Reverb reverb;
    juce::dsp::Limiter<float> limiter;
    juce::SmoothedValue<float> outputGain;
    std::atomic<int> notesPlayed { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VAESynthProcessor)
};
