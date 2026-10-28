#include "PlantView.h"
#include "Theme.h"

namespace
{
    double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }

    constexpr float hitRadius = 14.0f;
    constexpr double rippleSeconds = 1.2;
}

PlantView::PlantView (VAESynthProcessor& p)
    : processor (p), session (p.getSession()), vblank (this, [this] { frame(); })
{
    setOpaque (true);
}

void PlantView::setPlantArea (float top, float bottom)
{
    areaTop = top;
    areaBottom = bottom;
}

void PlantView::frame()
{
    const auto now = nowSeconds();

    if (session.getPlantVersion() != seenVersion)
    {
        seenVersion = session.getPlantVersion();
        plantStart = now;
        hovered = -1;
        ripples.clear();
    }

    // A MIDI note ripples the bud that is playing.
    const auto notes = processor.getNotesPlayed();
    if (notes != seenNotes)
    {
        seenNotes = notes;
        ripples.push_back ({ juce::jmax (0, session.getSelected()), now });
    }

    ripples.erase (std::remove_if (ripples.begin(), ripples.end(),
                                   [now] (const Ripple& r) { return now - r.start > rippleSeconds; }),
                   ripples.end());

    layout (now);
    repaint();
}

// Places every node for this frame: stems grow in, then sway a little.
void PlantView::layout (double now)
{
    const auto& nodes = session.getPlant().nodes;
    positions.assign (nodes.size(), {});
    growth.assign (nodes.size(), 0.0f);
    if (nodes.empty())
        return;

    const auto width = (float) getWidth();
    const auto span = juce::jmax (120.0f, areaBottom - areaTop);
    const auto unit = juce::jmin (span * 0.95f, width * (width < 700.0f ? 0.52f : 0.6f));
    const auto elapsed = (float) (now - plantStart);

    positions[0] = { width * 0.5f, areaBottom };
    growth[0] = 1.0f;

    // Nodes are stored depth-first, so every parent comes before its children.
    for (size_t i = 1; i < nodes.size(); ++i)
    {
        const auto& node = nodes[i];
        const auto sway = std::sin ((float) now * 0.7f + node.phase) * 0.018f * (float) node.depth;
        const auto angle = node.angle + sway;
        const auto grow = juce::jlimit (0.0f, 1.0f, (elapsed - node.born) / vae::Plant::growSeconds);
        const auto eased = 1.0f - std::pow (1.0f - grow, 3.0f);
        const auto length = node.length * unit * eased;
        const auto parent = positions[(size_t) node.parent];

        positions[i] = { parent.x + std::sin (angle) * length, parent.y - std::cos (angle) * length };
        growth[i] = grow;
    }
}

void PlantView::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    const auto& nodes = session.getPlant().nodes;
    if (nodes.empty() || positions.size() != nodes.size())
        return;

    const auto now = nowSeconds();

    // Stems.
    for (size_t i = 1; i < nodes.size(); ++i)
    {
        if (growth[i] <= 0.0f)
            continue;

        const auto a = positions[(size_t) nodes[i].parent], b = positions[i];
        const auto control = juce::Point<float> ((a.x + b.x) * 0.5f + (b.y - a.y) * 0.08f,
                                                 (a.y + b.y) * 0.5f - (b.x - a.x) * 0.08f);
        juce::Path stem;
        stem.startNewSubPath (a);
        stem.quadraticTo (control, b);

        g.setColour (theme::text.withAlpha (juce::jmax (0.12f, 0.4f - (float) nodes[i].depth * 0.06f)));
        g.strokePath (stem, juce::PathStrokeType (juce::jmax (0.6f, 2.1f - (float) nodes[i].depth * 0.4f),
                                                  juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Buds: plain dots. Dim until their sound is ready.
    const auto selected = session.getSelected();
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        const auto isSeed = i == 0;
        if (! isSeed && growth[i] < 0.9f)
            continue;

        const auto appear = isSeed ? 1.0f : (growth[i] - 0.9f) / 0.1f;
        const auto radius = (isSeed ? 4.5f : 2.6f) * appear * ((int) i == hovered ? 1.4f : 1.0f);
        const auto ready = session.isRendered ((int) i);

        g.setColour (theme::text.withAlpha (ready ? (isSeed ? 1.0f : 0.85f) : 0.3f));
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (positions[i]));

        if ((int) i == selected)
        {
            g.setColour (theme::text);
            g.drawEllipse (juce::Rectangle<float> (18.0f, 18.0f).withCentre (positions[i]), 1.0f);
        }
    }

    // Played buds ripple like a drop in water.
    for (const auto& r : ripples)
    {
        if (r.node >= (int) positions.size())
            continue;

        const auto k = (float) ((now - r.start) / rippleSeconds);
        const auto size = 12.0f + k * 60.0f;
        g.setColour (theme::text.withAlpha ((1.0f - k) * 0.55f));
        g.drawEllipse (juce::Rectangle<float> (size, size).withCentre (positions[(size_t) r.node]), 1.0f);
    }

    // The ground, with earlier seeds lying under it, oldest on the left.
    const auto groundY = areaBottom + 20.0f;
    const auto centreX = (float) getWidth() * 0.5f;
    g.setColour (theme::line);
    g.fillRect (centreX - 90.0f, groundY, 180.0f, 1.0f);

    historySpots.clear();
    const auto& history = session.getHistory();
    if (history.size() > 1)
    {
        const auto gap = 16.0f;
        const auto first = centreX - gap * (float) (history.size() - 1) * 0.5f;
        for (size_t i = 0; i < history.size(); ++i)
        {
            const juce::Point<float> spot (first + gap * (float) i, groundY + 16.0f);
            historySpots.push_back (spot);

            if ((int) i == session.getCurrentGeneration())
            {
                g.setColour (theme::text);
                g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (spot));
            }
            else
            {
                g.setColour (theme::text.withAlpha (0.5f));
                g.drawEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (spot), 1.0f);
            }
        }
    }
}

