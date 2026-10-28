#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace vae
{
    // What a node of the plant sounds like, as values in 0..1. For library
    // seeds they are the patch of the built-in synth; for your own sound they
    // are effects applied on top of it (the untouched sound is `sampleNeutral`).
    namespace trait
    {
        enum Index : size_t
        {
            bright,   // dark .. bright
            odd,      // balance of odd and even harmonics (synth only)
            metal,    // inharmonic shimmer: FM (synth) or ring modulation (sample)
            ratio,    // flavour of that shimmer and of the body resonance
            attack,   // instant .. slow fade-in
            decay,    // short .. long
            body,     // resonance
            noise,    // breath / air
            chorus,   // detuned double, wider
            shift,    // < 0.28 octave down layer, > 0.72 octave up layer
            reverse,  // > 0.8 reversed (sample) or swelled (synth)
            count
        };
    }

    using Traits = std::array<float, trait::count>;

    inline constexpr Traits sampleNeutral { 0.5f, 0.5f, 0.0f, 0.4f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f };

    inline bool isNeutral (const Traits& t) noexcept
    {
        for (size_t i = 0; i < trait::count; ++i)
            if (std::abs (t[i] - sampleNeutral[i]) > 1.0e-5f)
                return false;

        return true;
    }

    inline bool hasOctaveUp (const Traits& t) noexcept   { return t[trait::shift] > 0.72f; }
    inline bool hasOctaveDown (const Traits& t) noexcept { return t[trait::shift] < 0.28f; }
    inline bool isReversed (const Traits& t) noexcept    { return t[trait::reverse] > 0.8f; }

    struct LibrarySeed
    {
        const char* name;
        Traits traits;
    };

    // Seeds to start from without loading anything; they play the built-in synth.
    inline constexpr std::array<LibrarySeed, 5> library {{
        { "Glass",  { 0.85f, 0.40f, 0.62f, 0.35f, 0.03f, 0.55f, 0.20f, 0.05f, 0.40f, 0.5f, 0.0f } },
        { "Keys",   { 0.45f, 0.25f, 0.28f, 0.00f, 0.04f, 0.60f, 0.25f, 0.04f, 0.30f, 0.5f, 0.0f } },
        { "Pluck",  { 0.70f, 0.35f, 0.05f, 0.20f, 0.02f, 0.30f, 0.35f, 0.10f, 0.15f, 0.5f, 0.0f } },
        { "Bell",   { 0.60f, 0.50f, 0.88f, 0.55f, 0.01f, 0.85f, 0.15f, 0.02f, 0.10f, 0.5f, 0.0f } },
        { "Breath", { 0.30f, 0.70f, 0.02f, 0.10f, 0.35f, 0.50f, 0.65f, 0.55f, 0.25f, 0.5f, 0.0f } },
    }};

    // Names a node by how it differs from `seed`, e.g. "Darker, shorter, octave up".
    inline juce::String describeDifference (const Traits& node, const Traits& seed)
    {
        struct Change { float amount; const char* word; };

        auto change = [&] (trait::Index i, const char* up, const char* down) -> Change
        {
            const auto d = node[i] - seed[i];
            return { std::abs (d), d > 0.0f ? up : down };
        };

        std::array<Change, 7> changes {{
            change (trait::bright, "brighter", "darker"),
            change (trait::metal,  "metallic", nullptr),
            change (trait::decay,  "longer", "shorter"),
            change (trait::attack, "softer attack", nullptr),
            change (trait::noise,  "breathier", nullptr),
            change (trait::chorus, "wider", nullptr),
            change (trait::body,   "resonant", nullptr),
        }};

        std::sort (changes.begin(), changes.end(), [] (const Change& a, const Change& b) { return a.amount > b.amount; });

        juce::StringArray words;
        for (const auto& c : changes)
            if (c.word != nullptr && c.amount > 0.08f && words.size() < 2)
                words.add (c.word);

        if (hasOctaveUp (node) && ! hasOctaveUp (seed))     words.add ("octave up");
        if (hasOctaveDown (node) && ! hasOctaveDown (seed)) words.add ("octave down");
        if (isReversed (node) && ! isReversed (seed))       words.add ("reversed");

        if (words.isEmpty())
            return "Close to the seed";

        while (words.size() > 3)
            words.remove (words.size() - 1);

        auto text = words.joinIntoString (", ");
        return text.substring (0, 1).toUpperCase() + text.substring (1);
    }
}
