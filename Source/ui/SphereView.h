#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <deque>

#include "../PluginProcessor.h"

// The latent sphere: every grain of every sample, the cursor, its trail and
// the diameter it moves along. Drag to orbit the camera in the 3D view.
class SphereView final : public juce::Component,
                         private juce::Timer
{
public:
    explicit SphereView (VAESynthProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    enum class View { perspective, top, front };

    void timerCallback() override;
    void setView (View);

    VAESynthProcessor& processor;
    vae::Mat3 camera = vae::Mat3::rotationX (0.35f) * vae::Mat3::rotationY (0.5f);
    View view = View::perspective;
    juce::Point<float> lastDrag;
    std::deque<vae::Vec3> trail;

    juce::TextButton perspectiveButton { "3D" }, topButton { "Top" }, frontButton { "Front" };
};
