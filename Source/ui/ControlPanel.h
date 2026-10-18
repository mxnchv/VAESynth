#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"
#include "Controls.h"

// Root note editor: one row per loaded sample.
class RootNoteList final : public juce::Component
{
public:
    explicit RootNoteList (VAESynthProcessor&);

    void setMap (vae::GrainMap::Ptr);
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int rowHeight = 36;

private:
    struct Row
    {
        juce::String name;
        juce::Colour colour;
        std::unique_ptr<juce::TextButton> down, up;
    };

    VAESynthProcessor& processor;
    std::vector<Row> rows;
};

// Right-hand panel with the Space / Grains / Pitch / Voice tabs.
class ControlPanel final : public juce::Component,
                           private juce::Timer
{
public:
    explicit ControlPanel (VAESynthProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum class Tab { space, grains, pitch, voice };

    void timerCallback() override;
    void showTab (Tab);
    void updateNowPlaying();

    VAESynthProcessor& processor;
    Tab tab = Tab::space;
    juce::OwnedArray<juce::TextButton> tabButtons;

    // Space
    Knob azimuth, elevation, radius, speed;
    ChoiceButtons trajectory;

    // Grains
    Knob size, density, spray, shape, neighbours;

    // Pitch
    ChoiceButtons keyMode;
    Knob detune;
    RootNoteList rootNotes;

    // Voice
    Knob attack, decay, sustain, release, width, gain, velocityToRadius;

    struct MixRow
    {
        juce::String name;
        juce::Colour colour;
        float share = 0.0f;
    };

    vae::GrainMap::Ptr shownMap;
    juce::String zone;
    std::vector<MixRow> mix;
    juce::Rectangle<int> nowPlayingArea;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> sectionTitles;
};
