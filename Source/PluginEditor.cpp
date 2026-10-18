#include "PluginEditor.h"

namespace
{
    constexpr int headerHeight = 64;
    constexpr int footerHeight = 64;
    constexpr int panelWidth = 420;
}

VAESynthEditor::VAESynthEditor (VAESynthProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      sphere (p),
      panel (p),
      sampleBar (p)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (sphere);
    addAndMakeVisible (panel);
    addAndMakeVisible (sampleBar);

    setSize (1180, 760);
}

VAESynthEditor::~VAESynthEditor()
{
    setLookAndFeel (nullptr);
}

void VAESynthEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (28, 0);

    g.setColour (theme::text);
    g.setFont (juce::Font (theme::lightFont (24.0f)).withExtraKerningFactor (0.42f));
    g.drawText ("VAESYNTH", header.removeFromLeft (260), juce::Justification::centredLeft);

    g.setColour (theme::muted);
    g.setFont (theme::font (13.0f));
    g.drawText ("v" + juce::String (JucePlugin_VersionString) + "  prototype", header, juce::Justification::centredRight);

    g.setColour (theme::line);
    g.fillRect (0, headerHeight - 1, getWidth(), 1);
    g.fillRect (getWidth() - panelWidth - 1, headerHeight, 1, getHeight() - headerHeight - footerHeight);
}

void VAESynthEditor::paintOverChildren (juce::Graphics& g)
{
    if (! dragHovering)
        return;

    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillAll();
    g.setColour (theme::text);
    g.drawRect (getLocalBounds().reduced (6), 1);
    g.setFont (theme::font (18.0f));
    g.drawText ("Drop to add samples", getLocalBounds(), juce::Justification::centred);
}

void VAESynthEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (headerHeight);
    sampleBar.setBounds (area.removeFromBottom (footerHeight));
    panel.setBounds (area.removeFromRight (panelWidth));
    area.removeFromRight (1);
    sphere.setBounds (area);
}

bool VAESynthEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& path : files)
        if (VAESynthProcessor::isSupportedAudioFile (juce::File (path)))
            return true;

    return false;
}

void VAESynthEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    dragHovering = true;
    repaint();
}

void VAESynthEditor::fileDragExit (const juce::StringArray&)
{
    dragHovering = false;
    repaint();
}

void VAESynthEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragHovering = false;
    repaint();

    juce::Array<juce::File> audioFiles;
    for (const auto& path : files)
        audioFiles.add (juce::File (path));

    processor.addSampleFiles (audioFiles);
}
