#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Parameter IDs are stored in DAW sessions and automation lanes:
// never rename one once a build has been shared.
namespace ParamIDs
{
    // Space
    inline const juce::ParameterID azimuth         { "azimuth",         1 };
    inline const juce::ParameterID elevation       { "elevation",       1 };
    inline const juce::ParameterID radius          { "radius",          1 };
    inline const juce::ParameterID trajectoryMode  { "trajectoryMode",  1 };
    inline const juce::ParameterID trajectorySpeed { "trajectorySpeed", 1 };
    inline const juce::ParameterID speedMode       { "speedMode",       1 };
    inline const juce::ParameterID speedSync       { "speedSync",       1 };

    // Grains
    inline const juce::ParameterID grainSize       { "grainSize",       1 };
    inline const juce::ParameterID sizeMode        { "sizeMode",        1 };
    inline const juce::ParameterID sizeSync        { "sizeSync",        1 };
    inline const juce::ParameterID density         { "density",         1 };
    inline const juce::ParameterID densityMode     { "densityMode",     1 };
    inline const juce::ParameterID densitySync     { "densitySync",     1 };
    inline const juce::ParameterID spray           { "spray",           1 };
    inline const juce::ParameterID shape           { "shape",           1 };
    inline const juce::ParameterID neighbours      { "neighbours",      1 };

    // Pitch
    inline const juce::ParameterID keyMode         { "keyMode",         1 };
    inline const juce::ParameterID detune          { "detune",          1 };
    inline const juce::ParameterID scaleRoot       { "scaleRoot",       1 };
    inline const juce::ParameterID scaleType       { "scaleType",       1 };
    inline const juce::ParameterID harmonicFilter  { "harmonicFilter",  1 };

    // Voice
    inline const juce::ParameterID attack          { "attack",          1 };
    inline const juce::ParameterID decay           { "decay",           1 };
    inline const juce::ParameterID sustain         { "sustain",         1 };
    inline const juce::ParameterID release         { "release",         1 };
    inline const juce::ParameterID width           { "width",           1 };
    inline const juce::ParameterID gain            { "gain",            1 };
    inline const juce::ParameterID velocityToRadius { "velocityToRadius", 1 };
}

enum class TrajectoryMode { manual, orbit, lissajous };
enum class KeyMode { transpose, texture, tune };
enum class ClockMode { free, sync };

// Note values offered when a time parameter is synced to the host tempo.
namespace NoteDivisions
{
    const juce::StringArray& names();
    double beats (int index);   // length in quarter notes
}

// Trajectory cycle lengths when the speed is synced, in bars.
namespace CycleLengths
{
    const juce::StringArray& names();
    double bars (int index);
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
