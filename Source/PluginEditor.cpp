#include "PluginEditor.h"

namespace
{
    constexpr int topHeight = 76;
    constexpr int bottomHeight = 104;
    constexpr int margin = 20;
    constexpr double idleSeconds = 2.6;

    double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }

    void setupSlider (juce::Slider& s, juce::Component& parent)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s.setPopupDisplayEnabled (true, true, nullptr);
        parent.addAndMakeVisible (s);
    }

    const char* helpText =
        "A seed is a sound. It grows into a plant and every bud is a variation of it: "
        "further up, further from the seed. Right is brighter, left is darker. "
        "Sweep across the buds to play them, click one to pick it and play it from your keyboard, "
        "double-click to plant it and grow a new plant from it. "
        "The seeds you planted stay underground: click them to go back.";
}

// ---------------------------------------------------------------- top band

TopBand::TopBand (VAESynthProcessor& p)
    : rootAttachment (p.getState(), ParamIDs::scaleRoot.getParamID(), rootBox),
      scaleAttachment (p.getState(), ParamIDs::scaleType.getParamID(), scaleBox)
{
    rootBox.addItemList (vae::Scale::rootNames(), 1);
    scaleBox.addItemList (vae::Scale::typeNames(), 1);
    rootBox.setSelectedItemIndex ((int) p.getState().getParameterAsValue (ParamIDs::scaleRoot.getParamID()).getValue(), juce::dontSendNotification);
    scaleBox.setSelectedItemIndex ((int) p.getState().getParameterAsValue (ParamIDs::scaleType.getParamID()).getValue(), juce::dontSendNotification);

    buttonStyle::setStrong (newSeedButton);
    for (auto* c : std::initializer_list<juce::Component*> { &rootBox, &scaleBox, &newSeedButton })
        addAndMakeVisible (c);
}

void TopBand::paint (juce::Graphics& g)
{
    g.setColour (theme::text);
    g.setFont (theme::lightFont (21.0f).withExtraKerningFactor (0.45f));
    g.drawText ("VAESYNTH", margin, 0, 260, getHeight() - 12, juce::Justification::bottomLeft);

    theme::drawLabel (g, "Key", rootBox.getBounds().withY (rootBox.getY() - 18).withHeight (14));
    theme::drawLabel (g, "Scale", scaleBox.getBounds().withY (scaleBox.getY() - 18).withHeight (14));
}

void TopBand::resized()
{
    auto row = getLocalBounds().reduced (margin, 0).removeFromBottom (36).withTrimmedBottom (8);
    newSeedButton.setBounds (row.removeFromRight (80));
    row.removeFromRight (24);
    scaleBox.setBounds (row.removeFromRight (150));
    row.removeFromRight (24);
    rootBox.setBounds (row.removeFromRight (56));
}

// ---------------------------------------------------------------- bottom band

BottomBand::BottomBand (VAESynthProcessor& p)
    : wildAttachment (p.getState(), ParamIDs::wild.getParamID(), wild),
      spaceAttachment (p.getState(), ParamIDs::space.getParamID(), space),
      volumeAttachment (p.getState(), ParamIDs::volume.getParamID(), volume)
{
    for (auto* s : { &wild, &space, &volume })
        setupSlider (*s, *this);
    wild.onDragEnd = [this] { if (onWildReleased) onWildReleased(); };

    buttonStyle::setStrong (plantButton);
    for (auto* b : { &plantButton, &backButton, &regrowButton })
        addAndMakeVisible (b);
}

void BottomBand::setInfo (const juce::String& name, const juce::String& detail, bool canPlant, bool canGoBack)
{
    if (name != infoName || detail != infoDetail)
    {
        infoName = name;
        infoDetail = detail;
        repaint();
    }

    if (plantButton.isVisible() != canPlant || backButton.isVisible() != canGoBack)
    {
        plantButton.setVisible (canPlant);
        backButton.setVisible (canGoBack);
        resized();
    }
}

void BottomBand::paint (juce::Graphics& g)
{
    auto info = getLocalBounds().reduced (margin, 0).withTrimmedBottom (16).removeFromBottom (78).removeFromLeft (460);
    g.setColour (theme::text);
    g.setFont (theme::lightFont (19.0f));
    g.drawText (infoName, info.removeFromTop (26), juce::Justification::centredLeft, true);
    g.setColour (theme::muted);
    g.setFont (theme::lightFont (13.0f));
    g.drawText (infoDetail, info.removeFromTop (20), juce::Justification::centredLeft, true);

    for (auto [slider, label] : { std::pair { &wild, "Wild" }, std::pair { &space, "Space" }, std::pair { &volume, "Volume" } })
        theme::drawLabel (g, label, slider->getBounds().withY (slider->getY() - 16).withHeight (14));
}

