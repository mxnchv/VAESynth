#include "ControlPanel.h"

namespace
{
    constexpr int knobWidth = 92;
    constexpr int knobHeight = 100;
    constexpr int syncKnobHeight = 124;
    constexpr int sectionTitleHeight = 28;

    void placeRow (juce::Rectangle<int>& area, std::initializer_list<juce::Component*> components, int columns, int height = knobHeight)
    {
        auto row = area.removeFromTop (height);
        const auto cell = row.getWidth() / columns;
        for (auto* c : components)
        {
            const auto h = dynamic_cast<SyncableKnob*> (c) != nullptr ? syncKnobHeight : knobHeight;
            c->setBounds (row.removeFromLeft (cell).withSizeKeepingCentre (knobWidth, h).withY (row.getY()));
        }
        area.removeFromTop (16);
    }

    juce::String keyModeHint (int mode)
    {
        switch (mode)
        {
            case 0:  return "Notes transpose each sample from its root note.";
            case 1:  return "Grains keep their own pitch; with a scale they snap to it.";
            default: return "Every pitched grain is retuned to the note you play.";
        }
    }
}

ControlPanel::ControlPanel (VAESynthProcessor& p)
    : processor (p),
      azimuth (p.getState(), ParamIDs::azimuth, "Azimuth", true),
      elevation (p.getState(), ParamIDs::elevation, "Elevation", true),
      radius (p.getState(), ParamIDs::radius, "Radius", true),
      speed (p.getState(), ParamIDs::trajectorySpeed, ParamIDs::speedSync, ParamIDs::speedMode, "Speed"),
      trajectory (p.getState(), ParamIDs::trajectoryMode, { "Manual", "Orbit", "Lissajous" }),
      size (p.getState(), ParamIDs::grainSize, ParamIDs::sizeSync, ParamIDs::sizeMode, "Size"),
      density (p.getState(), ParamIDs::density, ParamIDs::densitySync, ParamIDs::densityMode, "Density"),
      spray (p.getState(), ParamIDs::spray, "Spray"),
      shape (p.getState(), ParamIDs::shape, "Shape"),
      neighbours (p.getState(), ParamIDs::neighbours, "Neighbours"),
      keyMode (p.getState(), ParamIDs::keyMode, { "Transpose", "Texture", "Tune" }),
      scaleRoot (p.getState(), ParamIDs::scaleRoot, "Key"),
      scaleType (p.getState(), ParamIDs::scaleType, "Scale"),
      detune (p.getState(), ParamIDs::detune, "Detune"),
      harmonicFilter (p.getState(), ParamIDs::harmonicFilter, "Harmonic filter"),
      attack (p.getState(), ParamIDs::attack, "Attack"),
      decay (p.getState(), ParamIDs::decay, "Decay"),
      sustain (p.getState(), ParamIDs::sustain, "Sustain"),
      release (p.getState(), ParamIDs::release, "Release"),
      width (p.getState(), ParamIDs::width, "Width"),
      gain (p.getState(), ParamIDs::gain, "Gain", true),
      velocityToRadius (p.getState(), ParamIDs::velocityToRadius, juce::String::fromUTF8 ("Velocity \xe2\x86\x92 Radius")),
      sampleList (p)
{
    const std::pair<const char*, Tab> tabs[] { { "Space", Tab::space }, { "Grains", Tab::grains }, { "Pitch", Tab::pitch },
                                               { "Voice", Tab::voice }, { "Samples", Tab::samples } };

    for (const auto& [name, t] : tabs)
    {
        auto* button = tabButtons.add (new juce::TextButton (name));
        buttonStyle::set (*button, "tab");
        button->onClick = [this, t = t] { showTab (t); };
        addAndMakeVisible (button);
    }

    sampleViewport.setViewedComponent (&sampleList, false);
    sampleViewport.setScrollBarsShown (true, false);
    sampleViewport.setScrollBarThickness (6);

    buttonStyle::set (clearButton, "small");
    clearButton.setToggleState (true, juce::dontSendNotification);
    clearButton.onClick = [this] { processor.clearSamples(); };

    keyMode.onChange = [this] (int) { repaint (hintArea); };

    for (auto* c : std::initializer_list<juce::Component*> { &azimuth, &elevation, &radius, &speed, &trajectory,
                                                             &size, &density, &spray, &shape, &neighbours,
                                                             &keyMode, &scaleRoot, &scaleType, &detune, &harmonicFilter,
                                                             &attack, &decay, &sustain, &release, &width, &gain, &velocityToRadius,
                                                             &sampleViewport, &clearButton })
        addChildComponent (c);

    showTab (Tab::space);
    startTimerHz (10);
}

void ControlPanel::showTab (Tab newTab)
{
    tab = newTab;

    for (int i = 0; i < tabButtons.size(); ++i)
        tabButtons[i]->setToggleState (i == (int) tab, juce::dontSendNotification);

    auto show = [] (std::initializer_list<juce::Component*> components, bool visible)
    {
        for (auto* c : components)
            c->setVisible (visible);
    };

    show ({ &azimuth, &elevation, &radius, &speed, &trajectory }, tab == Tab::space);
    show ({ &size, &density, &spray, &shape, &neighbours }, tab == Tab::grains);
    show ({ &keyMode, &scaleRoot, &scaleType, &detune, &harmonicFilter }, tab == Tab::pitch);
    show ({ &attack, &decay, &sustain, &release, &width, &gain, &velocityToRadius }, tab == Tab::voice);
    show ({ &sampleViewport, &clearButton }, tab == Tab::samples);

    resized();
    repaint();
}

