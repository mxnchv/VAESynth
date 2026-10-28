#include "Plant.h"

#include <functional>

namespace vae
{
    Plant Plant::grow (const Traits& seed, float wild, std::uint32_t rngSeed, const VariationEngine& engine)
    {
        Rng rng (rngSeed);
        Plant plant;

        PlantNode root;
        root.traits = seed;
        root.phase = rng.uniform() * juce::MathConstants<float>::twoPi;
        plant.nodes.push_back (root);

        // Depth-first, so each branch's buds are mutations of their own parent.
        std::function<void (int)> branch = [&] (int parentIndex)
        {
            const auto parent = plant.nodes[(size_t) parentIndex];
            if (parent.depth >= maxDepth)
                return;

            const auto count = parent.depth == 0 ? 3 : (rng.uniform() < (parent.depth >= 2 ? 0.75f : 0.5f) ? 2 : 3);
            const auto spread = parent.depth == 0 ? 0.6f : 0.42f;

            for (int i = 0; i < count; ++i)
            {
                const auto side = (float) i / (float) (count - 1) * 2.0f - 1.0f;

                PlantNode child;
                child.parent = parentIndex;
                child.depth = parent.depth + 1;
                child.angle = parent.angle * 0.7f + side * spread + rng.gauss() * 0.12f;
                child.length = (parent.depth == 0 ? 0.3f : parent.length * 0.8f) * (0.85f + rng.uniform() * 0.3f);
                child.born = parent.born + 0.32f + rng.uniform() * 0.18f;
                child.phase = rng.uniform() * juce::MathConstants<float>::twoPi;

                const MutationRequest request { juce::jlimit (-1.0f, 1.0f, child.angle / 1.1f),
                                                wild * 0.17f * (0.6f + (float) parent.depth * 0.2f) };
                child.traits = engine.mutate (parent.traits, request, rng);

                plant.nodes.push_back (child);
                branch ((int) plant.nodes.size() - 1);
            }
        };

        branch (0);
        return plant;
    }

    float Plant::fullyGrownAfter() const noexcept
    {
        float latest = 0.0f;
        for (const auto& node : nodes)
            latest = juce::jmax (latest, node.born + growSeconds);
        return latest;
    }
}
