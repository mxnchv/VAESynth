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
    juce::Font font (float height)
    {
        return juce::Font (juce::FontOptions (regularTypeface()).withHeight (height));
    }

    juce::Font lightFont (float height)
    {
        return juce::Font (juce::FontOptions (lightTypeface()).withHeight (height));
    }

    void drawLabel (juce::Graphics& g, const juce::String& label, juce::Rectangle<int> area)
    {
        g.setColour (muted);
        g.setFont (juce::Font (juce::FontOptions (regularTypeface()).withHeight (11.0f).withKerningFactor (0.2f)));
        g.drawText (label.toUpperCase(), area, juce::Justification::centredLeft);
    }
}

VAELookAndFeel::VAELookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, theme::background);
    setColour (juce::ComboBox::textColourId, theme::text);
    setColour (juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::PopupMenu::backgroundColourId, theme::background);
    setColour (juce::PopupMenu::textColourId, theme::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::text.withAlpha (0.12f));
    setColour (juce::PopupMenu::highlightedTextColourId, theme::text);
    setColour (juce::Label::textColourId, theme::text);
}

juce::Typeface::Ptr VAELookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    return f.getTypefaceStyle() == "Light" ? lightTypeface() : regularTypeface();
}

void VAELookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float, float, juce::Slider::SliderStyle, juce::Slider&)
{
    const auto centreY = (float) y + (float) height * 0.5f;

    g.setColour (theme::line);
    g.fillRect ((float) x, centreY - 0.5f, (float) width, 1.0f);

    g.setColour (theme::text);
    g.fillEllipse (sliderPos - 4.5f, centreY - 4.5f, 9.0f, 9.0f);
}

void VAELookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool isMouseOver, bool)
{
    const auto strong = (bool) button.getProperties().getWithDefault ("strong", false);
    const auto font = getTextButtonFont (button, button.getHeight());
    const auto bright = strong || isMouseOver || button.getToggleState();

    g.setFont (font);
    g.setColour (bright ? theme::text : theme::dim);
    g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centredLeft);

    const auto textWidth = juce::GlyphArrangement::getStringWidth (font, button.getButtonText());
    const auto underlineY = (float) button.getHeight() * 0.5f + font.getHeight() * 0.5f + 2.0f;

    if (isMouseOver || button.getToggleState())
    {
        g.setColour (theme::text);
        g.fillRect (0.0f, underlineY, textWidth, 1.0f);
    }
    else if (strong)
    {
        g.setColour (theme::line);
        g.fillRect (0.0f, underlineY, textWidth, 1.0f);
    }
}

juce::Font VAELookAndFeel::getTextButtonFont (juce::TextButton& button, int)
{
    return theme::lightFont ((float) button.getProperties().getWithDefault ("fontSize", 15.0));
}

void VAELookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                   int, int, int, int, juce::ComboBox&)
{
    g.setColour (theme::line);
    g.fillRect (0.0f, (float) height - 1.0f, (float) width, 1.0f);

    // A small chevron at the right.
    juce::Path chevron;
    const auto cx = (float) width - 6.0f, cy = (float) height * 0.5f;
    chevron.startNewSubPath (cx - 3.5f, cy - 1.5f);
    chevron.lineTo (cx, cy + 2.0f);
    chevron.lineTo (cx + 3.5f, cy - 1.5f);
    g.setColour (theme::dim);
    g.strokePath (chevron, juce::PathStrokeType (1.0f));
}

juce::Font VAELookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return theme::lightFont (15.0f);
}

void VAELookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (0, 0, box.getWidth() - 16, box.getHeight() - 1);
    label.setBorderSize ({ 0, 0, 0, 0 });
    label.setFont (getComboBoxFont (box));
}

juce::Font VAELookAndFeel::getPopupMenuFont()
{
    return theme::lightFont (15.0f);
}
