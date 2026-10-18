#include "SampleBar.h"

SampleBar::SampleBar (VAESynthProcessor& p) : processor (p)
{
    buttonStyle::set (addButton, "link");
    addButton.setToggleState (true, juce::dontSendNotification);
    addButton.onClick = [this] { chooseFiles(); };
    addAndMakeVisible (addButton);

    buttonStyle::set (rebuildButton, "pill");
    rebuildButton.setTooltip ("Recompute the map for the current samples. Trajectories and automation will sound different.");
    rebuildButton.onClick = [this] { processor.rebuildMap(); };
    addAndMakeVisible (rebuildButton);

    startTimerHz (4);
}

void SampleBar::chooseFiles()
{
    chooser = std::make_unique<juce::FileChooser> ("Add samples", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this] (const juce::FileChooser& fc) { processor.addSampleFiles (fc.getResults()); });
}

void SampleBar::resized()
{
    auto area = getLocalBounds().reduced (28, 0);
    area.removeFromLeft (90);  // "SAMPLES" title

    rebuildButton.setBounds (area.removeFromRight (124).withSizeKeepingCentre (124, 36));
    area.removeFromRight (20);
    statusArea = area.removeFromRight (220);
    area.removeFromRight (16);
    addButton.setBounds (area.removeFromRight (110).withSizeKeepingCentre (110, 36));
    area.removeFromRight (16);
    chipsArea = area;
}

void SampleBar::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    g.setColour (theme::line);
    g.fillRect (0, 0, getWidth(), 1);

    theme::drawSectionTitle (g, "Samples", getLocalBounds().reduced (28, 0).removeFromLeft (90));

    const auto map = processor.getMap();
    auto chips = chipsArea;
    g.setFont (theme::font (14.0f));

    if (map != nullptr)
    {
        const auto& sources = map->getSources();
        for (int i = 0; i < (int) sources.size(); ++i)
        {
            const auto& name = sources[(size_t) i]->name;
            const auto width = juce::roundToInt (juce::GlyphArrangement::getStringWidth (theme::font (14.0f), name)) + 18;
            const auto remaining = (int) sources.size() - i;

            if (width + 60 > chips.getWidth() && remaining > 1)
            {
                g.setColour (theme::muted);
                g.drawText ("+" + juce::String (remaining) + " more", chips, juce::Justification::centredLeft);
                break;
            }

            auto chip = chips.removeFromLeft (width);
            chips.removeFromLeft (18);
            g.setColour (theme::sampleColour (i));
            g.fillEllipse (chip.removeFromLeft (9).withSizeKeepingCentre (9, 9).toFloat());
            chip.removeFromLeft (9);
            g.setColour (theme::text);
            g.drawText (name, chip, juce::Justification::centredLeft, true);
        }
    }

    juce::String status;
    if (processor.isLoading())
        status = "Analysing...";
    else if (auto error = processor.getLastError(); error.isNotEmpty())
        status = error;
    else if (map != nullptr && map->getProjection().valid)
        status = "Map locked";

    g.setColour (theme::dim);
    g.setFont (theme::font (13.0f));
    g.drawText (status, statusArea, juce::Justification::centredRight, true);
}

void SampleBar::timerCallback()
{
    repaint();
}