int PlantView::hitNode (juce::Point<float> point) const
{
    int best = -1;
    auto bestDistance = hitRadius;

    for (size_t i = 0; i < positions.size(); ++i)
    {
        if (i > 0 && growth[i] < 1.0f)
            continue;

        const auto d = positions[i].getDistanceFrom (point);
        if (d < bestDistance)
        {
            best = (int) i;
            bestDistance = d;
        }
    }

    return best;
}

int PlantView::hitHistory (juce::Point<float> point) const
{
    for (size_t i = 0; i < historySpots.size(); ++i)
        if (historySpots[i].getDistanceFrom (point) < 9.0f)
            return (int) i;

    return -1;
}

// Higher on screen sounds higher, in key, around the seed's own note.
int PlantView::sweepNote (int node) const
{
    const auto span = juce::jmax (1.0f, areaBottom - areaTop);
    const auto k = juce::jlimit (0.0f, 1.0f, (areaBottom - positions[(size_t) node].y) / span);
    return processor.currentScale().snap ((float) session.getHomeNote() - 7.0f + k * 19.0f);
}

void PlantView::play (int node, int note, float velocity)
{
    const auto pan = juce::jlimit (-0.6f, 0.6f, (positions[(size_t) node].x - (float) getWidth() * 0.5f) / ((float) getWidth() * 0.5f));
    session.audition (node, note, velocity, pan);
    ripples.push_back ({ node, nowSeconds() });
}

void PlantView::sweep (juce::Point<float> point)
{
    const auto node = hitNode (point);
    setMouseCursor (node >= 0 || hitHistory (point) >= 0 ? juce::MouseCursor::PointingHandCursor
                                                         : juce::MouseCursor::NormalCursor);
    if (node == hovered)
        return;

    hovered = node;
    const auto now = nowSeconds();
    if (node >= 0 && now - lastSweep > 0.05)
    {
        lastSweep = now;
        play (node, sweepNote (node), 0.7f);
    }
}

void PlantView::mouseMove (const juce::MouseEvent& e) { sweep (e.position); }
void PlantView::mouseDrag (const juce::MouseEvent& e) { sweep (e.position); }
void PlantView::mouseExit (const juce::MouseEvent&)   { hovered = -1; }

void PlantView::mouseDown (const juce::MouseEvent& e)
{
    if (const auto generation = hitHistory (e.position); generation >= 0)
    {
        session.goTo (generation);
    }
    else if (const auto node = hitNode (e.position); node >= 0)
    {
        session.select (node);
        play (node, session.getHomeNote(), 0.8f);
        hovered = node;
    }
    else
    {
        session.select (-1);
    }

    if (onSelectionChanged)
        onSelectionChanged();
}

void PlantView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto node = hitNode (e.position);
    if (node > 0)
    {
        session.plantNode (node);
        if (onSelectionChanged)
            onSelectionChanged();
    }
}