void BottomBand::resized()
{
    auto area = getLocalBounds().reduced (margin, 0).withTrimmedBottom (16);

    // Only the actions that apply, side by side.
    auto actions = area.withTrimmedTop (area.getHeight() - 30).removeFromLeft (220);
    for (auto [button, width] : { std::pair { &plantButton, 80 }, std::pair { &backButton, 50 } })
    {
        if (! button->isVisible())
            continue;
        button->setBounds (actions.removeFromLeft (width));
        actions.removeFromLeft (20);
    }

    auto controls = area.removeFromRight (juce::jmin (420, area.getWidth() / 2)).removeFromBottom (30);
    regrowButton.setBounds (controls.removeFromRight (60));
    controls.removeFromRight (18);
    const auto sliderWidth = (controls.getWidth() - 36) / 3;
    for (auto* s : { &wild, &space, &volume })
    {
        s->setBounds (controls.removeFromLeft (sliderWidth));
        controls.removeFromLeft (18);
    }
}

// ---------------------------------------------------------------- seed chooser

SeedChooser::SeedChooser()
{
    for (int i = 0; i < (int) vae::library.size(); ++i)
    {
        auto* b = libraryButtons.add (new juce::TextButton (vae::library[(size_t) i].name));
        b->getProperties().set ("fontSize", 22.0);
        buttonStyle::setStrong (*b);
        b->onClick = [this, i] { if (onLibrarySeed) onLibrarySeed (i); };
        addAndMakeVisible (b);
    }

    buttonStyle::setStrong (ownSoundButton);
    ownSoundButton.onClick = [this] { if (onOwnSound) onOwnSound(); };
    addAndMakeVisible (ownSoundButton);
}

void SeedChooser::paint (juce::Graphics& g)
{
    g.fillAll (theme::background.withAlpha (0.9f));

    auto area = card;
    g.setColour (theme::muted);
    g.setFont (theme::font (12.0f).withExtraKerningFactor (0.3f));
    g.drawText ("CHOOSE A SEED", area.removeFromTop (30), juce::Justification::centred);

    area.removeFromTop (60);
    g.drawText ("or", area.removeFromTop (30), juce::Justification::centred);
    area.removeFromTop (40);

    g.setColour (theme::dim);
    g.setFont (theme::lightFont (14.0f));
    g.drawText ("you can also drag a one-shot anywhere on the window", area.removeFromTop (30), juce::Justification::centred);

    area.removeFromTop (14);
    g.setColour (theme::muted);
    g.setFont (theme::lightFont (13.0f));
    g.drawFittedText (helpText, area.removeFromTop (100), juce::Justification::centredTop, 6, 1.0f);
}

void SeedChooser::resized()
{
    card = getLocalBounds().withSizeKeepingCentre (juce::jmin (460, getWidth() - 32), 330);

    // Library names centred in one row.
    const auto font = theme::lightFont (22.0f);
    int total = 0;
    for (auto* b : libraryButtons)
        total += juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, b->getButtonText())) + 26;
    total -= 26;

    auto x = card.getCentreX() - total / 2;
    for (auto* b : libraryButtons)
    {
        const auto w = juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, b->getButtonText())) + 2;
        b->setBounds (x, card.getY() + 40, w, 36);
        x += w + 24;
    }

    const auto ownWidth = 150;
    ownSoundButton.setBounds (card.getCentreX() - ownWidth / 2 + 8, card.getY() + 126, ownWidth, 30);
}

void SeedChooser::mouseDown (const juce::MouseEvent& e)
{
    if (! card.contains (e.getPosition()) && onDismiss)
        onDismiss();
}

// ---------------------------------------------------------------- editor

VAESynthEditor::VAESynthEditor (VAESynthProcessor& p)
    : AudioProcessorEditor (p), processor (p), session (p.getSession()),
      plantView (p), topBand (p), bottomBand (p)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (plantView);
    addAndMakeVisible (topBand);
    addAndMakeVisible (bottomBand);
    addChildComponent (chooser);

    plantView.onSelectionChanged = [this] { updateInfo(); };

    topBand.newSeedButton.onClick = [this] { chooser.setVisible (true); };
    bottomBand.plantButton.onClick = [this] { session.plantNode (session.getSelected()); updateInfo(); };
    bottomBand.backButton.onClick = [this] { session.goTo (session.getCurrentGeneration() - 1); updateInfo(); };
    bottomBand.regrowButton.onClick = [this] { session.regrow(); updateInfo(); };
    bottomBand.onWildReleased = [this] { session.applyWild(); updateInfo(); };

    chooser.onLibrarySeed = [this] (int i) { session.chooseLibrarySeed (i); chooser.setVisible (false); updateInfo(); };
    chooser.onOwnSound = [this] { chooseFile(); };
    chooser.onDismiss = [this] { chooser.setVisible (false); };

    setResizable (true, true);
    setResizeLimits (760, 520, 1800, 1200);
    setSize (980, 640);

    lastActivity = nowSeconds();
    updateInfo();
    startTimerHz (30);
}

