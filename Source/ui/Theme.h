#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace theme
{
    inline const juce::Colour background { 0xff000000 };
    inline const juce::Colour line       { 0xff1e1e1e };
    inline const juce::Colour text       { 0xfff2f2f2 };
    inline const juce::Colour dim        { 0xffb5b5b5 };
    inline const juce::Colour muted      { 0xff8a8a8a };
    inline const juce::Colour outline    { 0xff3a3a3a };
    inline const juce::Colour track      { 0xff2e2e2e };

    // Colour is reserved for telling samples apart; everything else is greyscale.
    juce::Colour sampleColour (int index);

    juce::Font font (float height);
    juce::Font lightFont (float height);

    // Small caps-style section heading ("NOW PLAYING").
    void drawSectionTitle (juce::Graphics&, const juce::String&, juce::Rectangle<int>);
}

class VAELookAndFeel final : public juce::LookAndFeel_V4
{
public:
    VAELookAndFeel();

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool isMouseOver, bool isButtonDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool isMouseOver, bool isButtonDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
};

// Button styles, set as a "style" property on a TextButton:
//   "tab"  - panel tab, underlined across its width when selected
//   "link" - text option, underlined under the text when selected
//   "pill" - outlined rounded action button
//   "icon" - plain text action (e.g. "-" / "+")
//   "small"  - like "link", smaller (Free / Sync under a knob)
//   "toggle" - small centred label, bright when on (M / S)
namespace buttonStyle
{
    inline void set (juce::Button& b, const char* style) { b.getProperties().set ("style", style); }
}
