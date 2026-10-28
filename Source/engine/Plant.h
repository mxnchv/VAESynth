#pragma once

#include "VariationEngine.h"

#include <vector>

namespace vae
{
    // One node of a plant: the seed (index 0) or a bud. Geometry is in plant
    // units and only describes the shape; the UI decides where it is drawn.
    struct PlantNode
    {
        int parent = -1;
        int depth = 0;
        float angle = 0.0f;   // radians from vertical, positive leans right
        float length = 0.0f;  // stem length from the parent
        float born = 0.0f;    // seconds after planting when its stem starts growing
        float phase = 0.0f;   // for the idle sway
        Traits traits {};
    };

    // A seed and its buds, each a mutation of its parent. Growing is
    // deterministic: the same seed, wildness and rngSeed give the same plant.
    struct Plant
    {
        std::vector<PlantNode> nodes;

        static constexpr int maxDepth = 4;

        // Wild at 0 still varies a little: a plant of copies of the seed is useless.
        static constexpr float minimumWild = 0.25f;
        static constexpr float growSeconds = 0.55f;  // how long one stem takes to grow

        static Plant grow (const Traits& seed, float wild, std::uint32_t rngSeed, const VariationEngine&);

        // Seconds until the whole plant has grown.
        float fullyGrownAfter() const noexcept;
    };
}
