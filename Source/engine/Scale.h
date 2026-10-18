#pragma once

#include <juce_core/juce_core.h>

#include <cmath>
#include <iterator>

namespace vae
{
    // Musical scale used to keep played notes and grain pitches in key.
    struct Scale
    {
        bool active = false;
        int root = 0;                 // pitch class, 0 = C
        juce::uint16 mask = 0x0fff;   // bit n set: root + n semitones is in the scale

        static const juce::StringArray& typeNames()
        {
            static const juce::StringArray names { "Off", "Major", "Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian",
                                                   "Harmonic minor", "Pentatonic major", "Pentatonic minor", "Blues" };
            return names;
        }

        static const juce::StringArray& rootNames()
        {
            static const juce::StringArray names { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            return names;
        }

        static Scale fromIndices (int rootIndex, int typeIndex) noexcept
        {
            static constexpr juce::uint16 masks[] {
                0x0fff,                                         // off (unused)
                0b101010110101, 0b010110101101, 0b011010101101, // major, minor, dorian
                0b010110101011, 0b101011010101, 0b011010110101, // phrygian, lydian, mixolydian
                0b100110101101, 0b001010010101, 0b010010101001, // harmonic minor, pentatonic major, pentatonic minor
                0b010011101001                                  // blues
            };

            Scale s;
            s.active = typeIndex > 0 && typeIndex < (int) std::size (masks);
            s.root = ((rootIndex % 12) + 12) % 12;
            s.mask = s.active ? masks[typeIndex] : (juce::uint16) 0x0fff;
            return s;
        }

        bool contains (int pitchClass) const noexcept
        {
            const auto degree = ((pitchClass - root) % 12 + 12) % 12;
            return (mask >> degree) & 1;
        }

        // Nearest note of the scale; ties go down. Inactive scales return the
        // input rounded to the nearest semitone.
        int snap (float note) const noexcept
        {
            const auto nearest = (int) std::round (note);

            if (! active)
                return nearest;

            int best = nearest;
            float bestDistance = 1.0e9f;

            for (int offset = -6; offset <= 6; ++offset)
            {
                const auto candidate = nearest + offset;
                const auto distance = std::abs ((float) candidate - note);

                if (contains (candidate) && (distance < bestDistance - 1.0e-4f
                                             || (std::abs (distance - bestDistance) <= 1.0e-4f && candidate < best)))
                {
                    best = candidate;
                    bestDistance = distance;
                }
            }

            return best;
        }
    };
}
