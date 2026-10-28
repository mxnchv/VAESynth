#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>

namespace vae
{
    // Where a plant comes from: one of the library seeds (the built-in synth)
    // or a sound file. Immutable once loaded and shared between generations.
    struct SeedSource
    {
        enum class Kind { library, sample };

        Kind kind = Kind::library;
        int libraryIndex = 0;

        juce::String name;
        juce::File file;
        juce::AudioBuffer<float> audio;  // the file as loaded, 1 or 2 channels
        double sampleRate = 44100.0;
        int rootNote = 60;               // detected, or C4 when it has no clear pitch
        bool pitched = false;
    };

    using SeedSourcePtr = std::shared_ptr<const SeedSource>;

    // Longest sound file accepted as a seed.
    inline constexpr double maxSeedSeconds = 10.0;

    std::shared_ptr<SeedSource> makeLibrarySource (int index);

    // Reads a file and detects its note. Returns nullptr and sets `error` on failure.
    std::shared_ptr<SeedSource> loadSampleSource (juce::AudioFormatManager& formats,
                                                  const juce::File& file,
                                                  juce::String& error);

    // Detects the note of a one-shot: YIN over short windows, median of the
    // pitched ones. Returns -1 when less than 30 % of the windows are pitched.
    int detectRootNote (const juce::AudioBuffer<float>& audio, double sampleRate);

    // The audio of one node, rendered ahead of time. It plays at `rootNote`
    // when not transposed. Shared between the render thread, the UI and voices.
    struct RenderedSound final : public juce::ReferenceCountedObject
    {
        using Ptr = juce::ReferenceCountedObjectPtr<RenderedSound>;

        juce::AudioBuffer<float> audio;
        double sampleRate = 48000.0;
        int rootNote = 60;
    };
}
