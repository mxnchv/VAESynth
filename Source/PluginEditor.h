#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "ui/ControlPanel.h"
#include "ui/SampleBar.h"
#include "ui/SphereView.h"
#include "ui/Theme.h"

class VAESynthEditor final : public juce::AudioProcessorEditor,
                             public juce::FileDragAndDropTarget
{
public:
    explicit VAESynthEditor (VAESynthProcessor&);
    ~VAESynthEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    VAELookAndFeel lookAndFeel;
    VAESynthProcessor& processor;

    SphereView sphere;
    ControlPanel panel;
    SampleBar sampleBar;
    juce::TooltipWindow tooltips { this };

    bool dragHovering = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VAESynthEditor)
};
