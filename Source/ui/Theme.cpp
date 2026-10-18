#include "Theme.h"

#include <BinaryData.h>

namespace
{
    juce::Typeface::Ptr regularTypeface()
    {
        static auto typeface = juce::Typeface::createSystemTypefaceFor (BinaryData::JostRegular_ttf, BinaryData::JostRegular_ttfSize);
        return typeface;
    }

    juce::Typeface::Ptr lightTypeface()
    {
        static auto typeface = juce::Typeface::createSystemTypefaceFor (BinaryData::JostLight_ttf, BinaryData::JostLight_ttfSize);
        return typeface;
    }
}

namespace theme
{
    juce::Colour sampleColour (int index)
    {
        static const juce::Colour palette[] {
            juce::Colour (0xfff2a541), juce::Colour (0xff5ab0ff), juce::Colour (0xffc49bff), juce::Colour (0xff4fd1a5),
            juce::Colour (0xffff7ab6), juce::Colour (0xffa3e635), juce::Colour (0xffff8a65), juce::Colour (0xff7dd3fc)
        };

        return palette[(size_t) juce::jmax (0, index) % std::size (palette)];
    }

    juce::Font font (float height)
    {
        return juce::Font (juce::FontOptions (regularTypeface()).withHeight (height));
    }

    juce::Font lightFont (float height)
    {
        return juce::Font (juce::FontOptions (lightTypeface()).withHeight (height));
    }

    void drawSectionTitle (juce::Graphics& g, const juce::String& title, juce::Rectangle<int> area)
    {
        g.setColour (muted);
        g.setFont (juce::Font (juce::FontOptions (regularTypeface()).withHeight (11.0f).withKerningFactor (0.18f)));
        g.drawText (title.toUpperCase(), area, juce::Justification::centredLeft);
    }
}

VAELookAndFeel::VAELookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, theme::background);
    setColour (juce::TextButton::textColourOffId, theme::muted);
    setColour (juce::TextButton::textColourOnId, theme::text);
}

juce::Typeface::Ptr VAELookAndFeel::getTypefaceForFont (const juce::Font&)
{
    return regularTypeface();
}

void VAELookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 3.0f;
    const auto centre = bounds.getCentre();
    const juce::PathStrokeType stroke (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path trackArc;
    trackArc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour (theme::track);
    g.strokePath (trackArc, stroke);

    // Bipolar knobs fill from 12 o'clock; the arc alone shows the value.
    const auto valueAngle = startAngle + sliderPos * (endAngle - startAngle);
    const auto fromAngle = (bool) slider.getProperties().getWithDefault ("bipolar", false)
                             ? (startAngle + endAngle) * 0.5f
                             : startAngle;

    if (std::abs (valueAngle - fromAngle) > 0.01f)
    {
        juce::Path valueArc;
        valueArc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                                juce::jmin (fromAngle, valueAngle), juce::jmax (fromAngle, valueAngle), true);
        g.setColour (theme::text);
        g.strokePath (valueArc, stroke);
    }
}

void VAELookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                           bool isMouseOver, bool)
{
    const auto style = button.getProperties()["style"].toString();
    auto bounds = button.getLocalBounds().toFloat();

    if (style == "pill")
    {
        g.setColour (isMouseOver ? theme::muted : theme::outline);
        g.drawRoundedRectangle (bounds.reduced (0.5f), bounds.getHeight() * 0.5f, 1.0f);
    }
    else if (style == "tab" && button.getToggleState())
    {
        g.setColour (theme::text);
        g.fillRect (bounds.removeFromBottom (2.0f));
    }
}

void VAELookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool isMouseOver, bool)
{
    const auto style = button.getProperties()["style"].toString();
    const auto selected = button.getToggleState() || style == "pill";
    const auto font = getTextButtonFont (button, button.getHeight());

    g.setFont (font);
    g.setColour (selected || isMouseOver ? theme::text : theme::muted);

    auto area = button.getLocalBounds();
    if (style == "tab")
        area.removeFromBottom (6);

    const auto justification = style == "pill" || style == "icon" ? juce::Justification::centred
                                                                  : juce::Justification::centredLeft;
    g.drawText (button.getButtonText(), area, justification);

    if (style == "link" && button.getToggleState())
    {
        const auto textWidth = juce::GlyphArrangement::getStringWidth (font, button.getButtonText());
        g.fillRect (juce::Rectangle<float> (0.0f, (float) area.getCentreY() + font.getHeight() * 0.5f + 3.0f, textWidth, 1.0f));
    }
}

juce::Font VAELookAndFeel::getTextButtonFont (juce::TextButton& button, int)
{
    const auto style = button.getProperties()["style"].toString();
    return theme::font (style == "tab" ? 15.0f : (style == "icon" ? 18.0f : 14.0f));
}
