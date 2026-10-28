#include "PlantView.h"
#include "Theme.h"

#include <functional>

namespace
{
    double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }

    constexpr float hitRadius = 14.0f;
    constexpr double rippleSeconds = 1.2;
    constexpr double pulseSeconds = 2.0;

    // A point along the curved stem from parent a to child b, t in 0..1.
    juce::Point<float> stemPoint (juce::Point<float> a, juce::Point<float> b, float t)
    {
        const juce::Point<float> control ((a.x + b.x) * 0.5f + (b.y - a.y) * 0.08f,
                                          (a.y + b.y) * 0.5f - (b.x - a.x) * 0.08f);
        const auto u = 1.0f - t;
        return a * (u * u) + control * (2.0f * u * t) + b * (t * t);
    }
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

    // A plant grown from scratch replays its growth; a Wild change only re-renders.
    if (session.getShapeVersion() != seenVersion)
    {
        seenVersion = session.getShapeVersion();
        plantStart = now;
        hovered = -1;
        ripples.clear();
        pulses.clear();
        if (session.hasPlant())
            growRoots (session.getHistory()[(size_t) session.getCurrentGeneration()].rngSeed);
    }

    // A MIDI note lights the bud that is playing.
    const auto notes = processor.getNotesPlayed();
    if (notes != seenNotes)
    {
        seenNotes = notes;
        addPulse (juce::jmax (0, session.getSelected()), 0.8f, now);
    }

    ripples.erase (std::remove_if (ripples.begin(), ripples.end(),
                                   [now] (const Ripple& r) { return now - r.start > rippleSeconds; }),
                   ripples.end());
    pulses.erase (std::remove_if (pulses.begin(), pulses.end(),
                                  [now] (const Pulse& p) { return now - p.start > pulseSeconds; }),
                  pulses.end());

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

    // Roots: wide and shallow under the seed.
    const auto rootStart = juce::Point<float> (width * 0.5f, areaBottom + 4.0f);
    rootFrom.assign (roots.size(), {});
    rootTo.assign (roots.size(), {});
    rootDistance.assign (roots.size(), 0.0f);
    rootGrowth.assign (roots.size(), 0.0f);

    for (size_t i = 0; i < roots.size(); ++i)
    {
        const auto& r = roots[i];
        const auto from = r.parent < 0 ? rootStart : rootTo[(size_t) r.parent];
        const auto grow = juce::jlimit (0.0f, 1.0f, (elapsed - r.born) / 0.5f);
        const auto length = r.length * rootDepth * (1.0f - std::pow (1.0f - grow, 3.0f));

        rootFrom[i] = from;
        rootTo[i] = { from.x + std::sin (r.angle) * length * 1.7f, from.y + std::cos (r.angle) * length * 0.75f };
        rootDistance[i] = (r.parent < 0 ? 0.0f : rootDistance[(size_t) r.parent] + roots[(size_t) r.parent].length * rootDepth);
        rootGrowth[i] = grow;
    }
}

void PlantView::growRoots (std::uint32_t seed)
{
    vae::Rng rng (seed ^ 0x9e3779b9u);
    roots.clear();

    std::function<void (int, float, float, int, float)> branch = [&] (int parent, float angle, float length, int depth, float born)
    {
        const auto index = (int) roots.size();
        roots.push_back ({ parent, depth, angle, length, born });
        if (depth >= 4)
            return;

        const auto count = rng.uniform() < (depth == 1 ? 0.85f : 0.55f) ? 2 : 1;
        for (int i = 0; i < count; ++i)
        {
            const auto turn = count == 1 ? rng.gauss() * 0.3f : (i > 0 ? 1.0f : -1.0f) * (0.3f + rng.uniform() * 0.35f);
            branch (index, juce::jlimit (-1.45f, 1.45f, angle + turn), length * (0.6f + rng.uniform() * 0.2f),
                    depth + 1, born + 0.18f + rng.uniform() * 0.12f);
        }
    };

    const auto mains = 3 + (rng.uniform() < 0.5f ? 0 : 1);
    for (int i = 0; i < mains; ++i)
    {
        const auto side = (float) i / (float) (mains - 1) * 2.0f - 1.0f;
        branch (-1, side * 1.1f + rng.gauss() * 0.15f, 0.5f + rng.uniform() * 0.25f, 1, 0.1f + rng.uniform() * 0.25f);
    }
}

