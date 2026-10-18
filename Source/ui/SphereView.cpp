#include "SphereView.h"

#include "Theme.h"

namespace
{
    constexpr size_t maxTrailPoints = 400;
    constexpr size_t maxDrawnGrains = 4000;

    // Meridians and parallels of the unit sphere, as polylines.
    const std::vector<std::vector<vae::Vec3>>& wireframe()
    {
        static const auto lines = []
        {
            std::vector<std::vector<vae::Vec3>> result;
            constexpr int steps = 48;
            constexpr auto twoPi = juce::MathConstants<float>::twoPi;

            for (int k = 0; k < 6; ++k)
            {
                const auto a = (float) k * juce::MathConstants<float>::pi / 6.0f;
                std::vector<vae::Vec3> circle;
                for (int i = 0; i <= steps; ++i)
                {
                    const auto u = (float) i / steps * twoPi;
                    circle.push_back ({ std::sin (u) * std::cos (a), std::cos (u), std::sin (u) * std::sin (a) });
                }
                result.push_back (circle);
            }

            for (auto latitude : { -60.0f, -30.0f, 0.0f, 30.0f, 60.0f })
            {
                const auto la = juce::degreesToRadians (latitude);
                std::vector<vae::Vec3> circle;
                for (int i = 0; i <= steps; ++i)
                {
                    const auto u = (float) i / steps * twoPi;
                    circle.push_back ({ std::cos (la) * std::cos (u), std::sin (la), std::cos (la) * std::sin (u) });
                }
                result.push_back (circle);
            }

            return result;
        }();

        return lines;
    }

    struct Projected
    {
        float x, y, scale, depth;
    };
}

SphereView::SphereView (VAESynthProcessor& p) : processor (p)
{
    for (auto* button : { &perspectiveButton, &topButton, &frontButton })
    {
        buttonStyle::set (*button, "link");
        addAndMakeVisible (button);
    }

    perspectiveButton.onClick = [this] { setView (View::perspective); };
    topButton.onClick         = [this] { setView (View::top); };
    frontButton.onClick       = [this] { setView (View::front); };
    setView (View::perspective);

    setTitle ("Latent sphere");
    startTimerHz (30);
}

void SphereView::setView (View newView)
{
    view = newView;
    perspectiveButton.setToggleState (view == View::perspective, juce::dontSendNotification);
    topButton.setToggleState (view == View::top, juce::dontSendNotification);
    frontButton.setToggleState (view == View::front, juce::dontSendNotification);
    repaint();
}

void SphereView::resized()
{
    auto area = getLocalBounds().removeFromTop (48).reduced (24, 10);
    frontButton.setBounds (area.removeFromRight (44));
    area.removeFromRight (16);
    topButton.setBounds (area.removeFromRight (32));
    area.removeFromRight (16);
    perspectiveButton.setBounds (area.removeFromRight (24));
}

void SphereView::mouseDown (const juce::MouseEvent& e)
{
    lastDrag = e.position;
}

void SphereView::mouseDrag (const juce::MouseEvent& e)
{
    if (view != View::perspective)
        return;

    const auto delta = e.position - lastDrag;
    lastDrag = e.position;
    camera = (vae::Mat3::rotationY (delta.x * 0.008f) * vae::Mat3::rotationX (delta.y * 0.008f) * camera).orthonormalised();
    repaint();
}

void SphereView::timerCallback()
{
    const auto liveCursor = processor.getLiveCursor();
    const auto point = vae::directionFromAngles (liveCursor.azimuth, liveCursor.elevation) * liveCursor.radius;

    if (trail.empty() || (point - trail.back()).length() > 0.015f)
    {
        trail.push_back (point);
        if (trail.size() > maxTrailPoints)
            trail.pop_front();
    }

    repaint();
}