void ControlPanel::resized()
{
    sectionTitles.clear();
    nowPlayingArea = hintArea = samplesHeaderArea = {};

    auto area = getLocalBounds();
    auto tabRow = area.removeFromTop (60).reduced (28, 0).withTrimmedTop (16);
    for (auto* button : tabButtons)
    {
        button->setBounds (tabRow.removeFromLeft (juce::roundToInt (juce::GlyphArrangement::getStringWidth (theme::font (15.0f), button->getButtonText())) + 2));
        tabRow.removeFromLeft (24);
    }

    area.reduce (28, 22);

    auto addTitle = [&] (const juce::String& title)
    {
        sectionTitles.push_back ({ title, area.removeFromTop (sectionTitleHeight) });
    };

    switch (tab)
    {
        case Tab::space:
            placeRow (area, { &azimuth, &elevation, &radius, &speed }, 4, syncKnobHeight);
            addTitle ("Trajectory");
            trajectory.setBounds (area.removeFromTop (32));
            area.removeFromTop (24);
            nowPlayingArea = area;
            break;

        case Tab::grains:
            placeRow (area, { &size, &density, &spray }, 3, syncKnobHeight);
            placeRow (area, { &shape, &neighbours }, 3);
            break;

        case Tab::pitch:
        {
            keyMode.setBounds (area.removeFromTop (32));
            hintArea = area.removeFromTop (24);
            area.removeFromTop (16);

            auto selectors = area.removeFromTop (44);
            scaleRoot.setBounds (selectors.removeFromLeft (110));
            selectors.removeFromLeft (24);
            scaleType.setBounds (selectors.removeFromLeft (200));
            area.removeFromTop (24);

            placeRow (area, { &detune, &harmonicFilter }, 4);
            break;
        }

        case Tab::voice:
            addTitle ("Envelope");
            placeRow (area, { &attack, &decay, &sustain, &release }, 4);
            addTitle ("Output & expression");
            placeRow (area, { &width, &gain, &velocityToRadius }, 4);
            break;

        case Tab::samples:
        {
            samplesHeaderArea = area.removeFromTop (sectionTitleHeight);
            clearButton.setBounds (samplesHeaderArea.withLeft (samplesHeaderArea.getRight() - 60));
            area.removeFromTop (8);
            sampleViewport.setBounds (area);
            sampleList.setSize (area.getWidth() - 8, sampleList.getIdealHeight());
            break;
        }
    }
}

void ControlPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    g.setColour (theme::line);
    g.fillRect (0, 59, getWidth(), 1);

    for (const auto& [title, area] : sectionTitles)
        theme::drawSectionTitle (g, title, area);

    if (tab == Tab::pitch)
    {
        g.setColour (theme::muted);
        g.setFont (theme::font (12.0f));
        g.drawText (keyModeHint (juce::roundToInt (processor.getState().getRawParameterValue (ParamIDs::keyMode.getParamID())->load())),
                    hintArea, juce::Justification::centredLeft, true);
    }

    if (tab == Tab::samples)
    {
        const auto count = shownMap != nullptr ? (int) shownMap->getSources().size() : 0;
        theme::drawSectionTitle (g, "Samples  " + juce::String (count) + " / " + juce::String (VAESynthProcessor::maxSamples),
                                 samplesHeaderArea);

        if (count == 0)
        {
            g.setColour (theme::muted);
            g.setFont (theme::font (13.0f));
            g.drawText ("Drop audio files on the sphere to add samples.",
                        samplesHeaderArea.translated (0, 40), juce::Justification::centredLeft, true);
        }
    }

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
        sampleList.setMap (map);
        if (tab == Tab::samples)
        {
            resized();
            repaint();
        }
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
        const auto& sources = shownMap->getSources();
        const auto k = (size_t) juce::roundToInt (processor.getState().getRawParameterValue (ParamIDs::neighbours.getParamID())->load());

        bool anySolo = false;
        for (const auto& source : sources)
            anySolo = anySolo || processor.isSoloed (source->slot);

        auto audible = [&] (size_t sourceIndex)
        {
            const auto slot = sources[sourceIndex]->slot;
            return anySolo ? processor.isSoloed (slot) : ! processor.isMuted (slot);
        };

        std::vector<std::pair<float, size_t>> distances;
        distances.reserve (grains.size());
        for (size_t i = 0; i < grains.size(); ++i)
            if (audible ((size_t) grains[i].source))
                distances.push_back ({ (grains[i].position - point).lengthSquared(), i });

        const auto count = juce::jmin (k, distances.size());
        std::partial_sort (distances.begin(), distances.begin() + (std::ptrdiff_t) count, distances.end());

        // Same rule as the engine: nearest grains weighted by distance, blended
        // towards the whole map as the cursor approaches the centre.
        std::vector<float> nearShare (sources.size(), 0.0f), overallShare (sources.size(), 0.0f);
        float total = 0.0f;
        for (size_t i = 0; i < count; ++i)
        {
            const auto w = 1.0f / (std::sqrt (distances[i].first) + 0.05f);
            nearShare[(size_t) grains[distances[i].second].source] += w;
            total += w;
        }

        for (const auto& [d, index] : distances)
            overallShare[(size_t) grains[index].source] += 1.0f / (float) juce::jmax<size_t> (1, distances.size());

        const auto blend = juce::jmin (1.0f, distance);
        for (size_t s = 0; s < sources.size(); ++s)
            mix.push_back ({ sources[s]->name, theme::sampleColour (sources[s]->slot),
                             blend * (total > 0.0f ? nearShare[s] / total : 0.0f) + (1.0f - blend) * overallShare[s] });

        std::sort (mix.begin(), mix.end(), [] (const MixRow& a, const MixRow& b) { return a.share > b.share; });
        if (mix.size() > 6)
            mix.resize (6);
    }

    repaint (nowPlayingArea.expanded (0, 40));
}
