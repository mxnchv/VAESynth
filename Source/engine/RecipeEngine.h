#pragma once

#include "Recipe.h"
#include "VariationEngine.h"

#include <mutex>
#include <vector>

namespace vae
{
    // Variations that change what the sound is made of rather than piling
    // effects on it: the seed is analysed once into a recipe of partials and
    // noise, every bud mutates that recipe, and every note is rebuilt from it.
    // Library seeds are analysed from the built-in synth.
    class RecipeVariationEngine final : public VariationEngine
    {
    public:
        Traits mutate (const Traits& parent, const MutationRequest& request, Rng& rng) const override
        {
            return effects.mutate (parent, request, rng);  // same plants as the effects engine
        }

        RenderedSound::Ptr render (const Traits& traits, const SeedSource& seed) const override
        {
            return renderNote (traits, seed, seed.rootNote);
        }

        bool rendersEveryNote() const override { return true; }
        RenderedSound::Ptr renderNote (const Traits&, const SeedSource&, int note) const override;

        // The seed rebuilt from its recipe with nothing changed, to compare with the original.
        RenderedSound::Ptr renderRebuiltSeed (const SeedSource&) const;

    private:
        RecipePtr recipeFor (const SeedSource&) const;

        DspVariationEngine effects;
        mutable std::mutex cacheLock;
        mutable std::vector<std::pair<std::uint64_t, RecipePtr>> cache;
    };
}