void SphereView::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    const auto bounds = getLocalBounds().toFloat();
    const auto centre = bounds.getCentre();
    const auto scale = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f / 1.75f;

    const auto matrix = view == View::top   ? vae::Mat3::rotationX (juce::MathConstants<float>::halfPi)
                      : view == View::front ? vae::Mat3 {}
                                            : camera;
    const auto focal = view == View::perspective ? 3.4f : 1000.0f;

    auto project = [&] (vae::Vec3 p)
    {
        const auto v = matrix.apply (p);
        const auto s = focal / (focal - v.z);
        return Projected { centre.x + v.x * scale * s, centre.y - v.y * scale * s, s, v.z };
    };

    // Outer limit of the radius (1.5 R) and the sphere's wireframe.
    g.setColour (juce::Colour (0xff2a2a2a));
    g.drawEllipse (juce::Rectangle<float> (scale * 3.0f, scale * 3.0f).withCentre (centre), 1.0f);

    for (const auto& polyline : wireframe())
    {
        for (size_t i = 0; i + 1 < polyline.size(); ++i)
        {
            const auto a = project (polyline[i]), b = project (polyline[i + 1]);
            const auto depth = ((a.depth + b.depth) * 0.5f + 1.0f) * 0.5f;
            g.setColour (juce::Colours::white.withAlpha (0.04f + 0.14f * depth));
            g.drawLine (a.x, a.y, b.x, b.y, 1.0f);
        }
    }

    const auto liveCursor = processor.getLiveCursor();
    const auto direction = vae::directionFromAngles (liveCursor.azimuth, liveCursor.elevation);
    const auto cursorPoint = direction * liveCursor.radius;

    // Diameter from -R to +R through the current direction.
    const auto plus = project (direction * 1.5f), minus = project (direction * -1.5f);
    g.setColour (juce::Colours::white.withAlpha (0.22f));
    g.drawLine (minus.x, minus.y, plus.x, plus.y, 1.0f);
    g.setColour (theme::muted);
    g.setFont (theme::font (12.0f));
    g.drawText ("+R", juce::Rectangle<float> (plus.x + 6.0f, plus.y - 8.0f, 30.0f, 16.0f), juce::Justification::centredLeft);
    g.drawText (juce::String::fromUTF8 ("\xe2\x88\x92R"), juce::Rectangle<float> (minus.x + 6.0f, minus.y - 8.0f, 30.0f, 16.0f), juce::Justification::centredLeft);

    const auto map = processor.getMap();
    const auto hasGrains = map != nullptr && ! map->getGrains().empty();

    if (! hasGrains)
    {
        g.setColour (theme::muted);
        g.setFont (theme::font (15.0f));
        g.drawText (processor.isLoading() ? "Analysing samples..." : "Drop audio files here",
                    bounds.withTrimmedTop (bounds.getHeight() * 0.8f).toNearestInt(), juce::Justification::centredTop);
    }

    // Grains, back to front. The grains nearest the cursor are the ones sounding.
    std::vector<std::pair<float, size_t>> drawOrder;
    std::vector<Projected> projected;
    std::vector<size_t> nearest;

    if (hasGrains)
    {
        const auto& grains = map->getGrains();
        const auto stride = juce::jmax<size_t> (1, grains.size() / maxDrawnGrains);

        projected.reserve (grains.size() / stride + 1);
        for (size_t i = 0; i < grains.size(); i += stride)
        {
            projected.push_back (project (grains[i].position));
            drawOrder.push_back ({ projected.back().depth, i });
        }

        std::sort (drawOrder.begin(), drawOrder.end());

        const auto k = (size_t) juce::roundToInt (processor.getState().getRawParameterValue (ParamIDs::neighbours.getParamID())->load());
        std::vector<std::pair<float, size_t>> distances;
        distances.reserve (grains.size());
        for (size_t i = 0; i < grains.size(); ++i)
            distances.push_back ({ (grains[i].position - cursorPoint).lengthSquared(), i });

        const auto count = juce::jmin (k, distances.size());
        std::partial_sort (distances.begin(), distances.begin() + (std::ptrdiff_t) count, distances.end());
        for (size_t i = 0; i < count; ++i)
            nearest.push_back (distances[i].second);

        for (const auto& [depth, index] : drawOrder)
        {
            const auto& p = projected[index / stride];
            g.setColour (theme::sampleColour (grains[index].source).withAlpha (0.2f + 0.65f * (depth + 1.0f) * 0.5f));
            const auto r = 2.2f * p.scale;
            g.fillEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f);
        }
    }

    // Trail
    if (trail.size() > 1)
    {
        juce::Path path;
        for (size_t i = 0; i < trail.size(); ++i)
        {
            const auto p = project (trail[i]);
            if (i == 0)
                path.startNewSubPath (p.x, p.y);
            else
                path.lineTo (p.x, p.y);
        }

        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.strokePath (path, juce::PathStrokeType (1.2f));
    }

    const auto c = project (cursorPoint);

    if (view == View::perspective)
    {
        // Dashed drop line to the equator so depth can be read.
        const auto foot = project ({ cursorPoint.x, 0.0f, cursorPoint.z });
        const float dashes[] { 3.0f, 4.0f };
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.drawDashedLine ({ c.x, c.y, foot.x, foot.y }, dashes, 2, 1.0f);
        g.drawEllipse (foot.x - 7.0f * foot.scale, foot.y - 3.0f * foot.scale, 14.0f * foot.scale, 6.0f * foot.scale, 1.0f);
    }

    if (hasGrains)
    {
        const auto& grains = map->getGrains();
        for (auto index : nearest)
        {
            const auto p = project (grains[index].position);
            g.setColour (juce::Colours::white.withAlpha (0.3f));
            g.drawLine (c.x, c.y, p.x, p.y, 1.0f);
            g.setColour (theme::sampleColour (grains[index].source));
            const auto r = 4.4f * p.scale;
            g.fillEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f);
        }
    }

    g.setColour (theme::text);
    g.drawEllipse (c.x - 10.0f * c.scale, c.y - 10.0f * c.scale, 20.0f * c.scale, 20.0f * c.scale, 1.5f);
    g.fillEllipse (c.x - 4.5f * c.scale, c.y - 4.5f * c.scale, 9.0f * c.scale, 9.0f * c.scale);
}
