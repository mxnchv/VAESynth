#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

class VAESynthEditor final : public juce::AudioProcessorEditor
{
public:
    explicit VAESynthEditor (VAESynthProcessor&);
    ~VAESynthEditor() override = default;

    void paint (juce::Graphics&) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VAESynthEditor)
};
