// CaveCommon.h -- helpers shared by the cave feature files (CaveFeatures.cpp,
// CaveDripstone.cpp): vanilla Column.scan and the dripstone registration hook.
#pragma once

#include <optional>

#include "world/gen/feature/Feature.h"

namespace mc {

// Vanilla Column: the blocks that end a run of "inside" blocks above and below a
// start position (the ceiling and the floor themselves), when found in range.
struct CaveColumn {
    std::optional<int> floor, ceiling;

    // Inside blocks between floor and ceiling (vanilla Column.Range.height()).
    std::optional<int> height() const {
        if (!floor || !ceiling) return std::nullopt;
        return *ceiling - *floor - 1;
    }
};

// Vanilla Column.scan: nothing unless `pos` is inside; then each way up to
// range - 1 steps through inside blocks, keeping the end block if it is outside.
template <class Inside, class Outside>
std::optional<CaveColumn> scanColumn(const BlockGetter& level, const BlockPos& pos, int range, Inside inside,
                                     Outside outside) {
    if (!inside(level.getBlock(pos))) return std::nullopt;
    auto scan = [&](int dy) -> std::optional<int> {
        BlockPos p = pos;
        for (int k = 1; k < range && inside(level.getBlock(p)); ++k) p.y += dy;
        if (outside(level.getBlock(p))) return p.y;
        return std::nullopt;
    };
    CaveColumn c;
    c.ceiling = scan(1);
    c.floor = scan(-1);
    return c;
}

// Vanilla Mth.randomBetweenInclusive (callers guarantee lo <= hi).
inline int randomBetweenInclusive(Xoroshiro& r, int lo, int hi) { return r.nextInt(hi - lo + 1) + lo; }

void registerDripstoneFeatures(FeatureSet& set); // dripstone_cluster, large_dripstone, pointed_dripstone

} // namespace mc
