#pragma once

#include "Sounds.h"
#include "Traits.h"

#include <cstdint>

namespace vae
{
    // Small deterministic generator (mulberry32), so a plant regrows the same
    // from the same seed on every platform.
    class Rng
    {
    public:
        explicit Rng (std::uint32_t seed) noexcept : state (seed) {}

        float uniform() noexcept
        {
            state += 0x6D2B79F5u;
            auto t = state;
            t = (t ^ (t >> 15)) * (t | 1u);
            t ^= t + (t ^ (t >> 7)) * (t | 61u);
            return (float) ((t ^ (t >> 14)) >> 0) / 4294967296.0f;
        }

        float gauss() noexcept
        {
            const auto u = juce::jmax (1.0e-9f, uniform());
            return std::sqrt (-2.0f * std::log (u)) * std::cos (juce::MathConstants<float>::twoPi * uniform());
        }

    private:
        std::uint32_t state;
    };

    struct MutationRequest
    {
        float lean = 0.0f;    // -1 left (darker, softer) .. +1 right (brighter, more metallic)
        float amount = 0.0f;  // how far the child moves from its parent
    };

    // Grows variations of a sound and renders them. The beta does it with DSP;
    // a VAE can implement the same interface later (see docs/VAE.md).
    // Everything here runs off the audio thread.
    class VariationEngine
    {
    public:
        virtual ~VariationEngine() = default;

        virtual Traits mutate (const Traits& parent, const MutationRequest&, Rng&) const = 0;

        // Renders the node at the source's own note (`RenderedSound::rootNote`).
        // A sample seed with neutral traits comes back exactly as loaded.
        // May return nullptr when it cannot render: the bud then stays unlit.
        virtual RenderedSound::Ptr render (const Traits&, const SeedSource&) const = 0;

        // Engines that rebuild sounds can create each note on its own instead of
        // transposing one recording; the result plays at `note` (its rootNote).
        virtual bool rendersEveryNote() const { return false; }
        virtual RenderedSound::Ptr renderNote (const Traits& t, const SeedSource& s, int /*note*/) const { return render (t, s); }
    };

    class DspVariationEngine final : public VariationEngine
    {
    public:
        Traits mutate (const Traits& parent, const MutationRequest&, Rng&) const override;
        RenderedSound::Ptr render (const Traits&, const SeedSource&) const override;

        static constexpr double synthSampleRate = 48000.0;
        static constexpr double maxBudSeconds = 4.5;
    };
}
