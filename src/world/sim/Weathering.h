// Weathering.h -- blocks that change with their surroundings over time (vanilla
// randomTick of farmland, ice, snow layers, copper and budding amethyst) and sponges
// soaking up water.
#pragma once

#include <cstdint>

#include "world/block/BlockGetter.h"

namespace mc {

class Level;

namespace sim {

enum class Weathering : uint8_t {
    None,
    Farmland,        // wet near water or in rain, dries out, turns to dirt when bare and dry
    Ice,             // melts in block light above 10
    SnowLayer,       // melts in block light above 11
    Copper,          // unwaxed copper ages toward oxidized
    BuddingAmethyst, // grows buds on its sides
};
Weathering weatheringOf(const Block& block);

bool weatheringTicksRandomly(const BlockRegistry& registry, Weathering kind, StateId state);
void weatheringRandomTick(Level& level, Weathering kind, const BlockPos& pos, StateId state);

// Vanilla FarmBlock: a solid block put on it turns it to dirt a tick later; so does a fall
// onto it (a player falling more than 0.5 blocks, by chance).
void farmlandShapeUpdated(Level& level, const BlockPos& pos, StateId state, Direction dir);
void farmlandTick(Level& level, const BlockPos& pos, StateId state);
void trampleFarmland(Level& level, const BlockPos& pos, double fallDistance);

// Vanilla SpongeBlock.tryAbsorbWater: a dry sponge placed in or next to water takes the
// water within 6 blocks (up to 64 of them) and becomes wet.
void spongeUpdate(Level& level, const BlockPos& pos, StateId state);

// Copper's next weathering stage ("copper_block" -> "exposed_copper"); empty when none.
std::string copperNext(std::string_view id);

} // namespace sim
} // namespace mc
