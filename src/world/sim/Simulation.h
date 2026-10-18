// Simulation.h -- blocks acting on their own in a live level (vanilla's server-side block
// behavior): reactions to being placed and to changing neighbors, scheduled block and
// fluid ticks, and random ticks. The level calls these; pure shape rules (connections,
// support) stay in world/block/BlockBehavior.
#pragma once

#include <cstdint>

#include "world/block/BlockGetter.h"

namespace mc {

class Level;

namespace sim {

// Fluid tick types (vanilla's four fluids: water and lava, each still or flowing). A tick
// runs only if the cell still holds that type.
uint16_t fluidTickType(FluidKind kind, bool source);
uint16_t fluidTickTypeOf(const BlockRegistry& registry, StateId state); // 0: no fluid

// Vanilla Block.onPlace: `state` was just set at `pos` over `old`.
void onPlace(Level& level, const BlockPos& pos, StateId state, StateId old);
// Vanilla neighborChanged: the block next to `pos`, at `from`, changed.
void neighborChanged(Level& level, const BlockPos& pos, StateId state, const BlockPos& from);
// The scheduling side of vanilla updateShape: the neighbor of `pos` (holding `state`) in
// `dir` is now `neighbor`.
void shapeUpdated(Level& level, const BlockPos& pos, StateId state, Direction dir, StateId neighbor);
// Scheduled ticks: a block's, and a fluid's of `type`.
void blockTick(Level& level, const BlockPos& pos, StateId state);
void fluidTick(Level& level, const BlockPos& pos, StateId state, uint16_t type);
// Random ticks (vanilla randomTick of the block, then of its fluid).
bool randomlyTicks(const BlockRegistry& registry, StateId state);
void randomTick(Level& level, const BlockPos& pos, StateId state);
// A freshly generated chunk started ticking: fluids that can flow out start to (vanilla
// postProcessGeneration ticks the fluids the generator marked).
void postProcessChunk(Level& level, ChunkPos chunk);

} // namespace sim
} // namespace mc
