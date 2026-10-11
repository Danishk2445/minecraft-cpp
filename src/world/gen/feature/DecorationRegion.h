// DecorationRegion.h -- the DECORATE stage's WorldGenRegion: reads the TERRAIN
// snapshots of the 3x3 chunks around the source chunk plus this source's own
// earlier writes, and records every write for MERGE.
#pragma once

#include <array>
#include <memory>

#include "world/gen/feature/WorldGenRegion.h"

namespace mc {

class DecorationRegion final : public WorldGenRegion {
public:
    DecorationRegion(const BlockRegistry& registry, const BiomeRegistry& biomes, const TerrainNeighborhood& terrain,
                     int64_t seed, DecorationResult& out);

    // Tags the following writes (MERGE orders by step, then feature).
    void setFeature(uint8_t step, uint16_t feature) {
        step_ = step;
        feature_ = feature;
    }

    const BlockRegistry& registry() const override { return reg_; }
    StateId getBlock(const BlockPos& p) const override;
    bool setBlock(const BlockPos& p, StateId s, WritePolicy policy = kWriteIfUnchanged) override;
    bool canWrite(const BlockPos& p) const override;
    int height(Heightmap type, int x, int z) const override;
    BiomeId biome(const BlockPos& p) const override;
    const BiomeRegistry& biomes() const override { return biomes_; }
    int64_t seed() const override { return seed_; }

    ChunkPos center() const { return terrain_.center; }

private:
    const Chunk* chunkAt(int x, int z) const;
    // Index into overlay_ for an in-area position (-1 outside the 3x3 or the world).
    int sectionSlot(const BlockPos& p) const;
    StateId terrainBlock(const BlockPos& p) const;
    int columnIndex(int x, int z) const; // -1 outside the 3x3
    int computeHeight(Heightmap type, int x, int z) const;

    const BlockRegistry& reg_;
    const BiomeRegistry& biomes_;
    const TerrainNeighborhood& terrain_;
    int64_t seed_;
    DecorationResult& out_;
    TagId leaves_ = 0;
    int x0_, z0_; // minimum block x/z of the 3x3 area
    uint8_t step_ = 0;
    uint16_t feature_ = 0;
    // This source's own writes, per section of the 3x3 area (allocated on first
    // write; kNoOverlay where untouched), and the highest written y per column.
    static constexpr StateId kNoOverlay = 0xFFFF;
    using SectionOverlay = std::array<StateId, kSectionVolume>;
    std::array<std::unique_ptr<SectionOverlay>, 9 * kSectionCount> overlay_;
    std::array<int16_t, 48 * 48> overlayTop_;
    // Heightmap cache per column of the 48x48 area; kUnknown until first asked.
    static constexpr int16_t kUnknown = INT16_MIN;
    mutable std::array<int16_t, 48 * 48 * kHeightmapCount> heights_;
    mutable std::array<int16_t, 9> chunkTop_; // per snapshot: highest non-air y + 1, kUnknown until asked
};

} // namespace mc
