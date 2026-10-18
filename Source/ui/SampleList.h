#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"
#include "Theme.h"

// One row per loaded sample: colour, name, root note, mute, solo and remove.
class SampleList final : public juce::Component
{
public:
    explicit SampleList (VAESynthProcessor&);

    void setMap (const vae::GrainMap::Ptr&);
    int getIdealHeight() const noexcept { return (int) rows.size() * rowHeight; }

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int rowHeight = 40;

private:
    struct Row
    {
        vae::SampleSourcePtr source;
        std::unique_ptr<juce::TextButton> down, up, mute, solo, remove;
    };

    void updateToggles();

    VAESynthProcessor& processor;
    std::vector<Row> rows;
};
