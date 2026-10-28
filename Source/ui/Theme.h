#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// mxnchv look: black, whites at different opacities, Jost, thin lines.
namespace theme
{
    inline const juce::Colour background { 0xff000000 };
    inline const juce::Colour text       { 0xfff2f2f2 };
    inline const juce::Colour dim        = text.withAlpha (0.62f);
    inline const juce::Colour muted      = text.withAlpha (0.42f);
    inline const juce::Colour line       = text.withAlpha (0.16f);

    juce::Font font (float height);
    juce::Font lightFont (float height);

    // Small spaced capitals above a control ("WILD").
    void drawLabel (juce::Graphics&, const juce::String&, juce::Rectangle<int>);
}

class VAELookAndFeel final : public juce::LookAndFeel_V4
{
public:
    VAELookAndFeel();

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override {}
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool isMouseOver, bool isButtonDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;
};

// Text buttons are plain words. "strong" ones are always bright and underlined
// faintly; the rest are dim until hovered.
namespace buttonStyle
{
    inline void setStrong (juce::Button& b) { b.getProperties().set ("strong", true); }
}