void PlantView::addPulse (int node, float velocity, double now)
{
    pulses.push_back ({ node, now, 0.4f + 0.6f * juce::jlimit (0.0f, 1.0f, velocity) });
    if (pulses.size() > 24)
        pulses.erase (pulses.begin());
}

void PlantView::paintRoots (juce::Graphics& g) const
{
    for (size_t i = 0; i < roots.size(); ++i)
    {
        if (rootGrowth[i] <= 0.0f)
            continue;

        g.setColour (theme::text.withAlpha (0.13f));
        g.drawLine ({ rootFrom[i], rootTo[i] }, juce::jmax (0.5f, 1.5f - (float) roots[i].depth * 0.25f));
    }
}

void PlantView::paintLights (juce::Graphics& g, double now) const
{
    const auto& nodes = session.getPlant().nodes;

    for (const auto& pulse : pulses)
    {
        if (pulse.node >= (int) nodes.size())
            continue;

        const auto age = (float) (now - pulse.start);

        // Sap: light rises from the seed through every stem up to the bud that plays.
        std::vector<int> chain;
        for (auto n = pulse.node; n >= 0; n = nodes[(size_t) n].parent)
            chain.insert (chain.begin(), n);

        float total = 0.0f;
        for (size_t k = 1; k < chain.size(); ++k)
            total += positions[(size_t) chain[k - 1]].getDistanceFrom (positions[(size_t) chain[k]]);

        const auto travel = juce::jmax (0.12f, total / 1100.0f);
        const auto head = juce::jmin (1.0f, age / travel) * total;
        const auto sapFade = age < travel ? 1.0f : juce::jmax (0.0f, 1.0f - (age - travel) / 0.9f);

        if (sapFade > 0.0f && chain.size() > 1)
        {
            juce::Path sap;
            float walked = 0.0f;
            for (size_t k = 1; k < chain.size() && walked < head; ++k)
            {
                const auto a = positions[(size_t) chain[k - 1]], b = positions[(size_t) chain[k]];
                const auto segment = juce::jmax (1.0f, a.getDistanceFrom (b));
                const auto upTo = juce::jmin (1.0f, (head - walked) / segment);
                sap.startNewSubPath (a);
                for (int s = 1; s <= 12; ++s)
                    sap.lineTo (stemPoint (a, b, upTo * (float) s / 12.0f));
                walked += segment;
            }

            // A soft halo under a bright core.
            g.setColour (theme::text.withAlpha (0.16f * sapFade * pulse.strength));
            g.strokePath (sap, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (theme::text.withAlpha (0.85f * sapFade * pulse.strength));
            g.strokePath (sap, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Roots: a soft wave of light slides down and out, continuously.
        const auto front = age * 150.0f;
        const auto rootFade = juce::jmax (0.0f, 1.0f - age / (float) pulseSeconds);
        for (size_t i = 0; i < roots.size(); ++i)
        {
            if (rootGrowth[i] <= 0.0f)
                continue;

            constexpr int pieces = 8;
            const auto length = roots[i].length * rootDepth;
            for (int s = 0; s < pieces; ++s)
            {
                const auto d = (rootDistance[i] + length * ((float) s + 0.5f) / (float) pieces - front) / 28.0f;
                const auto alpha = std::exp (-d * d) * rootFade * pulse.strength;
                if (alpha < 0.02f)
                    continue;

                const auto from = rootFrom[i] + (rootTo[i] - rootFrom[i]) * ((float) s / (float) pieces);
                const auto to = rootFrom[i] + (rootTo[i] - rootFrom[i]) * ((float) (s + 1) / (float) pieces);
                g.setColour (theme::text.withAlpha (alpha * 0.85f));
                g.drawLine ({ from, to }, juce::jmax (0.5f, 1.5f - (float) roots[i].depth * 0.25f) + 0.6f);
            }
        }
    }
}

void PlantView::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    const auto& nodes = session.getPlant().nodes;
    if (nodes.empty() || positions.size() != nodes.size())
        return;

    const auto now = nowSeconds();

    paintRoots (g);

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

    paintLights (g, now);

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
    const auto groundY = areaBottom + 6.0f;
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
            const juce::Point<float> spot (first + gap * (float) i, areaBottom + rootDepth + 30.0f);
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
    addPulse (node, velocity, nowSeconds());
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
