// Plants.h -- plants growing in a live level (vanilla randomTick and performBonemeal of
// crops, stems, saplings, sugar cane, cactus, kelp and cave vines, bamboo, vines, sweet
// berries, cocoa and mushrooms), grass and mycelium spreading, leaves keeping their
// distance to a log and decaying without one, and bone meal.
#pragma once

#include <cstdint>

#include "world/block/BlockGetter.h"

namespace mc {

class Level;

namespace sim {

enum class Plant : uint8_t {
    None,
    Crop,      // wheat, carrots, potatoes, beetroots, torchflower crop, nether wart
    Stem,      // pumpkin and melon stems
    Sapling,
    SugarCane,
    Cactus,
    Kelp,      // kelp (the head; kelp_plant is the body)
    CaveVines, // cave_vines (the head; cave_vines_plant is the body)
    Bamboo,
    BambooSapling,
    Vine,
    BerryBush,
    Cocoa,
    Mushroom,  // brown and red mushrooms
    Grass,     // grass blocks and mycelium: spread to dirt, die in the dark
    Leaves,
};
Plant plantOf(const Block& block);

// Whether `state` takes random ticks (vanilla isRandomlyTicking), and one random tick.
bool plantTicksRandomly(const BlockRegistry& registry, Plant plant, StateId state);
void plantRandomTick(Level& level, Plant plant, const BlockPos& pos, StateId state);

// Vanilla LeavesBlock: a neighbor changing schedules a distance check a tick later, which
// takes the distance to the nearest log (7 when none is within 6).
void leavesShapeUpdated(Level& level, const BlockPos& pos, StateId state, StateId neighbor);
void leavesTick(Level& level, const BlockPos& pos, StateId state);
// Vanilla GrowingPlantHead/BodyBlock.updateShape: kelp and cave vines turn into their body
// under (over) a new head, and back into a head when the plant above (below) goes.
void growingPlantShapeUpdated(Level& level, const BlockPos& pos, StateId state, Direction dir, StateId neighbor);

// Vanilla TreeGrower.growTree: the sapling at `pos` becomes its tree (2x2 saplings their
// big tree); false when the tree didn't fit.
bool growTree(Level& level, const BlockPos& pos, StateId sapling);

// Vanilla BoneMealItem.useOn: bone meal on the block at `pos` clicked on `face`, or into
// the water in front of it; true when some was used. Emits a BoneMeal level event.
bool applyBoneMeal(Level& level, const BlockPos& pos, Direction face);

} // namespace sim
} // namespace mc
