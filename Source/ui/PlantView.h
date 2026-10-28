#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"

// Draws the plant and plays it: sweeping over buds auditions them in key,
// a click picks one, a double click plants it. Earlier seeds lie underground.
class PlantView final : public juce::Component
{
public:
    explicit PlantView (VAESynthProcessor&);

    // Vertical span the plant may use, in local coordinates: the seed sits at
    // `bottom`, the highest buds reach about `top`.
    void setPlantArea (float top, float bottom);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    std::function<void()> onSelectionChanged;

private:
    struct Ripple { int node; double start; };

    void frame();
    void layout (double now);
    int hitNode (juce::Point<float>) const;
    int hitHistory (juce::Point<float>) const;
    void sweep (juce::Point<float>);
    void play (int node, int note, float velocity);
    int sweepNote (int node) const;

    VAESynthProcessor& processor;
    PlantSession& session;
    juce::VBlankAttachment vblank;

    float areaTop = 80.0f, areaBottom = 400.0f;
    int seenVersion = -1;
    int seenNotes = 0;
    double plantStart = 0.0;
    double lastSweep = 0.0;
    int hovered = -1;

    std::vector<juce::Point<float>> positions;
    std::vector<float> growth;
    std::vector<Ripple> ripples;
    std::vector<juce::Point<float>> historySpots;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlantView)
};
