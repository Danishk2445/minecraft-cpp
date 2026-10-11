// LightEngine.h -- sky and block light (docs/DESIGN.md "Lighting").
//
// computeChunk() is the pipeline's LIGHT stage: light for one chunk computed
// from scratch over its 3x3 neighborhood (exact, since light travels at most 15
// blocks). update() repairs light incrementally after a block change on the live
// world with remove/re-add flood fills. Both follow the same rules, so an
// incremental result always equals a from-scratch recomputation.
#pragma once

#include <unordered_set>
#include <vector>

#include "core/Math.h"
#include "world/block/BlockRegistry.h"
#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkSnapshot.h"

namespace mc {

class ChunkManager;

class LightEngine {
public:
    explicit LightEngine(const BlockRegistry& registry);

    // Fills `out`'s light sections for the center chunk of `n` (which must hold the
    // final blocks of all 9 columns).
    void computeChunk(const Neighborhood& n, Chunk& out) const;

    // After the block at `p` changed in `chunks` (the new state is already set).
    // Adds every position whose light changed to `changed`.
    void update(ChunkManager& chunks, const BlockPos& p, std::vector<BlockPos>& changed) const;

    uint8_t opacity(StateId s) const { return opacity_[s]; }
    uint8_t emission(StateId s) const { return emission_[s]; }
    uint8_t blockedFaces(StateId s) const { return faces_[s]; }

private:
    std::vector<uint8_t> opacity_, emission_, faces_; // per StateId
};

} // namespace mc
