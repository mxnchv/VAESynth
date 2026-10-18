#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include "Parameters.h"
#include "engine/GrainEngine.h"
#include "engine/GrainMap.h"

class VAESynthProcessor final : public juce::AudioProcessor,
                                private juce::Timer
{
public:
    static constexpr int maxSamples = 32;

    VAESynthProcessor();
    ~VAESynthProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() noexcept { return state; }

    //==============================================================================
    // Sample library. Loading, analysis and removal run in order on a
    // background thread; every change publishes a new immutable GrainMap.
    static bool isSupportedAudioFile (const juce::File&);
    void addSampleFiles (const juce::Array<juce::File>& files);
    void removeSample (const vae::SampleSourcePtr& source);
    void clearSamples();
    void rebuildMap();

    vae::GrainMap::Ptr getMap() const;
    bool isLoading() const noexcept { return pendingJobs.load() > 0; }
    juce::String getLastError() const;

    // Per-sample settings, addressed by SampleSource::slot.
    int getRootNote (int slot) const;
    void setRootNote (int slot, int midiNote);
    bool isMuted (int slot) const;
    void setMuted (int slot, bool);
    bool isSoloed (int slot) const;
    void setSoloed (int slot, bool);

    // Plays one grain of the current map once (clicking a point on the sphere).
    void auditionGrain (int mapGrainIndex) noexcept { pendingAudition.store (mapGrainIndex); }

    // Map grain indices that started playing since the last call. UI thread only.
    void readPlayedGrains (const std::function<void (int)>& callback) { engine.readPlayedGrains (callback); }

    // Where the cursor actually is, trajectories included. Safe from any thread.
    struct Cursor
    {
        float azimuth = 0.0f, elevation = 0.0f, radius = 1.0f;
    };

    Cursor getLiveCursor() const noexcept;
    double getTempo() const noexcept { return liveTempo.load(); }

private:
    struct LoadRequest
    {
        juce::File file;
        int rootNote = -1;  // -1: use the detected root
        bool muted = false, soloed = false;
    };

    struct RetiredSource
    {
        vae::SampleSourcePtr source;
        juce::uint32 unusedSince = 0;
    };

    void timerCallback() override;
    void queueLoad (const LoadRequest&);
    void loadFile (const LoadRequest&);
    void publishMap (vae::GrainMap::Ptr);
    void retire (const vae::SampleSourcePtr&);
    vae::EngineSettings readSettings (vae::Vec3 direction, float radius, double tempo, double ppq, bool playing) const;

    juce::AudioProcessorValueTreeState state;
    vae::GrainEngine engine;
    vae::SampleSlots slots;
    juce::SmoothedValue<float> outputGain;

    std::atomic<float>* azimuth = nullptr;
    std::atomic<float>* elevation = nullptr;
    std::atomic<float>* radius = nullptr;
    std::atomic<float>* trajectoryMode = nullptr;
    std::atomic<float>* trajectorySpeed = nullptr;
    std::atomic<float>* speedMode = nullptr;
    std::atomic<float>* speedSync = nullptr;
    std::atomic<float>* grainSize = nullptr;
    std::atomic<float>* sizeMode = nullptr;
    std::atomic<float>* sizeSync = nullptr;
    std::atomic<float>* density = nullptr;
    std::atomic<float>* densityMode = nullptr;
    std::atomic<float>* densitySync = nullptr;
    std::atomic<float>* spray = nullptr;
    std::atomic<float>* shape = nullptr;
    std::atomic<float>* neighbours = nullptr;
    std::atomic<float>* keyMode = nullptr;
    std::atomic<float>* detune = nullptr;
    std::atomic<float>* scaleRoot = nullptr;
    std::atomic<float>* scaleType = nullptr;
    std::atomic<float>* harmonicFilter = nullptr;
    std::atomic<float>* attack = nullptr;
    std::atomic<float>* decay = nullptr;
    std::atomic<float>* sustain = nullptr;
    std::atomic<float>* release = nullptr;
    std::atomic<float>* width = nullptr;
    std::atomic<float>* gain = nullptr;
    std::atomic<float>* velocityToRadius = nullptr;

    double trajectoryTime = 0.0;
    std::atomic<float> liveAzimuth { 0.0f }, liveElevation { 0.0f }, liveRadius { 1.0f };
    std::atomic<double> liveTempo { 120.0 };
    std::atomic<int> pendingAudition { -1 };

    juce::AudioFormatManager formats;
    std::atomic<int> pendingJobs { 0 };

    mutable juce::CriticalSection libraryLock;
    std::vector<vae::SampleSourcePtr> library;   // in map order
    std::array<bool, vae::SampleSlots::count> slotUsed {};
    std::vector<RetiredSource> retired;          // removed sources, kept until no grain can still read them
    vae::Projection frozenProjection;
    vae::GrainMap::Ptr latestMap;
    juce::ReferenceCountedArray<vae::GrainMap> liveMaps;  // released here, never on the audio thread
    juce::String lastError;

    juce::SpinLock mapSwapLock;
    vae::GrainMap::Ptr pendingMap;  // guarded by mapSwapLock
    vae::GrainMap::Ptr audioMap;    // audio thread only

    // Declared last so it is destroyed first, before anything its jobs touch.
    juce::ThreadPool loader { juce::ThreadPoolOptions{}.withNumberOfThreads (1).withThreadName ("VAESynth loader") };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VAESynthProcessor)
};
