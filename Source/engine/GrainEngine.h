#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <vector>

#include "GrainMap.h"
#include "Scale.h"

namespace vae
{
    enum class PitchMode { transpose, texture, tune };

    // When grains are spawned. Free: `density` per second with a little jitter.
    // Synced: one grain per `divisionBeats`, locked to the host grid while it plays.
    struct GrainClock
    {
        bool synced = false;
        bool hostPlaying = false;
        double ppqAtBlockStart = 0.0;
        double samplesPerBeat = 22050.0;
        double divisionBeats = 0.5;
    };

    struct EngineSettings
    {
        Vec3 direction { 0.0f, 0.0f, 1.0f };  // unit vector of the cursor
        float radius = 1.0f;                  // signed: negative is the antipode
        float grainSizeMs = 80.0f;
        float density = 24.0f;                // grains per second, per voice (free clock)
        GrainClock clock;
        float spray = 0.15f;                  // 0..1
        float shape = 0.2f;                   // 0 = smooth (Hann), 1 = percussive
        int neighbours = 6;
        PitchMode pitchMode = PitchMode::transpose;
        Scale scale;
        float harmonicFilter = 0.0f;          // 0..1, prefer grains whose spectrum fits the key
        float detuneCents = 8.0f;
        float width = 0.6f;                   // stereo spread, 0..1
        float velocityToRadius = 0.3f;        // 0..1
        juce::ADSR::Parameters envelope { 0.01f, 0.3f, 0.8f, 0.6f };
    };

    // Per-sample settings, indexed by SampleSource::slot so they survive
    // other samples being removed. Written by the UI, read by the audio thread.
    struct SampleSlots
    {
        static constexpr int count = 64;

        std::array<std::atomic<int>, count> rootNote;
        std::array<std::atomic<bool>, count> muted, soloed;

        SampleSlots()
        {
            for (int i = 0; i < count; ++i)
                reset (i);
        }

        void reset (int slot, int root = 60)
        {
            rootNote[(size_t) slot].store (root);
            muted[(size_t) slot].store (false);
            soloed[(size_t) slot].store (false);
        }

        bool anySoloed() const noexcept
        {
            for (const auto& s : soloed)
                if (s.load (std::memory_order_relaxed))
                    return true;
            return false;
        }

        bool isAudible (int slot, bool soloActive) const noexcept
        {
            return soloActive ? soloed[(size_t) slot].load (std::memory_order_relaxed)
                              : ! muted[(size_t) slot].load (std::memory_order_relaxed);
        }
    };

    // Polyphonic granular player. All MIDI notes share the sphere cursor; each
    // active note spawns grains picked near it and plays them transposed.
    class GrainEngine
    {
    public:
        static constexpr int maxVoices = 8;
        static constexpr int maxGrains = 512;
        static constexpr int maxNeighbours = 16;

        void prepare (double sampleRate, int maxBlockSize);

        // Audio thread. The caller keeps the map alive while it is in use.
        void setMap (const GrainMap* newMap) noexcept { map = newMap; }
        void setSampleSlots (const SampleSlots* slots) noexcept { sampleSlots = slots; }

        void process (juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi, const EngineSettings& settings);
        void reset();

        // Plays one grain of the map as it is, without a note (clicking a point).
        void audition (int mapGrainIndex, const EngineSettings& settings);

        // Map grain indices that started playing, oldest first. Single consumer (UI).
        template <typename Callback>
        void readPlayedGrains (Callback&& callback)
        {
            const auto scope = playedFifo.read (playedFifo.getNumReady());
            for (int i = 0; i < scope.blockSize1; ++i) callback (played[(size_t) (scope.startIndex1 + i)]);
            for (int i = 0; i < scope.blockSize2; ++i) callback (played[(size_t) (scope.startIndex2 + i)]);
        }

        int getNumActiveGrains() const noexcept;
        int getGrainsSpawned (int slot) const noexcept { return spawnedBySlot[(size_t) slot]; }

    private:
        struct Voice
        {
            bool held = false;
            int inputNote = -1;   // the key pressed, for matching note-off
            int note = -1;        // the note played, after snapping to the scale
            float velocity = 0.0f;
            juce::uint32 startedAt = 0;
            double samplesToNextGrain = 0.0;
            bool firstGrainPending = false;
            juce::int64 lastGridIndex = 0;
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

        void handleMidi (const juce::MidiMessage&, const EngineSettings&);
        void renderSegment (juce::AudioBuffer<float>&, int start, int numSamples, const EngineSettings&);
        void spawnGrain (int voiceIndex, int delay, const EngineSettings&);
        int pickGrain (Vec3 target, int voiceNote, const EngineSettings&);
        int pickAnyGrain();
        bool isAudible (const MapGrain&) const noexcept;
        float semitoneShift (const SampleSource&, const GrainInfo&, int voiceNote, const EngineSettings&) const noexcept;
        float harmonicFit (const MapGrain&, int voiceNote, const EngineSettings&) const noexcept;
        Grain* freeGrainSlot() noexcept;
        void startGrain (Grain&, int mapGrainIndex, int voice, int delay, int length, double increment,
                         float gain, float pan, float shape);
        Vec3 randomInBall();

        static float grainWindow (float t, float shape) noexcept;

        double sampleRate = 44100.0;
        int blockCapacity = 0;
        juce::uint32 noteCounter = 0;
        bool soloActive = false;

        const GrainMap* map = nullptr;
        const SampleSlots* sampleSlots = nullptr;

        std::array<Voice, maxVoices> voices;
        std::array<Grain, maxGrains> grains;
        std::vector<float> envelopeBuffer;  // (maxVoices + 1) rows of blockCapacity; the last row is all ones
        juce::Random random;

        juce::AbstractFifo playedFifo { 1024 };
        std::array<int, 1024> played {};
        std::array<int, SampleSlots::count> spawnedBySlot {};
    };
}
