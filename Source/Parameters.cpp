#include "Parameters.h"
#include "engine/Scale.h"

#include <iterator>

namespace
{
    using Float = juce::AudioParameterFloat;
    using Attributes = juce::AudioParameterFloatAttributes;

    Attributes withText (std::function<juce::String (float)> toText)
    {
        return Attributes().withStringFromValueFunction ([toText] (float v, int) { return toText (v); });
    }

    juce::String milliseconds (float v)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String (juce::roundToInt (v)) + " ms";
    }

    juce::String percent (float v)
    {
        return juce::String (juce::roundToInt (v * 100.0f)) + " %";
    }

    juce::NormalisableRange<float> skewed (float min, float max, float centre)
    {
        juce::NormalisableRange<float> range { min, max };
        range.setSkewForCentre (centre);
        return range;
    }
}

namespace NoteDivisions
{
    const juce::StringArray& names()
    {
        static const juce::StringArray list { "1/1", "1/2", "1/2T", "1/4.", "1/4", "1/4T", "1/8.", "1/8", "1/8T",
                                              "1/16.", "1/16", "1/16T", "1/32", "1/32T", "1/64" };
        return list;
    }

    double beats (int index)
    {
        static constexpr double values[] { 4.0, 2.0, 4.0 / 3.0, 1.5, 1.0, 2.0 / 3.0, 0.75, 0.5, 1.0 / 3.0,
                                           0.375, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0, 0.0625 };
        return values[(size_t) juce::jlimit (0, (int) std::size (values) - 1, index)];
    }
}

namespace CycleLengths
{
    const juce::StringArray& names()
    {
        static const juce::StringArray list { "1/4 bar", "1/2 bar", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars", "32 bars" };
        return list;
    }

    double bars (int index)
    {
        static constexpr double values[] { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0 };
        return values[(size_t) juce::jlimit (0, (int) std::size (values) - 1, index)];
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    const juce::StringArray clockModes { "Free", "Sync" };

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto degrees = [] (float v) { return juce::String (juce::roundToInt (v)) + juce::String::fromUTF8 ("\xc2\xb0"); };

    layout.add (std::make_unique<Float> (ParamIDs::azimuth, "Azimuth", juce::NormalisableRange<float> { -180.0f, 180.0f }, 0.0f, withText (degrees)));
    layout.add (std::make_unique<Float> (ParamIDs::elevation, "Elevation", juce::NormalisableRange<float> { -90.0f, 90.0f }, 0.0f, withText (degrees)));
    layout.add (std::make_unique<Float> (ParamIDs::radius, "Radius", juce::NormalisableRange<float> { -1.5f, 1.5f }, 1.0f,
                                         withText ([] (float v) { return (v > 0.0f ? "+" : "") + juce::String (v, 2) + " R"; })));

    layout.add (std::make_unique<juce::AudioParameterChoice> (ParamIDs::trajectoryMode, "Trajectory",
                                                              juce::StringArray { "Manual", "Orbit", "Lissajous" }, 0));
    layout.add (std::make_unique<Float> (ParamIDs::trajectorySpeed, "Speed", skewed (0.05f, 4.0f, 1.0f), 1.0f,
                                         withText ([] (float v) { return juce::String (v, 2) + "x"; })));
    layout.add (std::make_unique<juce::AudioParameterChoice> (ParamIDs::speedMode, "Speed mode", clockModes, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (ParamIDs::speedSync, "Speed (synced)", CycleLengths::names(), 4));

    layout.add (std::make_unique<Float> (ParamIDs::grainSize, "Size", skewed (5.0f, 500.0f, 80.0f), 80.0f, withText (milliseconds)));
    layout.add (std::make_unique<juce::AudioParameterChoice> (ParamIDs::sizeMode, "Size mode", clockModes, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (ParamIDs::sizeSync, "Size (synced)", NoteDivisions::names(), 10));
    layout.add (std::make_unique<Float> (ParamIDs::density, "Density", skewed (1.0f, 200.0f, 24.0f), 24.0f,
                                         withText ([] (float v) { return juce::String (juce::roundToInt (v)) + " /s"; })));
    layout.add (std::make_unique<juce::AudioParameterChoice> (ParamIDs::densityMode, "Density mode", clockModes, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (ParamIDs::densitySync, "Density (synced)", NoteDivisions::names(), 10));
    layout.add (std::make_unique<Float> (ParamIDs::spray, "Spray", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.15f, withText (percent)));
    layout.add (std::make_unique<Float> (ParamIDs::shape, "Shape", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.2f,
                                         withText ([] (float v) { return v < 0.34f ? juce::String ("Soft") : (v < 0.67f ? juce::String ("Medium") : juce::String ("Percussive")); })));
    layout.add (std::make_unique<juce::AudioParameterInt> (ParamIDs::neighbours, "Neighbours", 1, 16, 6));

    layout.add (std::make_unique<juce::AudioParameterChoice> (ParamIDs::keyMode, "Key mode",
                                                              juce::StringArray { "Transpose", "Texture", "Tune" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (ParamIDs::scaleRoot, "Scale root", vae::Scale::rootNames(), 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (ParamIDs::scaleType, "Scale", vae::Scale::typeNames(), 0));
    layout.add (std::make_unique<Float> (ParamIDs::harmonicFilter, "Harmonic filter", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, withText (percent)));
    layout.add (std::make_unique<Float> (ParamIDs::detune, "Detune", juce::NormalisableRange<float> { 0.0f, 100.0f }, 8.0f,
                                         withText ([] (float v) { return juce::String::fromUTF8 ("\xc2\xb1 ") + juce::String (juce::roundToInt (v)) + " ct"; })));

    layout.add (std::make_unique<Float> (ParamIDs::attack, "Attack", skewed (0.0f, 2000.0f, 200.0f), 10.0f, withText (milliseconds)));
    layout.add (std::make_unique<Float> (ParamIDs::decay, "Decay", skewed (0.0f, 3000.0f, 300.0f), 300.0f, withText (milliseconds)));
    layout.add (std::make_unique<Float> (ParamIDs::sustain, "Sustain", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.8f, withText (percent)));
    layout.add (std::make_unique<Float> (ParamIDs::release, "Release", skewed (0.0f, 5000.0f, 500.0f), 600.0f, withText (milliseconds)));
    layout.add (std::make_unique<Float> (ParamIDs::width, "Width", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.6f, withText (percent)));
    layout.add (std::make_unique<Float> (ParamIDs::gain, "Gain", juce::NormalisableRange<float> { -24.0f, 6.0f }, 0.0f,
                                         withText ([] (float v) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; })));
    layout.add (std::make_unique<Float> (ParamIDs::velocityToRadius, "Velocity to radius", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.3f, withText (percent)));

    return layout;
}
