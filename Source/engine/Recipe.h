#pragma once

#include "Sounds.h"  // before Traits.h: its Kind::library would shadow vae::library
#include "Traits.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace vae
{
    // A sound taken apart into what it is made of: partials (how loud each one
    // is over time, and where it sits relative to the fundamental) and noise
    // (how loud each frequency band is over time). It can be rebuilt at any note.
    struct Recipe
    {
        double sampleRate = 44100.0;
        int rootNote = 60;
        float fundamental = 261.6f;     // Hz at rootNote
        int numSamples = 0;             // length of the analysed sound
        bool stereo = false;
        float width = 0.0f;             // 0 mono .. 1 fully decorrelated sides
        float peak = 0.0f;              // of the analysed sound

        int fftSize = 2048;
        int hop = 256;
        int numFrames = 0;

        int numPartials = 0;
        std::vector<float> ratios;      // per partial: frequency / (index · fundamental)
        std::vector<float> amplitudes;  // numFrames × numPartials, linear sine amplitude

        int numBands = 0;
        std::vector<float> bandEdges;   // numBands + 1, Hz
        std::vector<float> noise;       // numFrames × numBands, magnitude per bin

        float amplitude (int frame, int partial) const noexcept { return amplitudes[(size_t) (frame * numPartials + partial)]; }
        float noiseLevel (int frame, int band) const noexcept   { return noise[(size_t) (frame * numBands + band)]; }
    };

    using RecipePtr = std::shared_ptr<const Recipe>;

    // Analyses `audio` (1 or 2 channels) whose fundamental is `rootNote`.
    RecipePtr analyse (const juce::AudioBuffer<float>& audio, double sampleRate, int rootNote);

    // How to rebuild: the note to play and the changes ("genes") to apply,
    // each a difference from the seed in -1..1 (0 = unchanged).
    struct Genes
    {
        float tilt = 0.0f;        // darker .. brighter
        float evenOdd = 0.0f;     // < 0 thins odd partials, > 0 thins even ones (hollow)
        float inharmonic = 0.0f;  // < 0 more harmonic, > 0 stretched partials (bell, metal)
        float formant = 0.0f;     // moves the colour down or up (smaller / bigger body)
        float attack = 0.0f;      // > 0 slower fade-in, higher partials later
        float decay = 0.0f;       // < 0 dies faster (highs first), > 0 lasts longer
        float body = 0.0f;        // > 0 a resonance
        float noise = 0.0f;       // less .. more breath
        float chorus = 0.0f;      // > 0 detuned twins spread left and right
        int octaveLayer = 0;      // -1, 0 or +1
        float octaveGain = 0.0f;
        bool reversed = false;    // the sound's evolution runs backwards: it swells
    };

    Genes genesFrom (const Traits& node, const Traits& seed);

    // Rebuilds the recipe at `note`, applying `genes`. At most `maxSeconds` long.
    juce::AudioBuffer<float> resynthesise (const Recipe&, int note, const Genes&, double maxSeconds, std::uint32_t noiseSeed);
}
