// BlockGetter.h -- read access to blocks by world position, shared by world
// generation regions, block rules (canSurvive) and, later, the live level.
#pragma once

#include "core/Math.h"
#include "world/block/BlockRegistry.h"

namespace mc {

class BlockGetter {
public:
    virtual ~BlockGetter() = default;
    virtual const BlockRegistry& registry() const = 0;
    virtual StateId getBlock(const BlockPos& p) const = 0;

    const StateInfo& info(const BlockPos& p) const { return registry().info(getBlock(p)); }
    bool isAir(const BlockPos& p) const { return (info(p).flags & kAir) != 0; }
    FluidState fluid(const BlockPos& p) const { return info(p).fluid; }
    bool isWaterSource(const BlockPos& p) const {
        const FluidState f = fluid(p);
        return f.kind == FluidKind::Water && f.source();
    }
    bool hasTag(const BlockPos& p, TagId t) const { return registry().hasTag(getBlock(p), t); }
    // The block's face toward `face` is fully covered (vanilla isFaceSturdy, approximated).
    bool sturdy(const BlockPos& p, Direction face) const { return (info(p).sturdyFaces >> int(face)) & 1u; }
};

} // namespace mc
