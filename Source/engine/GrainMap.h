#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <memory>
#include <vector>

#include "Geometry.h"

namespace vae
{
    // v0 places grains with hand-made audio descriptors. The VAE encoder will
    // replace these features in v1; the projection to the sphere stays the same.
    constexpr int numFeatures = 5;
    using FeatureVector = std::array<float, numFeatures>;

    using Chroma = std::array<float, 12>;  // energy per pitch class, C first, sums to 1

    struct GrainInfo
    {
        int start = 0;          // first sample of the analysis window
        float timeNorm = 0.0f;  // position in the sample, 0 = start, 1 = end
        FeatureVector features {};
        float pitch = -1.0f;    // MIDI note (fractional); negative when the grain has no clear pitch
        Chroma chroma {};
    };

    struct SampleSource
    {
        juce::String name;
        juce::File file;
        juce::AudioBuffer<float> audio;  // mono
        double sampleRate = 44100.0;
        std::vector<GrainInfo> grains;
        int detectedRoot = -1;  // most common pitch, or -1 for unpitched material
        int slot = 0;           // stable index for colour and per-sample settings
    };

    using SampleSourcePtr = std::shared_ptr<const SampleSource>;

    // Cuts mono audio into overlapping windows and measures each one.
    // Near-silent windows are skipped.
    std::shared_ptr<SampleSource> analyseAudio (const juce::String& name,
                                                juce::AudioBuffer<float> mono,
                                                double sampleRate);

    // Reads, mixes down to mono and analyses a file. Returns nullptr and sets
    // `error` on failure. Longer files are truncated to `maxSeconds`.
    std::shared_ptr<SampleSource> loadAndAnalyse (juce::AudioFormatManager& formats,
                                                  const juce::File& file,
                                                  juce::String& error,
                                                  double maxSeconds = 120.0);

    // Maps feature vectors to directions on the unit sphere: standardise,
    // keep the 3 principal components, whiten them and normalise.
    struct Projection
    {
        bool valid = false;
        FeatureVector mean {}, scale {};
        std::array<FeatureVector, 3> axes {};
        std::array<float, 3> axisGain {};

        static Projection fit (const std::vector<FeatureVector>& data);
        Vec3 project (const FeatureVector& features) const noexcept;

        juce::ValueTree toValueTree() const;
        static Projection fromValueTree (const juce::ValueTree& tree);
    };

    struct MapGrain
    {
        int source = 0;         // index into GrainMap::getSources()
        int grainIndex = 0;     // index into that source's grains
        int start = 0;
        float timeNorm = 0.0f;
        Vec3 position;
    };

    // Immutable snapshot of every loaded sample and where its grains sit on
    // the sphere. A new map is built whenever the library or projection changes.
    class GrainMap final : public juce::ReferenceCountedObject
    {
    public:
        using Ptr = juce::ReferenceCountedObjectPtr<GrainMap>;

        // With a valid `frozenProjection` existing grains keep their place and
        // new ones are fitted into the same map; otherwise the projection is refitted.
        static Ptr build (std::vector<SampleSourcePtr> sources, const Projection* frozenProjection);

        const std::vector<SampleSourcePtr>& getSources() const noexcept { return sources; }
        const std::vector<MapGrain>& getGrains() const noexcept         { return grains; }
        const Projection& getProjection() const noexcept                { return projection; }

    private:
        std::vector<SampleSourcePtr> sources;
        std::vector<MapGrain> grains;
        Projection projection;
    };
}
