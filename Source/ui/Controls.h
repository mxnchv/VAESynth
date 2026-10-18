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
    ChoiceButtons (juce::AudioProcessorValueTreeState& state, const juce::ParameterID& id, const juce::StringArray& labels)
    {
        for (int i = 0; i < labels.size(); ++i)
        {
            auto* button = buttons.add (new juce::TextButton (labels[i]));
            buttonStyle::set (*button, "link");
            button->onClick = [this, i] { attachment->setValueAsCompleteGesture ((float) i); };
            addAndMakeVisible (button);
        }

        attachment = std::make_unique<juce::ParameterAttachment> (*state.getParameter (id.getParamID()),
                                                                  [this] (float value) { select (juce::roundToInt (value)); });
        attachment->sendInitialUpdate();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        for (auto* button : buttons)
        {
            const auto textWidth = (int) std::ceil (juce::GlyphArrangement::getStringWidth (theme::font (14.0f), button->getButtonText()));
            button->setBounds (area.removeFromLeft (textWidth + 4));
            area.removeFromLeft (20);
        }
    }

private:
    void select (int index)
    {
        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setToggleState (i == index, juce::dontSendNotification);
    }

    juce::OwnedArray<juce::TextButton> buttons;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};
