#include "SphereView.h"

#include "Theme.h"

namespace
{
    constexpr size_t maxTrailPoints = 400;
    constexpr size_t maxDrawnGrains = 4000;
    constexpr size_t maxFlashes = 400;
    constexpr float hitRadius = 7.0f;
    constexpr float dragThreshold = 4.0f;

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

    juce::RangedAudioParameter& parameter (VAESynthProcessor& p, const juce::ParameterID& id)
    {
        return *p.getState().getParameter (id.getParamID());
    }
}

SphereView::SphereView (VAESynthProcessor& p)
    : processor (p),
      azimuth (parameter (p, ParamIDs::azimuth)),
      elevation (parameter (p, ParamIDs::elevation)),
      radius (parameter (p, ParamIDs::radius)),
      trajectoryMode (parameter (p, ParamIDs::trajectoryMode))
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
    setDescription ("Drag to move the cursor, mouse wheel to change the radius, click a point to hear it, Alt-drag to rotate.");
    startTimerHz (30);
}

SphereView::~SphereView()
{
    if (drag == Drag::cursor)
        endCursorDrag();
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

SphereView::Frame SphereView::getFrame() const
{
    Frame f;
    const auto bounds = getLocalBounds().toFloat();
    f.centre = bounds.getCentre();
    f.scale = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f / 1.75f;
    f.focal = view == View::perspective ? 3.4f : 1000.0f;
    f.matrix = view == View::top   ? vae::Mat3::rotationX (juce::MathConstants<float>::halfPi)
             : view == View::front ? vae::Mat3 {}
                                   : camera;
    return f;
}

SphereView::Projected SphereView::project (const Frame& f, vae::Vec3 p)
{
    const auto v = f.matrix.apply (p);
    const auto s = f.focal / (f.focal - v.z);
    return { f.centre.x + v.x * f.scale * s, f.centre.y - v.y * f.scale * s, s, v.z };
}

//==============================================================================
vae::Vec3 SphereView::screenToDirection (juce::Point<float> position) const
{
    const auto f = getFrame();
    const auto sx = (position.x - f.centre.x) / f.scale;
    const auto sy = -(position.y - f.centre.y) / f.scale;
    const auto planar = sx * sx + sy * sy;

    // Pick on a sphere the size of the current radius, so the cursor stays
    // under the mouse whether it is near the core or out in extrapolation.
    const auto r = juce::jlimit (0.3f, 1.5f, std::abs (processor.getLiveCursor().radius));
    vae::Vec3 viewPoint;

    if (view == View::perspective)
    {
        // Ray from the eye at (0, 0, F) through the screen point: (t*sx, t*sy, F - t*F).
        const auto F = f.focal;
        const auto a = planar + F * F, b = -2.0f * F * F, c = F * F - r * r;
        const auto discriminant = b * b - 4.0f * a * c;

        if (discriminant >= 0.0f)
        {
            const auto t = (-b - std::sqrt (discriminant)) / (2.0f * a);
            viewPoint = { t * sx, t * sy, F - t * F };
        }
        else
        {
            viewPoint = vae::Vec3 { sx, sy, 0.0f }.normalised() * r;
        }
    }
    else
    {
        viewPoint = planar < r * r ? vae::Vec3 { sx, sy, std::sqrt (r * r - planar) }
                              : vae::Vec3 { sx, sy, 0.0f }.normalised() * r;
    }

    return f.matrix.transposed().apply (viewPoint).normalised();
}

int SphereView::grainAt (juce::Point<float> position) const
{
    int best = -1;
    float bestDepth = -1.0e9f;

    for (const auto& target : hitTargets)
        if (target.position.getDistanceFrom (position) <= hitRadius && target.depth > bestDepth)
        {
            best = target.grain;
            bestDepth = target.depth;
        }

    return best;
}

void SphereView::setParameter (juce::RangedAudioParameter& p, float value)
{
    p.setValueNotifyingHost (p.convertTo0to1 (value));
}

void SphereView::startCursorDrag()
{
    drag = Drag::cursor;

    // Grabbing the cursor takes it over from any running trajectory.
    if (juce::roundToInt (trajectoryMode.convertFrom0to1 (trajectoryMode.getValue())) != (int) TrajectoryMode::manual)
    {
        trajectoryMode.beginChangeGesture();
        setParameter (trajectoryMode, (float) TrajectoryMode::manual);
        trajectoryMode.endChangeGesture();
    }

    azimuth.beginChangeGesture();
    elevation.beginChangeGesture();
}

void SphereView::moveCursorTo (juce::Point<float> position)
{
    auto direction = screenToDirection (position);

    // With a negative radius the cursor sits on the opposite side of its direction.
    if (radius.convertFrom0to1 (radius.getValue()) < 0.0f)
        direction = direction * -1.0f;

    setParameter (azimuth, juce::radiansToDegrees (std::atan2 (direction.x, direction.z)));
    setParameter (elevation, juce::radiansToDegrees (std::asin (juce::jlimit (-1.0f, 1.0f, direction.y))));
}

void SphereView::endCursorDrag()
{
    azimuth.endChangeGesture();
    elevation.endChangeGesture();
    drag = Drag::none;
}

void SphereView::mouseDown (const juce::MouseEvent& e)
{
    lastDrag = dragStart = e.position;

    if (e.mods.isPopupMenu() || e.mods.isAltDown() || e.mods.isMiddleButtonDown())
    {
        drag = Drag::camera;
        return;
    }

    pressedGrain = grainAt (e.position);

    if (pressedGrain >= 0)
    {
        drag = Drag::pendingAudition;
        return;
    }

    startCursorDrag();
    moveCursorTo (e.position);
}

void SphereView::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::camera)
    {
        if (view == View::perspective)
        {
            const auto delta = e.position - lastDrag;
            camera = (vae::Mat3::rotationY (delta.x * 0.008f) * vae::Mat3::rotationX (delta.y * 0.008f) * camera).orthonormalised();
            repaint();
        }
    }
    else if (drag == Drag::pendingAudition)
    {
        // Pressing on a point and moving away means grabbing, not listening.
        if (e.position.getDistanceFrom (dragStart) > dragThreshold)
        {
            startCursorDrag();
            moveCursorTo (e.position);
        }
    }
    else if (drag == Drag::cursor)
    {
        moveCursorTo (e.position);
    }

    lastDrag = e.position;
}

