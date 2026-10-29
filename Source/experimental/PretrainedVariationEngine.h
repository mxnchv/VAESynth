#pragma once

#include "../engine/VariationEngine.h"

#include <array>
#include <map>
#include <mutex>

namespace vae
{
    // EXPERIMENTAL (branch experiment/pretrained-engine; see experimental/README.md).
    //
    // A variation engine whose buds come from a pretrained autoencoder (Stable
    // Audio Open's VAE) running in a local Python server on 127.0.0.1
    // (experimental/pretrained_server). Used by PlantSession only when the plugin
    // is built with -DVAESYNTH_EXPERIMENTAL_PRETRAINED=ON; otherwise it is not
    // even compiled.
    //
    // The 11 trait values of a node are reused as coordinates in the 11 main
    // directions of the latent space (0.5 = no offset from the seed), so the
    // plant, the session and the interface work unchanged. The seed itself and
    // library patches still render with the DSP engine, exactly as before.
    //
    // mutate() runs here, in C++, so plants stay deterministic. It mirrors
    // vaesynth_exp/plant.py. render() asks the server for the bud's audio and
    // returns nullptr (the bud stays unlit) if the server is not running, is too
    // slow or answers with an error.
    class PretrainedVariationEngine final : public VariationEngine
    {
    public:
        static constexpr int defaultPort = 8765;  // overridable with the VAESYNTH_SERVER_PORT variable

        explicit PretrainedVariationEngine (int port = 0);

        Traits mutate (const Traits& parent, const MutationRequest&, Rng&) const override;
        RenderedSound::Ptr render (const Traits&, const SeedSource&) const override;

        // What the server's config says about moving through the latent space.
        struct Settings
        {
            std::array<float, trait::count> direction {};  // brighter towards +; zeros if unknown
            float sigma = 1.0f, leanK = 1.2f, leash = 0.0f;
            int configVersion = 0;
            bool loaded = false;
        };

        // The server's settings, asking it (GET /info) if they are not known yet.
        Settings getSettings() const;

        // The seed itself: sampleNeutral or a library patch.
        static bool isOrigin (const Traits&) noexcept;

        // The mutation, without any network. Same formula as vaesynth_exp/plant.py.
        static Traits mutateWith (const Traits& parent, const MutationRequest&, Rng&, const Settings&) noexcept;

        // Minimal HTTP/1.1 over a plain TCP socket to 127.0.0.1. A raw socket keeps
        // macOS hosts' HTTP restrictions out of the way and works the same everywhere.
        struct Response
        {
            int status = 0;  // 0 = no answer (not running, timeout, broken reply)
            juce::StringPairArray headers;
            juce::MemoryBlock body;
        };

        Response request (const juce::String& method, const juce::String& path,
                          const juce::StringPairArray& headers, const juce::MemoryBlock& body, int timeoutMs) const;

    private:
        const juce::AudioBuffer<float>* seedAudio (const SeedSource&, double& sampleRate) const;
        bool uploadSeed (const juce::String& key, const juce::AudioBuffer<float>&, double sampleRate) const;

        int port;
        DspVariationEngine dsp;

        mutable std::mutex mutex;
        mutable Settings settings;
        mutable juce::int64 lastInfoAttempt = 0;  // ms; 0 = never asked
        mutable std::map<int, RenderedSound::Ptr> libraryAudio;  // library patches rendered once, as seeds
    };
}
