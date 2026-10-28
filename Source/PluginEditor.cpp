#include "PluginEditor.h"

VAESynthEditor::VAESynthEditor (VAESynthProcessor& p)
    : AudioProcessorEditor (p)
{
    setSize (960, 600);
}

void VAESynthEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    auto area = getLocalBounds().reduced (32);

    g.setColour (juce::Colour (0xfff2f2f2));
    g.setFont (juce::FontOptions().withHeight (28.0f).withKerningFactor (0.4f));
    g.drawText ("VAESYNTH", area.removeFromTop (40), juce::Justification::centredLeft);

    g.setColour (juce::Colour (0xff8a8a8a));
    g.setFont (juce::FontOptions().withHeight (15.0f));
    g.drawText ("Skeleton build " + juce::String (JucePlugin_VersionString)
                    + "  -  play MIDI notes to hear a test tone",
                area.removeFromTop (24), juce::Justification::centredLeft);
}
