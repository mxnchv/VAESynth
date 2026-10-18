#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"
#include "Theme.h"

// Bottom bar: loaded samples, "Add sample", map state and "Rebuild map".
class SampleBar final : public juce::Component,
                        private juce::Timer
{
public:
    explicit SampleBar (VAESynthProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void chooseFiles();

    VAESynthProcessor& processor;
    juce::TextButton addButton { "+  Add sample" }, rebuildButton { "Rebuild map" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> chipsArea, statusArea;
};
