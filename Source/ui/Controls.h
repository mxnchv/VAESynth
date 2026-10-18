#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Theme.h"

// Arc knob with its name and current value underneath.
class Knob final : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::ParameterID& id,
          const juce::String& labelText, bool bipolar = false)
        : label (labelText),
          parameter (state.getParameter (id.getParamID()))
    {
        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        slider.setMouseDragSensitivity (200);
        slider.getProperties().set ("bipolar", bipolar);
        slider.setTitle (labelText);
        slider.onValueChange = [this] { repaint(); };
        addAndMakeVisible (slider);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id.getParamID(), slider);
        slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
    }

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromBottom (36);
        slider.setBounds (area.withSizeKeepingCentre (56, 56));
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().removeFromBottom (36);

        g.setColour (theme::muted);
        g.setFont (theme::font (12.0f));
        g.drawText (label, area.removeFromTop (17), juce::Justification::centred);

        g.setColour (theme::text);
        g.setFont (theme::font (13.0f));
        g.drawText (parameter->getCurrentValueAsText(), area, juce::Justification::centred);
    }

private:
    juce::Slider slider;
    juce::String label;
    juce::RangedAudioParameter* parameter;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// Row of underlined text options bound to a choice parameter.
class ChoiceButtons final : public juce::Component
{
public:
    ChoiceButtons (juce::AudioProcessorValueTreeState& state, const juce::ParameterID& id,
                   const juce::StringArray& labels, bool small = false)
        : fontHeight (small ? 12.0f : 14.0f),
          gap (small ? 12 : 20)
    {
        for (int i = 0; i < labels.size(); ++i)
        {
            auto* button = buttons.add (new juce::TextButton (labels[i]));
            buttonStyle::set (*button, small ? "small" : "link");
            button->onClick = [this, i] { attachment->setValueAsCompleteGesture ((float) i); };
            addAndMakeVisible (button);
        }

        attachment = std::make_unique<juce::ParameterAttachment> (*state.getParameter (id.getParamID()),
                                                                  [this] (float value) { select (juce::roundToInt (value)); });
        attachment->sendInitialUpdate();
    }

    int getTotalWidth() const
    {
        int total = 0;
        for (auto* button : buttons)
            total += textWidth (*button) + gap;
        return total - gap;
    }

    void resized() override
    {
        auto area = getLocalBounds();
        if (centred)
            area = area.withSizeKeepingCentre (getTotalWidth(), area.getHeight());

        for (auto* button : buttons)
        {
            button->setBounds (area.removeFromLeft (textWidth (*button)));
            area.removeFromLeft (gap);
        }
    }

    void setCentred (bool shouldCentre) { centred = shouldCentre; resized(); }

    std::function<void (int)> onChange;

private:
    int textWidth (juce::TextButton& button) const
    {
        return (int) std::ceil (juce::GlyphArrangement::getStringWidth (theme::font (fontHeight), button.getButtonText())) + 4;
    }

    void select (int index)
    {
        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setToggleState (i == index, juce::dontSendNotification);

        if (onChange)
            onChange (index);
    }

    float fontHeight;
    int gap;
    bool centred = false;
    juce::OwnedArray<juce::TextButton> buttons;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// A knob that can run free (ms, per second...) or locked to the host tempo
// (note values). "Free / Sync" underneath switches which parameter it shows.
class SyncableKnob final : public juce::Component
{
public:
    SyncableKnob (juce::AudioProcessorValueTreeState& state, const juce::ParameterID& freeId,
                  const juce::ParameterID& syncId, const juce::ParameterID& modeId, const juce::String& label)
        : freeKnob (state, freeId, label),
          syncKnob (state, syncId, label),
          mode (state, modeId, { "Free", "Sync" }, true)
    {
        addAndMakeVisible (freeKnob);
        addChildComponent (syncKnob);
        addAndMakeVisible (mode);
        mode.setCentred (true);
        mode.onChange = [this] (int index)
        {
            freeKnob.setVisible (index == 0);
            syncKnob.setVisible (index == 1);
        };
        mode.onChange (juce::roundToInt (state.getRawParameterValue (modeId.getParamID())->load()));
    }

    void resized() override
    {
        auto area = getLocalBounds();
        mode.setBounds (area.removeFromBottom (22));
        freeKnob.setBounds (area);
        syncKnob.setBounds (area);
    }

private:
    Knob freeKnob, syncKnob;
    ChoiceButtons mode;
};

// "Label  < value >" selector for a choice parameter with many options.
class StepSelector final : public juce::Component
{
public:
    StepSelector (juce::AudioProcessorValueTreeState& state, const juce::ParameterID& id, const juce::String& labelText)
        : label (labelText),
          parameter (dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (id.getParamID())))
    {
        jassert (parameter != nullptr);

        for (auto* button : { &previous, &next })
        {
            buttonStyle::set (*button, "icon");
            addAndMakeVisible (button);
        }

        previous.setTitle ("Previous " + labelText);
        next.setTitle ("Next " + labelText);
        previous.onClick = [this] { step (-1); };
        next.onClick = [this] { step (1); };

        attachment = std::make_unique<juce::ParameterAttachment> (*parameter, [this] (float) { repaint(); });
        attachment->sendInitialUpdate();
    }

    void resized() override
    {
        auto area = getLocalBounds().withTrimmedTop (18);
        previous.setBounds (area.removeFromLeft (28));
        next.setBounds (area.removeFromRight (28));
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        g.setColour (theme::muted);
        g.setFont (theme::font (12.0f));
        g.drawText (label, area.removeFromTop (18), juce::Justification::centredLeft);

        g.setColour (theme::text);
        g.setFont (theme::font (14.0f));
        g.drawText (parameter->getCurrentChoiceName(), area.reduced (28, 0), juce::Justification::centred, true);
    }

private:
    void step (int delta)
    {
        const auto count = parameter->choices.size();
        attachment->setValueAsCompleteGesture ((float) (((parameter->getIndex() + delta) % count + count) % count));
    }

    juce::String label;
    juce::AudioParameterChoice* parameter;
    juce::TextButton previous { "<" }, next { ">" };
    std::unique_ptr<juce::ParameterAttachment> attachment;
};
