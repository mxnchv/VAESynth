#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "ui/PlantView.h"
#include "ui/Theme.h"

// Title, key and scale.
class TopBand final : public juce::Component
{
public:
    explicit TopBand (VAESynthProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

    juce::TextButton newSeedButton { "New seed" };

private:
    juce::ComboBox rootBox, scaleBox;
    juce::AudioProcessorValueTreeState::ComboBoxAttachment rootAttachment, scaleAttachment;
};

// What is picked, what to do with it, and the few sound controls.
class BottomBand final : public juce::Component
{
public:
    explicit BottomBand (VAESynthProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void setInfo (const juce::String& name, const juce::String& detail, bool canPlant, bool canGoBack);

    juce::TextButton plantButton { "Plant this" }, backButton { "Back" }, regrowButton { "Regrow" };

private:
    juce::Slider wild, space, volume;
    juce::AudioProcessorValueTreeState::SliderAttachment wildAttachment, spaceAttachment, volumeAttachment;
    juce::String infoName, infoDetail;
};

// Pick a library seed or use your own sound.
class SeedChooser final : public juce::Component
{
public:
    SeedChooser();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    std::function<void (int)> onLibrarySeed;
    std::function<void()> onOwnSound, onDismiss;

private:
    juce::OwnedArray<juce::TextButton> libraryButtons;
    juce::TextButton ownSoundButton { "Use your own sound" };
    juce::Rectangle<int> card;
};

class VAESynthEditor final : public juce::AudioProcessorEditor,
                             public juce::FileDragAndDropTarget,
                             private juce::Timer
{
public:
    explicit VAESynthEditor (VAESynthProcessor&);
    ~VAESynthEditor() override;

    void paint (juce::Graphics&) override {}
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    void timerCallback() override;
    void updateInfo();
    void loadFile (const juce::File&);
    void chooseFile();
    void wake();

    VAESynthProcessor& processor;
    PlantSession& session;
    VAELookAndFeel lookAndFeel;

    PlantView plantView;
    TopBand topBand;
    BottomBand bottomBand;
    SeedChooser chooser;
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::String error;
    bool dragging = false;
    juce::Point<int> lastMouse;
    double lastActivity = 0.0;
    float bandAlpha = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VAESynthEditor)
};
