#include "RecipeEngine.h"

namespace vae
{
    namespace
    {
        const Traits& seedTraits (const SeedSource& seed)
        {
            return seed.kind == SeedSource::Kind::library ? library[(size_t) seed.libraryIndex].traits : sampleNeutral;
        }

        std::uint32_t hashOf (const Traits& t, int note) noexcept
        {
            std::uint32_t h = 2166136261u ^ (std::uint32_t) note;
            for (auto v : t)
            {
                h ^= (std::uint32_t) (v * 1000003.0f);
                h *= 16777619u;
            }
            return h;
        }

        RenderedSound::Ptr makeSound (juce::AudioBuffer<float> audio, double sampleRate, int note)
        {
            RenderedSound::Ptr sound (new RenderedSound());
            sound->audio = std::move (audio);
            sound->sampleRate = sampleRate;
            sound->rootNote = note;
            return sound;
        }
    }

    RecipePtr RecipeVariationEngine::recipeFor (const SeedSource& seed) const
    {
        {
            const std::lock_guard<std::mutex> lock (cacheLock);
            for (const auto& [id, recipe] : cache)
                if (id == seed.id)
                    return recipe;
        }

        RecipePtr recipe;
        if (seed.kind == SeedSource::Kind::library)
        {
            const auto synth = effects.render (seedTraits (seed), seed);
            recipe = analyse (synth->audio, synth->sampleRate, seed.rootNote);
        }
        else
        {
            recipe = analyse (seed.audio, seed.sampleRate, seed.rootNote);
        }

        const std::lock_guard<std::mutex> lock (cacheLock);
        cache.emplace_back (seed.id, recipe);
        if (cache.size() > 8)
            cache.erase (cache.begin());
        return recipe;
    }

    RenderedSound::Ptr RecipeVariationEngine::renderNote (const Traits& traits, const SeedSource& seed, int note) const
    {
        const auto isSeed = traits == seedTraits (seed);

        // The seed at its own note is the original sound, untouched.
        if (isSeed && note == seed.rootNote)
            return effects.render (traits, seed);

        const auto recipe = recipeFor (seed);
        const auto genes = isSeed ? Genes {} : genesFrom (traits, seedTraits (seed));
        const auto maxSeconds = isSeed ? maxSeedSeconds : DspVariationEngine::maxBudSeconds;
        auto audio = resynthesise (*recipe, note, genes, maxSeconds, hashOf (traits, note));

        // Buds play about as loud as the seed, so sweeping the plant stays even.
        const auto peak = audio.getMagnitude (0, audio.getNumSamples());
        if (! isSeed && peak > 1.0e-6f)
            audio.applyGain (juce::jmin (8.0f, juce::jmin (0.95f, recipe->peak) / peak));

        return makeSound (std::move (audio), recipe->sampleRate, note);
    }

    RenderedSound::Ptr RecipeVariationEngine::renderRebuiltSeed (const SeedSource& seed) const
    {
        const auto recipe = recipeFor (seed);
        return makeSound (resynthesise (*recipe, seed.rootNote, {}, maxSeedSeconds, hashOf (seedTraits (seed), seed.rootNote)),
                          recipe->sampleRate, seed.rootNote);
    }
}