void SphereView::mouseUp (const juce::MouseEvent&)
{
    if (drag == Drag::pendingAudition && pressedGrain >= 0)
    {
        processor.auditionGrain (pressedGrain);
        flashes.push_back ({ pressedGrain, 1.0f });
    }
    else if (drag == Drag::cursor)
    {
        endCursorDrag();
    }

    drag = Drag::none;
    pressedGrain = -1;
}

void SphereView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const auto current = radius.convertFrom0to1 (radius.getValue());
    const auto delta = (wheel.isReversed ? -wheel.deltaY : wheel.deltaY) * 0.75f;

    radius.beginChangeGesture();
    setParameter (radius, juce::jlimit (-1.5f, 1.5f, current + delta));
    radius.endChangeGesture();
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

    // Grains that started playing light up and fade.
    const auto map = processor.getMap();
    if (map != flashMap)
    {
        flashMap = map;
        flashes.clear();
    }

    for (auto& flash : flashes)
        flash.second *= 0.86f;

    flashes.erase (std::remove_if (flashes.begin(), flashes.end(), [] (const auto& f) { return f.second < 0.05f; }), flashes.end());

    processor.readPlayedGrains ([this] (int index)
    {
        if (flashes.size() < maxFlashes)
            flashes.push_back ({ index, 1.0f });
    });

    repaint();
}