VAESynthEditor::~VAESynthEditor()
{
    setLookAndFeel (nullptr);
}

void VAESynthEditor::resized()
{
    auto area = getLocalBounds();
    plantView.setBounds (area);
    chooser.setBounds (area);
    topBand.setBounds (area.removeFromTop (topHeight));
    bottomBand.setBounds (area.removeFromBottom (bottomHeight));
    plantView.setPlantArea ((float) topHeight + 30.0f, (float) (getHeight() - bottomHeight) - PlantView::rootDepth - 50.0f);
}

void VAESynthEditor::paintOverChildren (juce::Graphics& g)
{
    if (! dragging)
        return;

    const auto area = getLocalBounds().reduced (16).toFloat();
    juce::Path outline;
    outline.addRectangle (area);
    juce::Path dashed;
    const float dashes[] { 4.0f, 4.0f };
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
    g.setColour (theme::dim);
    g.fillPath (dashed);

    g.setColour (theme::text);
    g.setFont (theme::font (13.0f).withExtraKerningFactor (0.2f));
    g.drawText ("DROP A ONE-SHOT TO PLANT IT", getLocalBounds(), juce::Justification::centred);
}

void VAESynthEditor::updateInfo()
{
    const auto selected = session.getSelected();
    const auto generation = session.getCurrentGeneration();
    juce::String name, detail;

    if (! error.isEmpty())
    {
        name = "That file could not be read";
        detail = error;
    }
    else if (selected > 0)
    {
        const auto depth = session.getPlant().nodes[(size_t) selected].depth;
        name = session.describeNode (selected);
        detail = juce::String (depth) + (depth == 1 ? " step" : " steps") + " from the seed  ·  your MIDI plays it  ·  double-click plants it";
    }
    else if (session.hasPlant())
    {
        const auto& source = session.getHistory()[(size_t) generation].source;
        name = session.describeNode (0);
        detail = "Seed";
        if (source->kind == vae::SeedSource::Kind::sample)
            detail << "  ·  " << (source->pitched ? juce::MidiMessage::getMidiNoteName (source->rootNote, true, true, 4) : juce::String ("no clear pitch"));
        if (generation > 0)
            detail << "  ·  generation " << (generation + 1);
        detail << "  ·  sweep the buds to hear its variations";
    }

    bottomBand.setInfo (name, detail, selected > 0, generation > 0);
}

void VAESynthEditor::wake()
{
    lastActivity = nowSeconds();
}

void VAESynthEditor::timerCallback()
{
    // Controls fade away when the mouse rests, so only the plant remains.
    const auto mouse = juce::Desktop::getMousePosition();
    if (mouse != lastMouse)
    {
        lastMouse = mouse;
        wake();
    }

    const auto idle = nowSeconds() - lastActivity > idleSeconds && ! chooser.isVisible() && ! dragging;
    const auto target = idle ? 0.0f : 1.0f;
    bandAlpha += (target - bandAlpha) * (idle ? 0.06f : 0.3f);
    topBand.setAlpha (bandAlpha);
    bottomBand.setAlpha (bandAlpha);

    updateInfo();
}

// ---------------------------------------------------------------- your own sound

void VAESynthEditor::loadFile (const juce::File& file)
{
    juce::String loadError;
    if (session.loadSeedFile (file, loadError))
    {
        error.clear();
        chooser.setVisible (false);
    }
    else
    {
        error = loadError;
        juce::Timer::callAfterDelay (5000, [safe = juce::Component::SafePointer<VAESynthEditor> (this)]
        {
            if (safe != nullptr) safe->error.clear();
        });
    }

    updateInfo();
}

void VAESynthEditor::chooseFile()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Choose a one-shot", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.caf");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult().existsAsFile())
                                      loadFile (fc.getResult());
                              });
}

bool VAESynthEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    return ! files.isEmpty();
}

void VAESynthEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    dragging = true;
    repaint();
}

void VAESynthEditor::fileDragExit (const juce::StringArray&)
{
    dragging = false;
    repaint();
}

void VAESynthEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragging = false;
    repaint();
    loadFile (juce::File (files[0]));
}
