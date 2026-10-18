#include "ControlPanel.h"

namespace
{
    constexpr int knobWidth = 92;
    constexpr int knobHeight = 100;
    constexpr int sectionTitleHeight = 28;

    void placeRow (juce::Rectangle<int>& area, std::initializer_list<juce::Component*> components, int columns)
    {
        auto row = area.removeFromTop (knobHeight);
        const auto cell = row.getWidth() / columns;
        for (auto* c : components)
            c->setBounds (row.removeFromLeft (cell).withSizeKeepingCentre (knobWidth, knobHeight));
        area.removeFromTop (16);
    }
}

//==============================================================================
RootNoteList::RootNoteList (VAESynthProcessor& p) : processor (p) {}

void RootNoteList::setMap (vae::GrainMap::Ptr map)
{
    for (auto& row : rows)
    {
        removeChildComponent (row.down.get());
        removeChildComponent (row.up.get());
    }

    rows.clear();

    if (map != nullptr)
    {
        const auto& sources = map->getSources();
        for (int i = 0; i < (int) sources.size(); ++i)
        {
            Row row { sources[(size_t) i]->name, theme::sampleColour (i),
                      std::make_unique<juce::TextButton> (juce::String::fromUTF8 ("\xe2\x88\x92")),
                      std::make_unique<juce::TextButton> ("+") };

            for (auto* button : { row.down.get(), row.up.get() })
            {
                buttonStyle::set (*button, "icon");
                addAndMakeVisible (button);
            }

            row.down->setTitle ("Lower root note of " + row.name);
            row.up->setTitle ("Raise root note of " + row.name);
            row.down->onClick = [this, i] { processor.setRootNote (i, processor.getRootNote (i) - 1); repaint(); };
            row.up->onClick   = [this, i] { processor.setRootNote (i, processor.getRootNote (i) + 1); repaint(); };
            rows.push_back (std::move (row));
        }
    }

    resized();
    repaint();
}

void RootNoteList::resized()
{
    auto area = getLocalBounds();
    for (auto& row : rows)
    {
        auto r = area.removeFromTop (rowHeight);
        row.up->setBounds (r.removeFromRight (32));
        r.removeFromRight (44);
        row.down->setBounds (r.removeFromRight (32));
    }
}

void RootNoteList::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();

    for (int i = 0; i < (int) rows.size(); ++i)
    {
        auto r = area.removeFromTop (rowHeight);
        auto controls = r.removeFromRight (108);

        g.setColour (rows[(size_t) i].colour);
        g.fillEllipse (r.removeFromLeft (8).withSizeKeepingCentre (8, 8).toFloat());
        r.removeFromLeft (12);

        g.setColour (theme::text);
        g.setFont (theme::font (14.0f));
        g.drawText (rows[(size_t) i].name, r, juce::Justification::centredLeft, true);

        controls.removeFromRight (32);
        g.drawText (juce::MidiMessage::getMidiNoteName (processor.getRootNote (i), true, true, 3),
                    controls.removeFromRight (44), juce::Justification::centred);
    }
}

