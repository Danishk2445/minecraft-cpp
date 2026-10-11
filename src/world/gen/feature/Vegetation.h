// Vegetation.h -- internals shared by the Vegetation*.cpp feature families:
// land plants (Vegetation.cpp), water plants and coral (VegetationAquatic.cpp),
// icebergs and ice spikes (VegetationIce.cpp).
#pragma once

#include <optional>

#include "world/gen/feature/Feature.h"

namespace mc {

void registerAquaticFeatures(FeatureSet& set); // seagrass, kelp, sea_pickle, coral_tree/claw/mushroom
void registerIceFeatures(FeatureSet& set);     // iceberg, blue_ice, ice_spike

namespace veg {

// Java's `r.nextInt(n) - r.nextInt(n)`, evaluated left to right (C++ leaves the
// order of the two calls unspecified, which would make worlds compiler-dependent).
inline int triangle(Xoroshiro& r, int n) {
    const int a = r.nextInt(n);
    return a - r.nextInt(n);
}

// Vanilla Mth.ceil(float).
inline int ceil(float f) {
    const int i = int(f);
    return f > float(i) ? i + 1 : i;
}

// Block index of a required block; reports a load error when the registry lacks it.
inline std::optional<uint16_t> requireBlock(FeatureLoader& l, const char* id) {
    if (const Block* b = l.block(Json(id))) return b->index;
    return std::nullopt;
}

inline bool isBlock(const BlockGetter& level, const BlockPos& p, uint16_t block) {
    return level.info(p).block == block;
}

} // namespace veg
} // namespace mc
