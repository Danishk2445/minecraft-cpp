// BlockBehavior.h -- vanilla Block.updateShape: how a block reacts when a
// neighbor changes (fence/pane/wall connections, stair corners, chest pairs,
// door and double-plant halves, snowy grass, losing support).
#pragma once

#include <optional>

#include "world/block/BlockGetter.h"

namespace mc {

// Fences, panes and walls joined to their four neighbors as vanilla would place them.
StateId withConnections(const BlockRegistry& reg, const BlockGetter& level, StateId self, const BlockPos& pos);
// Stairs with the straight/inner/outer shape their neighbors give.
StateId withStairsShape(const BlockRegistry& reg, const BlockGetter& level, StateId self, const BlockPos& pos);

// The state `self` at `pos` takes after its neighbor in direction `dir` became
// `neighbor`. Air means the block breaks (no support, other half gone).
StateId updateShape(const BlockRegistry& reg, const BlockGetter& level, StateId self, const BlockPos& pos, Direction dir,
                    StateId neighbor);

// The state after a scheduled tick (buttons spring back); `self` if nothing changes.
StateId scheduledTickResult(const BlockRegistry& reg, const BlockGetter& level, StateId self, const BlockPos& pos);

struct UseResult {
    StateId state;
    int tickDelay = 0; // schedule a tick this many game ticks later (0: none)
};
// Vanilla hand use of doors, trapdoors, fence gates, levers and buttons (iron doors
// and trapdoors need redstone). `playerFacing` is the player's horizontal direction.
std::optional<UseResult> useBlock(const BlockRegistry& reg, StateId self, Direction playerFacing);

} // namespace mc
