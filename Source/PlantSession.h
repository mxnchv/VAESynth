#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

#include "engine/Plant.h"
#include "engine/RecipeEngine.h"

#include <array>
#include <atomic>
#include <vector>

// The seed history, the current plant and its rendered sounds.
//
// Threads:
//  - message thread: every public method except the audio-thread ones;
//  - a render thread: renders the notes of the picked bud first (engines that
//    rebuild every note), then the buds of the current plant, seed first;
//  - audio thread: getPlayableSounds() and drainAuditions() only.
//
// Every rendered sound is also kept in `keepAlive` until nothing else
// references it, so the audio thread never frees one.
class PlantSession final : private juce::Thread,
                           private juce::Timer
{
public:
    explicit PlantSession (std::atomic<float>& wildParameter);
    ~PlantSession() override;

    // How buds are made: effects on the seed, or rebuilt from its recipe.
    enum class Engine { effects, recipe };

    // What MIDI plays: the picked bud (or the seed), note by note when the
    // engine rebuilds every note, otherwise one sound transposed.
    struct PlayableSounds final : public juce::ReferenceCountedObject
    {
        using Ptr = juce::ReferenceCountedObjectPtr<PlayableSounds>;

        vae::RenderedSound::Ptr base;
        std::array<vae::RenderedSound::Ptr, 128> notes;

        const vae::RenderedSound::Ptr& forNote (int note) const noexcept
        {
            const auto& own = notes[(size_t) juce::jlimit (0, 127, note)];
            return own != nullptr ? own : base;
        }
    };

    struct Generation
    {
        vae::SeedSourcePtr source;
        vae::Traits traits {};
        float wild = 0.5f;
        std::uint32_t rngSeed = 1;
        juce::String name;
    };

    // ---- message thread
    void chooseLibrarySeed (int index);
    bool loadSeedFile (const juce::File&, juce::String& error);
    void plantNode (int nodeIndex);
    void regrow();
    void applyWild();  // same plant shape, buds nearer to or further from the seed
    void goTo (int generationIndex);
    void select (int nodeIndex);  // -1: nothing picked, the seed plays
    void audition (int nodeIndex, int note, float velocity, float pan);
    void setEngine (Engine);
    Engine getEngine() const noexcept                   { return engineKind; }
    void auditionRebuiltSeed();  // the seed rebuilt from its recipe, to compare

    bool hasPlant() const noexcept                      { return ! history.empty(); }
    const vae::Plant& getPlant() const noexcept         { return plant; }
    const std::vector<Generation>& getHistory() const   { return history; }
    int getCurrentGeneration() const noexcept           { return current; }
    int getSelected() const noexcept                    { return selected; }
    int getPlantVersion() const noexcept                { return plantVersion; }
    int getShapeVersion() const noexcept                { return shapeVersion; }  // changes when the plant regrows from scratch
    int getHomeNote() const noexcept;
    bool isRendered (int nodeIndex) const;
    juce::String describeNode (int nodeIndex) const;

    juce::ValueTree toValueTree() const;
    void restore (const juce::ValueTree&);

    // Blocks until every bud of the current plant, and every note of the
    // picked bud, is rendered (tests only).
    bool waitUntilRendered (int timeoutMs);

    // ---- audio thread
    PlayableSounds::Ptr getPlayableSounds();

    struct Audition
    {
        vae::RenderedSound* sound = nullptr;  // holds one reference taken by audition()
        int note = 60;
        float velocity = 0.8f;
        float pan = 0.0f;
    };

    template <typename Callback>
    void drainAuditions (Callback&& callback)
    {
        const auto scope = auditionFifo.read (auditionFifo.getNumReady());
        auto handle = [&] (int start, int size)
        {
            for (int i = start; i < start + size; ++i)
            {
                auto& a = auditions[(size_t) i];
                vae::RenderedSound::Ptr sound (a.sound);
                a.sound->decReferenceCountWithoutDeleting();
                callback (sound, a.note, a.velocity, a.pan);
            }
        };
        handle (scope.startIndex1, scope.blockSize1);
        handle (scope.startIndex2, scope.blockSize2);
    }

private:
    void run() override;
    void timerCallback() override;

    void pushGeneration (Generation);
    void growCurrent (bool keepSelection = false);
    void publishPlayable();
    void scheduleNotes();
    void pushAudition (const vae::RenderedSound::Ptr&, int note, float velocity, float pan);
    const vae::VariationEngine& activeEngine() const;
    static std::uint32_t newRngSeed();

    std::atomic<float>& wild;
    vae::DspVariationEngine engine;
    vae::RecipeVariationEngine recipeEngine;
    Engine engineKind = Engine::effects;
    juce::AudioFormatManager formats;

    std::vector<Generation> history;
    int current = -1;
    int selected = -1;
    int plantVersion = 0;
    int shapeVersion = 0;
    vae::Plant plant;

    // Shared with the render thread.
    mutable juce::CriticalSection lock;
    std::vector<vae::RenderedSound::Ptr> sounds;     // per node of the current plant
    std::vector<vae::RenderedSound::Ptr> keepAlive;
    std::vector<PlayableSounds::Ptr> keepAliveSets;
    int renderJob = 0;
    const vae::VariationEngine* jobEngine = nullptr;
    vae::SeedSourcePtr jobSource;
    std::vector<vae::Traits> jobTraits;
    std::vector<int> jobOrder;        // node indices; -1 = the rebuilt seed
    size_t budCursor = 0;
    int playableNode = 0;
    vae::RenderedSound::Ptr rebuiltSeed;

    int noteJob = 0;
    vae::Traits noteTraits {};
    std::vector<int> noteOrder;
    size_t noteCursor = 0, notesDone = 0;
    std::array<vae::RenderedSound::Ptr, 128> noteSounds;

    // Shared with the audio thread.
    juce::SpinLock playableLock;
    PlayableSounds::Ptr playable;

    juce::AbstractFifo auditionFifo { 64 };
    std::array<Audition, 64> auditions;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlantSession)
};
