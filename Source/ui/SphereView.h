#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <deque>

#include "../PluginProcessor.h"

// The latent sphere, as an instrument: drag on it to move the cursor, use the
// wheel to change the radius, click a point to hear it. Alt-drag or right-drag
// orbits the camera. Grains flash as they play.
class SphereView final : public juce::Component,
                         private juce::Timer
{
public:
    explicit SphereView (VAESynthProcessor&);
    ~SphereView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    enum class View { perspective, top, front };
    enum class Drag { none, cursor, camera, pendingAudition };

    struct Frame
    {
        juce::Point<float> centre;
        float scale = 1.0f, focal = 3.4f;
        vae::Mat3 matrix;
    };

    struct Projected
    {
        float x, y, scale, depth;
    };

    struct HitTarget
    {
        juce::Point<float> position;
        float depth;
        int grain;
    };

    void timerCallback() override;
    void setView (View);
    Frame getFrame() const;
    static Projected project (const Frame&, vae::Vec3);
    vae::Vec3 screenToDirection (juce::Point<float>) const;
    int grainAt (juce::Point<float>) const;
    void startCursorDrag();
    void moveCursorTo (juce::Point<float>);
    void endCursorDrag();
    void setParameter (juce::RangedAudioParameter&, float value);

    VAESynthProcessor& processor;
    juce::RangedAudioParameter& azimuth;
    juce::RangedAudioParameter& elevation;
    juce::RangedAudioParameter& radius;
    juce::RangedAudioParameter& trajectoryMode;

    vae::Mat3 camera = vae::Mat3::rotationX (0.35f) * vae::Mat3::rotationY (0.5f);
    View view = View::perspective;
    Drag drag = Drag::none;
    juce::Point<float> lastDrag, dragStart;
    int pressedGrain = -1;

    std::deque<vae::Vec3> trail;
    std::vector<std::pair<int, float>> flashes;  // map grain index, brightness 0..1
    std::vector<HitTarget> hitTargets;           // grains as drawn in the last paint
    vae::GrainMap::Ptr flashMap;

    juce::TextButton perspectiveButton { "3D" }, topButton { "Top" }, frontButton { "Front" };
};
