#pragma once

#include "Sounds.h"

#include <array>

namespace vae
{
    // Plays rendered one-shots, transposed by resampling. Real-time safe:
    // voices only hold references; sounds are never freed here as long as
    // their owner keeps a reference (see PlantSession).
    class Sampler
    {
    public:
        static constexpr int numVoices = 16;

        void prepare (double sampleRate);

        // `pan` is -1..1. Voices started with `fromKey` stop on noteOff.
        void start (const RenderedSound::Ptr& sound, int note, float velocity, float pan, bool fromKey);
        void noteOff (int note);
        void setSustain (bool down);
        void allNotesOff();

        // Adds into the first two channels of `output`.
        void render (juce::AudioBuffer<float>& output, int startSample, int numSamples);

        int activeVoices() const noexcept;

    private:
        struct Voice
        {
            RenderedSound::Ptr sound;
            double position = 0.0;
            double increment = 1.0;
            float gainLeft = 0.0f, gainRight = 0.0f;
            float release = 1.0f, releaseStep = 0.0f;
            int note = -1;
            bool fromKey = false, keyDown = false;
            juce::uint64 age = 0;
        };

        void beginRelease (Voice&);

        std::array<Voice, numVoices> voices;
        double sampleRate = 48000.0;
        juce::uint64 counter = 0;
        bool sustain = false;
    };
}
