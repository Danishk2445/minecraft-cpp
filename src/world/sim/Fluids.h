// Fluids.h -- water and lava flowing in a live level (vanilla FlowingFluid, LiquidBlock,
// WaterFluid and LavaFluid): spreading down and to the sides toward the nearest drop,
// new sources between two others, lava meeting water, and the current that pushes
// entities.
#pragma once

#include <glm/glm.hpp>

#include "world/block/BlockGetter.h"

namespace mc {

class Level;

namespace sim {

// Vanilla's fluid state: amount 1..8 (sources and falling fluid are 8).
struct Fluid {
    FluidKind kind = FluidKind::None;
    int amount = 0;
    bool falling = false;
    bool source = false;
    bool empty() const { return kind == FluidKind::None; }
    float height() const { return float(amount) / 9.0f; } // vanilla getOwnHeight
};
Fluid fluidOf(const BlockRegistry& registry, StateId state);

// Per fluid: how far it reaches (drop-off per block), how far it looks for a way down,
// and how many ticks it waits between steps.
int fluidDropOff(FluidKind kind);
int fluidSlopeFind(FluidKind kind);
int fluidTickDelay(FluidKind kind);

// The water or lava block for a fluid state (vanilla createLegacyBlock): level 0 for a
// source, 8 - amount when flowing, 8 when falling.
StateId liquidBlock(const BlockRegistry& registry, FluidKind kind, int amount, bool falling);

// Vanilla FlowingFluid.tick: a flowing fluid takes its new level from its neighbors (or
// dries up), then the fluid spreads.
void fluidTick(Level& level, const BlockPos& pos, StateId state);
// Vanilla LiquidBlock.shouldSpreadLiquid: lava touching water (above or beside) hardens
// into obsidian (a source) or cobblestone; false when it did.
bool lavaHardens(Level& level, const BlockPos& pos, StateId state);
// Can fluid flow into the open cell next to `pos` in `dir` (down or sideways)? For
// starting flows in freshly generated chunks.
bool canFlowOut(const Level& level, const BlockPos& pos, StateId state, Direction dir);

// Vanilla FlowingFluid.getFlow: the current at `pos`, a unit vector (zero in still fluid).
glm::dvec3 fluidFlow(const BlockGetter& level, const BlockPos& pos);

} // namespace sim
} // namespace mc
