#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"
#include "Controls.h"
#include "SampleList.h"

// Right-hand panel with the Space / Grains / Pitch / Voice / Samples tabs.
class ControlPanel final : public juce::Component,
                           private juce::Timer
{
public:
    explicit ControlPanel (VAESynthProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum class Tab { space, grains, pitch, voice, samples };

    void timerCallback() override;
    void showTab (Tab);
    void updateNowPlaying();

    VAESynthProcessor& processor;
    Tab tab = Tab::space;
    juce::OwnedArray<juce::TextButton> tabButtons;

    // Space
    Knob azimuth, elevation, radius;
    SyncableKnob speed;
    ChoiceButtons trajectory;

    // Grains
    SyncableKnob size, density;
    Knob spray, shape, neighbours;

    // Pitch
    ChoiceButtons keyMode;
    StepSelector scaleRoot, scaleType;
    Knob detune, harmonicFilter;

    // Voice
    Knob attack, decay, sustain, release, width, gain, velocityToRadius;

    // Samples
    SampleList sampleList;
    juce::Viewport sampleViewport;
    juce::TextButton clearButton { "Clear all" };

    struct MixRow
    {
        juce::String name;
        juce::Colour colour;
        float share = 0.0f;
    };

    vae::GrainMap::Ptr shownMap;
    juce::String zone;
    std::vector<MixRow> mix;
    juce::Rectangle<int> nowPlayingArea, hintArea, samplesHeaderArea;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> sectionTitles;
};
