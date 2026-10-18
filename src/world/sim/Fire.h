// Fire.h -- fire in a live level (vanilla FireBlock and BaseFireBlock): it ages, burns
// out its neighbors by their burn odds, spreads to air beside things that catch by their
// ignite odds, dies in the rain and without fuel, and lights TNT; lava sets things alight
// (LavaFluid.randomTick).
#pragma once

#include "world/block/BlockGetter.h"

namespace mc {

class Level;

namespace sim {

// Vanilla FireBlock.getFireTickDelay: 30-39 ticks between a fire's ticks.
int fireTickDelay(Level& level);
void fireOnPlace(Level& level, const BlockPos& pos, StateId state, StateId old);
void fireTick(Level& level, const BlockPos& pos, StateId state);
// Vanilla LavaFluid.randomTick: (doFireTick) lava lights the air above it next to
// burnable blocks.
void lavaRandomTick(Level& level, const BlockPos& pos);
// Lights fire in the empty cell at `pos` if fire can stay there (lightning, flint and
// steel); true when it did.
bool ignite(Level& level, const BlockPos& pos);

} // namespace sim
} // namespace mc
