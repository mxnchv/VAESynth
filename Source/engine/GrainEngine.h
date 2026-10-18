#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <vector>

#include "GrainMap.h"

namespace vae
{
    struct EngineSettings
    {
        Vec3 direction { 0.0f, 0.0f, 1.0f };  // unit vector of the cursor
        float radius = 1.0f;                  // signed: negative is the antipode
        float grainSizeMs = 80.0f;
        float density = 24.0f;                // grains per second, per voice
        float spray = 0.15f;                  // 0..1
        float shape = 0.2f;                   // 0 = smooth (Hann), 1 = percussive
        int neighbours = 6;
        bool transpose = true;                // false = texture mode
        float detuneCents = 8.0f;
        float width = 0.6f;                   // stereo spread, 0..1
        float velocityToRadius = 0.3f;        // 0..1
        juce::ADSR::Parameters envelope { 0.01f, 0.3f, 0.8f, 0.6f };
    };

    // Polyphonic granular player. All MIDI notes share the sphere cursor; each
    // active note spawns grains picked near it and plays them transposed.
    class GrainEngine
    {
    public:
        static constexpr int maxVoices = 8;
        static constexpr int maxGrains = 512;
        static constexpr int maxNeighbours = 16;
        static constexpr int maxSources = 64;

        void prepare (double sampleRate, int maxBlockSize);

        // Audio thread. The caller keeps the map alive while it is in use.
        void setMap (const GrainMap* newMap) noexcept { map = newMap; }

        // Root note per source index; read on the audio thread.
        void setRootNotes (const std::array<std::atomic<int>, maxSources>* roots) noexcept { rootNotes = roots; }

        void process (juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi, const EngineSettings& settings);
        void reset();

        int getNumActiveGrains() const noexcept;

    private:
        struct Voice
        {
            bool held = false;
            int note = -1;
            float velocity = 0.0f;
            juce::uint32 startedAt = 0;
            double samplesToNextGrain = 0.0;
            juce::ADSR envelope;
        };

        struct Grain
        {
            bool active = false;
            const SampleSource* source = nullptr;
            int voice = 0;
            int delay = 0;
            int length = 0;
            int position = 0;
            double readPosition = 0.0;
            double increment = 1.0;
            float gainLeft = 0.0f, gainRight = 0.0f;
            float shape = 0.0f;
        };

        void handleMidi (const juce::MidiMessage&);
        void renderSegment (juce::AudioBuffer<float>&, int start, int numSamples, const EngineSettings&);
        void spawnGrain (int voiceIndex, int delay, const EngineSettings&);
        int pickGrain (Vec3 target, int neighbours);
        Vec3 randomInBall();

        static float grainWindow (float t, float shape) noexcept;

        double sampleRate = 44100.0;
        int blockCapacity = 0;
        juce::uint32 noteCounter = 0;

        const GrainMap* map = nullptr;
        const std::array<std::atomic<int>, maxSources>* rootNotes = nullptr;

        std::array<Voice, maxVoices> voices;
        std::array<Grain, maxGrains> grains;
        std::vector<float> envelopeBuffer;  // maxVoices * blockCapacity
        juce::Random random;
    };
}
