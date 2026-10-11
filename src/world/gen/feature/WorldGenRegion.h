// WorldGenRegion.h -- the world as features see it: block reads and writes,
// heightmaps and biomes. The DECORATE stage implements it over the TERRAIN
// snapshots of a 3x3 chunk neighborhood (DecorationRegion); the live level will
// implement it for saplings and bone meal.
#pragma once

#include <cstdint>

#include "world/biome/Biome.h"
#include "world/block/BlockGetter.h"
#include "world/block/BlockRegistry.h"
#include "world/chunk/Chunk.h"
#include "world/gen/ChunkGenerator.h"

namespace mc {

// Vanilla heightmap types used by world generation. A heightmap stores, per
// column, one above the highest block that counts (kWorldMinY when none does).
enum class Heightmap : uint8_t {
    WorldSurface,           // any non-air block (vanilla WORLD_SURFACE_WG)
    OceanFloor,             // blocks motion, so fluids and plants do not count (OCEAN_FLOOR_WG)
    MotionBlocking,         // blocks motion or holds a fluid (MOTION_BLOCKING)
    MotionBlockingNoLeaves, // MOTION_BLOCKING minus leaves
};
constexpr int kHeightmapCount = 4;

class WorldGenRegion : public BlockGetter {
public:
    // Writes a block. Returns false (and writes nothing) outside the writable area.
    virtual bool setBlock(const BlockPos& p, StateId s, WritePolicy policy = kWriteIfUnchanged) = 0;
    virtual bool canWrite(const BlockPos& p) const = 0;
    virtual int height(Heightmap type, int x, int z) const = 0;
    virtual BiomeId biome(const BlockPos& p) const = 0;
    virtual const BiomeRegistry& biomes() const = 0;
    virtual int64_t seed() const = 0;
    virtual int seaLevel() const { return 63; }

    static bool inBuildHeight(int y) { return y >= kWorldMinY && y <= kWorldMaxY; }
};

// Whether a state counts toward a heightmap type.
bool countsForHeightmap(const BlockRegistry& reg, TagId leaves, Heightmap type, StateId s);

} // namespace mc
