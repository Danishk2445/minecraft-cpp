// FeatureUtil.h -- helpers shared by feature implementations.
#pragma once

#include <optional>
#include <string_view>

#include "world/gen/feature/WorldGenRegion.h"

namespace mc {

// Places a block the way vanilla features do: double plants (blocks with
// half=lower/upper) as both halves when the space above is free, and
// waterloggable blocks waterlogged when they replace water. Returns false if
// nothing was placed.
bool placeBlock(WorldGenRegion& level, StateId state, const BlockPos& pos, WritePolicy policy = kWriteIfUnchanged);

// `state` with property `prop` set to `value`, or `state` unchanged if it lacks it.
StateId withProp(const BlockRegistry& reg, StateId state, std::string_view prop, std::string_view value);
StateId withProp(const BlockRegistry& reg, StateId state, std::string_view prop, int value);
StateId withProp(const BlockRegistry& reg, StateId state, std::string_view prop, bool value);

// Cached tags and blocks that tree and plant features test often.
struct FeatureTags {
    explicit FeatureTags(const BlockRegistry& reg);
    TagId dirt, leaves, logs, replaceableByTrees, sand, baseStone, moss, lushGround, azaleaRoots, dripstoneReplaceable,
        featuresCannotReplace, ice, snow;
    uint16_t air, water, lava, grassBlock, podzol, mycelium, farmland, moss_block, clay, vine, snowLayer;

    bool isDirt(const BlockGetter& l, const BlockPos& p) const { return l.hasTag(p, dirt); }
    // Vanilla TreeFeature.validTreePos: air, leaves or replaceable_by_trees.
    bool validTreePos(const BlockGetter& l, const BlockPos& p) const {
        const StateId s = l.getBlock(p);
        const BlockRegistry& r = l.registry();
        return (r.info(s).flags & kAir) || r.hasTag(s, leaves) || r.hasTag(s, replaceableByTrees);
    }
    bool isAirOrLeaves(const BlockGetter& l, const BlockPos& p) const {
        const StateId s = l.getBlock(p);
        return (l.registry().info(s).flags & kAir) || l.registry().hasTag(s, leaves);
    }
};
const FeatureTags& featureTags(const BlockRegistry& reg);

} // namespace mc
