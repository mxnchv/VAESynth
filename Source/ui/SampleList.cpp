#include "SampleList.h"

namespace
{
    constexpr int noteWidth = 40;
    constexpr int iconWidth = 28;
    constexpr int toggleWidth = 26;

    std::unique_ptr<juce::TextButton> makeButton (const juce::String& text, const char* style, const juce::String& title)
    {
        auto button = std::make_unique<juce::TextButton> (text);
        buttonStyle::set (*button, style);
        button->setTitle (title);
        return button;
    }
}

SampleList::SampleList (VAESynthProcessor& p) : processor (p) {}

void SampleList::setMap (const vae::GrainMap::Ptr& map)
{
    removeAllChildren();
    rows.clear();

    if (map != nullptr)
    {
        for (const auto& source : map->getSources())
        {
            const auto slot = source->slot;
            const auto& name = source->name;

            Row row { source,
                      makeButton (juce::String::fromUTF8 ("\xe2\x88\x92"), "icon", "Lower root note of " + name),
                      makeButton ("+", "icon", "Raise root note of " + name),
                      makeButton ("M", "toggle", "Mute " + name),
                      makeButton ("S", "toggle", "Solo " + name),
                      makeButton (juce::String::fromUTF8 ("\xc3\x97"), "icon", "Remove " + name) };

            row.down->onClick = [this, slot] { processor.setRootNote (slot, processor.getRootNote (slot) - 1); repaint(); };
            row.up->onClick   = [this, slot] { processor.setRootNote (slot, processor.getRootNote (slot) + 1); repaint(); };
            row.mute->onClick = [this, slot] { processor.setMuted (slot, ! processor.isMuted (slot)); updateToggles(); };
            row.solo->onClick = [this, slot] { processor.setSoloed (slot, ! processor.isSoloed (slot)); updateToggles(); };
            row.remove->onClick = [this, source] { processor.removeSample (source); };

            for (auto* button : { row.down.get(), row.up.get(), row.mute.get(), row.solo.get(), row.remove.get() })
                addAndMakeVisible (button);

            rows.push_back (std::move (row));
        }
    }

    updateToggles();
    setSize (getWidth(), getIdealHeight());
    resized();
}

void SampleList::updateToggles()
{
    for (auto& row : rows)
    {
        row.mute->setToggleState (processor.isMuted (row.source->slot), juce::dontSendNotification);
        row.solo->setToggleState (processor.isSoloed (row.source->slot), juce::dontSendNotification);
    }

    repaint();
}

void SampleList::resized()
{
    auto area = getLocalBounds();

    for (auto& row : rows)
    {
        auto r = area.removeFromTop (rowHeight);
        row.remove->setBounds (r.removeFromRight (iconWidth));
        row.solo->setBounds (r.removeFromRight (toggleWidth));
        row.mute->setBounds (r.removeFromRight (toggleWidth));
        r.removeFromRight (8);
        row.up->setBounds (r.removeFromRight (iconWidth));
        r.removeFromRight (noteWidth);
        row.down->setBounds (r.removeFromRight (iconWidth));
    }
}

void SampleList::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();
    bool anySolo = false;
    for (const auto& row : rows)
        anySolo = anySolo || processor.isSoloed (row.source->slot);

    for (const auto& row : rows)
    {
        const auto slot = row.source->slot;
        const auto audible = anySolo ? processor.isSoloed (slot) : ! processor.isMuted (slot);

        auto r = area.removeFromTop (rowHeight);
        r.removeFromRight (iconWidth + 2 * toggleWidth + 8);
        auto note = r.removeFromRight (iconWidth + noteWidth).withTrimmedRight (iconWidth);
        r.removeFromRight (iconWidth);

        g.setColour (theme::sampleColour (slot).withMultipliedAlpha (audible ? 1.0f : 0.35f));
        g.fillEllipse (r.removeFromLeft (8).withSizeKeepingCentre (8, 8).toFloat());
        r.removeFromLeft (10);

        g.setColour (audible ? theme::text : theme::muted);
        g.setFont (theme::font (14.0f));
        g.drawText (row.source->name, r, juce::Justification::centredLeft, true);

        g.setColour (theme::text);
        g.drawText (juce::MidiMessage::getMidiNoteName (processor.getRootNote (slot), true, true, 3), note, juce::Justification::centred);

        g.setColour (theme::line);
        g.fillRect (0, r.getBottom() - 1, getWidth(), 1);
    }
}