void SphereView::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    const auto frame = getFrame();
    const auto bounds = getLocalBounds().toFloat();
    auto proj = [&frame] (vae::Vec3 p) { return project (frame, p); };

    // Outer limit of the radius (1.5 R) and the sphere's wireframe.
    g.setColour (juce::Colour (0xff2a2a2a));
    g.drawEllipse (juce::Rectangle<float> (frame.scale * 3.0f, frame.scale * 3.0f).withCentre (frame.centre), 1.0f);

    for (const auto& polyline : wireframe())
    {
        for (size_t i = 0; i + 1 < polyline.size(); ++i)
        {
            const auto a = proj (polyline[i]), b = proj (polyline[i + 1]);
            const auto depth = ((a.depth + b.depth) * 0.5f + 1.0f) * 0.5f;
            g.setColour (juce::Colours::white.withAlpha (0.04f + 0.14f * depth));
            g.drawLine (a.x, a.y, b.x, b.y, 1.0f);
        }
    }

    const auto liveCursor = processor.getLiveCursor();
    const auto direction = vae::directionFromAngles (liveCursor.azimuth, liveCursor.elevation);
    const auto cursorPoint = direction * liveCursor.radius;

    // Diameter from -R to +R through the current direction.
    const auto plus = proj (direction * 1.5f), minus = proj (direction * -1.5f);
    g.setColour (juce::Colours::white.withAlpha (0.22f));
    g.drawLine (minus.x, minus.y, plus.x, plus.y, 1.0f);
    g.setColour (theme::muted);
    g.setFont (theme::font (12.0f));
    g.drawText ("+R", juce::Rectangle<float> (plus.x + 6.0f, plus.y - 8.0f, 30.0f, 16.0f), juce::Justification::centredLeft);
    g.drawText (juce::String::fromUTF8 ("\xe2\x88\x92R"), juce::Rectangle<float> (minus.x + 6.0f, minus.y - 8.0f, 30.0f, 16.0f), juce::Justification::centredLeft);

    const auto map = processor.getMap();
    const auto hasGrains = map != nullptr && ! map->getGrains().empty();
    hitTargets.clear();

    if (! hasGrains)
    {
        g.setColour (theme::muted);
        g.setFont (theme::font (15.0f));
        g.drawText (processor.isLoading() ? "Analysing samples..." : "Drop audio files here",
                    bounds.withTrimmedTop (bounds.getHeight() * 0.8f).toNearestInt(), juce::Justification::centredTop);
    }

    std::vector<size_t> nearest;

    if (hasGrains)
    {
        const auto& grains = map->getGrains();
        const auto& sources = map->getSources();

        bool anySolo = false;
        for (const auto& source : sources)
            anySolo = anySolo || processor.isSoloed (source->slot);

        std::vector<bool> audible (sources.size());
        for (size_t s = 0; s < sources.size(); ++s)
            audible[s] = anySolo ? processor.isSoloed (sources[s]->slot) : ! processor.isMuted (sources[s]->slot);

        // Back to front, thinned out when there are very many grains.
        const auto stride = juce::jmax<size_t> (1, grains.size() / maxDrawnGrains);
        std::vector<std::pair<float, size_t>> drawOrder;
        std::vector<Projected> projected;
        projected.reserve (grains.size() / stride + 1);

        for (size_t i = 0; i < grains.size(); i += stride)
        {
            projected.push_back (proj (grains[i].position));
            drawOrder.push_back ({ projected.back().depth, i });
        }

        std::sort (drawOrder.begin(), drawOrder.end());

        for (const auto& [depth, index] : drawOrder)
        {
            const auto& p = projected[index / stride];
            const auto source = (size_t) grains[index].source;
            const auto alpha = (0.2f + 0.65f * (depth + 1.0f) * 0.5f) * (audible[source] ? 1.0f : 0.25f);

            g.setColour (theme::sampleColour (sources[source]->slot).withAlpha (alpha));
            const auto r = 2.2f * p.scale;
            g.fillEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f);

            if (audible[source])
                hitTargets.push_back ({ { p.x, p.y }, depth, (int) index });
        }

        // The grains nearest the cursor are the ones it is playing from.
        const auto k = (size_t) juce::roundToInt (processor.getState().getRawParameterValue (ParamIDs::neighbours.getParamID())->load());
        std::vector<std::pair<float, size_t>> distances;
        distances.reserve (grains.size());
        for (size_t i = 0; i < grains.size(); ++i)
            if (audible[(size_t) grains[i].source])
                distances.push_back ({ (grains[i].position - cursorPoint).lengthSquared(), i });

        const auto count = juce::jmin (k, distances.size());
        std::partial_sort (distances.begin(), distances.begin() + (std::ptrdiff_t) count, distances.end());
        for (size_t i = 0; i < count; ++i)
            nearest.push_back (distances[i].second);

        // Flashes for grains that just played.
        for (const auto& [index, level] : flashes)
        {
            if (! juce::isPositiveAndBelow (index, (int) grains.size()))
                continue;

            const auto p = proj (grains[(size_t) index].position);
            const auto colour = theme::sampleColour (sources[(size_t) grains[(size_t) index].source]->slot);
            const auto halo = (3.0f + 7.0f * level) * p.scale;

            g.setColour (colour.withAlpha (0.35f * level));
            g.fillEllipse (p.x - halo, p.y - halo, halo * 2.0f, halo * 2.0f);
            g.setColour (juce::Colours::white.withAlpha (level));
            const auto core = 2.0f * p.scale;
            g.fillEllipse (p.x - core, p.y - core, core * 2.0f, core * 2.0f);
        }
    }

    // Trail
    if (trail.size() > 1)
    {
        juce::Path path;
        for (size_t i = 0; i < trail.size(); ++i)
        {
            const auto p = proj (trail[i]);
            if (i == 0)
                path.startNewSubPath (p.x, p.y);
            else
                path.lineTo (p.x, p.y);
        }

        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.strokePath (path, juce::PathStrokeType (1.2f));
    }

    const auto c = proj (cursorPoint);

    if (view == View::perspective)
    {
        // Dashed drop line to the equator so depth can be read.
        const auto foot = proj ({ cursorPoint.x, 0.0f, cursorPoint.z });
        const float dashes[] { 3.0f, 4.0f };
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.drawDashedLine ({ c.x, c.y, foot.x, foot.y }, dashes, 2, 1.0f);
        g.drawEllipse (foot.x - 7.0f * foot.scale, foot.y - 3.0f * foot.scale, 14.0f * foot.scale, 6.0f * foot.scale, 1.0f);
    }

    if (hasGrains)
    {
        const auto& grains = map->getGrains();
        const auto& sources = map->getSources();

        for (auto index : nearest)
        {
            const auto p = proj (grains[index].position);
            g.setColour (juce::Colours::white.withAlpha (0.3f));
            g.drawLine (c.x, c.y, p.x, p.y, 1.0f);
            g.setColour (theme::sampleColour (sources[(size_t) grains[index].source]->slot));
            const auto r = 4.4f * p.scale;
            g.fillEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f);
        }
    }

    g.setColour (theme::text);
    g.drawEllipse (c.x - 10.0f * c.scale, c.y - 10.0f * c.scale, 20.0f * c.scale, 20.0f * c.scale, 1.5f);
    g.fillEllipse (c.x - 4.5f * c.scale, c.y - 4.5f * c.scale, 9.0f * c.scale, 9.0f * c.scale);

    g.setColour (theme::muted);
    g.setFont (theme::font (12.0f));
    g.drawText (juce::String::fromUTF8 ("Drag: move  \xc2\xb7  Wheel: radius  \xc2\xb7  Click a point: listen  \xc2\xb7  Alt-drag: rotate"),
                getLocalBounds().removeFromBottom (32).reduced (24, 0), juce::Justification::centredLeft, true);
}