//==============================================================================
ControlPanel::ControlPanel (VAESynthProcessor& p)
    : processor (p),
      azimuth (p.getState(), ParamIDs::azimuth, "Azimuth", true),
      elevation (p.getState(), ParamIDs::elevation, "Elevation", true),
      radius (p.getState(), ParamIDs::radius, "Radius", true),
      speed (p.getState(), ParamIDs::trajectorySpeed, "Speed"),
      trajectory (p.getState(), ParamIDs::trajectoryMode, { "Manual", "Orbit", "Lissajous" }),
      size (p.getState(), ParamIDs::grainSize, "Size"),
      density (p.getState(), ParamIDs::density, "Density"),
      spray (p.getState(), ParamIDs::spray, "Spray"),
      shape (p.getState(), ParamIDs::shape, "Shape"),
      neighbours (p.getState(), ParamIDs::neighbours, "Neighbours"),
      keyMode (p.getState(), ParamIDs::keyMode, { "Transpose", "Texture" }),
      detune (p.getState(), ParamIDs::detune, "Detune"),
      rootNotes (p),
      attack (p.getState(), ParamIDs::attack, "Attack"),
      decay (p.getState(), ParamIDs::decay, "Decay"),
      sustain (p.getState(), ParamIDs::sustain, "Sustain"),
      release (p.getState(), ParamIDs::release, "Release"),
      width (p.getState(), ParamIDs::width, "Width"),
      gain (p.getState(), ParamIDs::gain, "Gain", true),
      velocityToRadius (p.getState(), ParamIDs::velocityToRadius, juce::String::fromUTF8 ("Velocity \xe2\x86\x92 Radius"))
{
    const std::pair<const char*, Tab> tabs[] { { "Space", Tab::space }, { "Grains", Tab::grains },
                                               { "Pitch", Tab::pitch }, { "Voice", Tab::voice } };

    for (const auto& [name, t] : tabs)
    {
        auto* button = tabButtons.add (new juce::TextButton (name));
        buttonStyle::set (*button, "tab");
        button->onClick = [this, t = t] { showTab (t); };
        addAndMakeVisible (button);
    }

    for (auto* c : std::initializer_list<juce::Component*> { &azimuth, &elevation, &radius, &speed, &trajectory,
                                                             &size, &density, &spray, &shape, &neighbours,
                                                             &keyMode, &detune, &rootNotes,
                                                             &attack, &decay, &sustain, &release, &width, &gain, &velocityToRadius })
        addChildComponent (c);

    showTab (Tab::space);
    startTimerHz (10);
}

void ControlPanel::showTab (Tab newTab)
{
    tab = newTab;

    for (int i = 0; i < tabButtons.size(); ++i)
        tabButtons[i]->setToggleState (i == (int) tab, juce::dontSendNotification);

    const auto space = tab == Tab::space, grains = tab == Tab::grains, pitch = tab == Tab::pitch, voice = tab == Tab::voice;

    for (auto* c : { (juce::Component*) &azimuth, (juce::Component*) &elevation, (juce::Component*) &radius,
                     (juce::Component*) &speed, (juce::Component*) &trajectory })
        c->setVisible (space);

    for (auto* c : { &size, &density, &spray, &shape, &neighbours })
        c->setVisible (grains);

    keyMode.setVisible (pitch);
    detune.setVisible (pitch);
    rootNotes.setVisible (pitch);

    for (auto* c : { &attack, &decay, &sustain, &release, &width, &gain, &velocityToRadius })
        c->setVisible (voice);

    resized();
    repaint();
}

void ControlPanel::resized()
{
    sectionTitles.clear();
    nowPlayingArea = {};

    auto area = getLocalBounds();
    auto tabRow = area.removeFromTop (60).reduced (28, 0).withTrimmedTop (16);
    for (auto* button : tabButtons)
    {
        button->setBounds (tabRow.removeFromLeft (juce::roundToInt (juce::GlyphArrangement::getStringWidth (theme::font (15.0f), button->getButtonText())) + 2));
        tabRow.removeFromLeft (28);
    }

    area.reduce (28, 22);

    auto addTitle = [&] (const juce::String& title)
    {
        sectionTitles.push_back ({ title, area.removeFromTop (sectionTitleHeight) });
    };

    switch (tab)
    {
        case Tab::space:
            placeRow (area, { &azimuth, &elevation, &radius, &speed }, 4);
            addTitle ("Trajectory");
            trajectory.setBounds (area.removeFromTop (32));
            area.removeFromTop (24);
            nowPlayingArea = area;
            break;

        case Tab::grains:
            placeRow (area, { &size, &density, &spray }, 3);
            placeRow (area, { &shape, &neighbours }, 3);
            break;

        case Tab::pitch:
            keyMode.setBounds (area.removeFromTop (32));
            area.removeFromTop (16);
            placeRow (area, { &detune }, 4);
            addTitle ("Root note per sample");
            rootNotes.setBounds (area);
            break;

        case Tab::voice:
            addTitle ("Envelope");
            placeRow (area, { &attack, &decay, &sustain, &release }, 4);
            addTitle ("Output & expression");
            placeRow (area, { &width, &gain, &velocityToRadius }, 4);
            break;
    }
}

void ControlPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    g.setColour (theme::line);
    g.fillRect (0, 59, getWidth(), 1);

    for (const auto& [title, area] : sectionTitles)
        theme::drawSectionTitle (g, title, area);

    if (tab != Tab::space || nowPlayingArea.isEmpty())
        return;

    auto area = nowPlayingArea;
    g.setColour (theme::line);
    g.fillRect (area.removeFromTop (1));
    area.removeFromTop (16);

    auto header = area.removeFromTop (22);
    theme::drawSectionTitle (g, "Now playing", header);
    g.setColour (theme::text);
    g.setFont (theme::font (14.0f));
    g.drawText (zone, header, juce::Justification::centredRight);
    area.removeFromTop (10);

    for (const auto& row : mix)
    {
        auto r = area.removeFromTop (26);
        auto label = r.removeFromLeft (150);
        auto value = r.removeFromRight (48);

        g.setColour (row.colour);
        g.fillEllipse (label.removeFromLeft (8).withSizeKeepingCentre (8, 8).toFloat());
        label.removeFromLeft (8);
        g.setColour (theme::text);
        g.setFont (theme::font (13.0f));
        g.drawText (row.name, label, juce::Justification::centredLeft, true);

        g.setColour (theme::dim);
        g.drawText (juce::String (juce::roundToInt (row.share * 100.0f)) + " %", value, juce::Justification::centredRight);

        auto bar = r.reduced (10, 0).withSizeKeepingCentre (r.getWidth() - 20, 2).toFloat();
        g.setColour (theme::track);
        g.fillRect (bar);
        g.setColour (row.colour);
        g.fillRect (bar.withWidth (bar.getWidth() * row.share));
    }
}

void ControlPanel::timerCallback()
{
    auto map = processor.getMap();

    if (map != shownMap)
    {
        shownMap = map;
        rootNotes.setMap (map);
    }

    if (tab == Tab::space)
        updateNowPlaying();
}

void ControlPanel::updateNowPlaying()
{
    const auto liveCursor = processor.getLiveCursor();
    const auto point = vae::directionFromAngles (liveCursor.azimuth, liveCursor.elevation) * liveCursor.radius;
    const auto distance = std::abs (liveCursor.radius);

    zone = distance < 0.35f ? "Core" : (distance <= 1.05f ? "Surface" : "Extrapolation");
    if (liveCursor.radius < 0.0f && distance >= 0.35f)
        zone << juce::String::fromUTF8 (" \xc2\xb7 antipode");

    mix.clear();

    if (shownMap != nullptr && ! shownMap->getGrains().empty())
    {
        const auto& grains = shownMap->getGrains();
        const auto numSources = shownMap->getSources().size();
        const auto k = (size_t) juce::roundToInt (processor.getState().getRawParameterValue (ParamIDs::neighbours.getParamID())->load());

        std::vector<std::pair<float, size_t>> distances;
        distances.reserve (grains.size());
        for (size_t i = 0; i < grains.size(); ++i)
            distances.push_back ({ (grains[i].position - point).lengthSquared(), i });

        const auto count = juce::jmin (k, distances.size());
        std::partial_sort (distances.begin(), distances.begin() + (std::ptrdiff_t) count, distances.end());

        // Same rule as the engine: nearest grains weighted by distance, blended
        // towards the whole map as the cursor approaches the centre.
        std::vector<float> nearShare (numSources, 0.0f), overallShare (numSources, 0.0f);
        float total = 0.0f;
        for (size_t i = 0; i < count; ++i)
        {
            const auto w = 1.0f / (std::sqrt (distances[i].first) + 0.05f);
            nearShare[(size_t) grains[distances[i].second].source] += w;
            total += w;
        }

        for (const auto& grain : grains)
            overallShare[(size_t) grain.source] += 1.0f / (float) grains.size();

        const auto blend = juce::jmin (1.0f, distance);
        for (size_t s = 0; s < numSources; ++s)
            mix.push_back ({ shownMap->getSources()[s]->name, theme::sampleColour ((int) s),
                             blend * (total > 0.0f ? nearShare[s] / total : 0.0f) + (1.0f - blend) * overallShare[s] });

        std::sort (mix.begin(), mix.end(), [] (const MixRow& a, const MixRow& b) { return a.share > b.share; });
        if (mix.size() > 6)
            mix.resize (6);
    }

    repaint (nowPlayingArea.expanded (0, 40));
}
