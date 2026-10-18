// LiveRegion.h -- the live level as world generation's features see it, so saplings
// and bone meal grow the same trees, mushrooms and plant patches the world generates.
// Writes go straight into the level; finish() then gives the changed blocks and their
// neighbors the updates a normal setBlock would (connections, support, fluid ticks).
#pragma once

#include <vector>

#include "world/gen/feature/WorldGenRegion.h"

namespace mc {

class Level;

namespace sim {

class LiveRegion final : public WorldGenRegion {
public:
    LiveRegion(Level& level, const BiomeRegistry& biomes);
    ~LiveRegion() override;

    const BlockRegistry& registry() const override;
    StateId getBlock(const BlockPos& p) const override;
    bool setBlock(const BlockPos& p, StateId s, WritePolicy policy = kWriteIfUnchanged) override;
    bool canWrite(const BlockPos& p) const override;
    int height(Heightmap type, int x, int z) const override;
    BiomeId biome(const BlockPos& p) const override;
    const BiomeRegistry& biomes() const override { return biomes_; }
    int64_t seed() const override;

    // Neighbor and shape updates around everything written (once; also on destruction).
    void finish();

private:
    struct Change {
        BlockPos pos;
        StateId old;
    };
    Level& level_;
    const BiomeRegistry& biomes_;
    TagId leaves_ = 0;
    std::vector<Change> changes_;
};

// Places the named feature (its configuration, without its placement rules) at
// `origin`, as saplings and bone meal do; false if the level has no features or the
// feature didn't fit.
bool placeFeature(Level& level, std::string_view name, const BlockPos& origin);

} // namespace sim
} // namespace mc
