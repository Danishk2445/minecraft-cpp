// ChunkGenerator.h -- world generation interface used by the staged pipeline.
//
// Both stages are pure functions of (seed, chunk position, inputs) and must be
// thread-safe: the pipeline runs them on worker threads in any order.
#pragma once

#include <array>
#include <memory>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "core/Math.h"
#include "world/chunk/Chunk.h"

namespace mc {

class BlockRegistry;

enum BlockWriteFlag : uint8_t {
    kWriteAlways = 0,
    kWriteIfReplaceable = 1 << 0, // only into air, water or replaceable plants
};

struct BlockWrite {
    BlockPos pos; // world position, within the 3x3 chunks around the source
    StateId state;
    uint8_t step;  // decoration step: lower steps apply first
    uint8_t flags; // BlockWriteFlag
};

struct DecorationResult {
    ChunkPos source;
    std::vector<BlockWrite> writes;
};

// The TERRAIN snapshots of the 3x3 chunks around a center, index (dz+1)*3+(dx+1).
struct TerrainNeighborhood {
    ChunkPos center;
    std::array<std::shared_ptr<const Chunk>, 9> chunks;

    const Chunk* chunkAt(ChunkPos p) const {
        const int dx = p.x - center.x, dz = p.z - center.z;
        if (dx < -1 || dx > 1 || dz < -1 || dz > 1) return nullptr;
        return chunks[size_t((dz + 1) * 3 + dx + 1)].get();
    }
    StateId get(const BlockPos& p) const {
        const Chunk* c = chunkAt(ChunkPos::of(p));
        return c ? c->get(p.x & 15, p.y, p.z & 15) : StateId(0);
    }
};

class ChunkGenerator {
public:
    virtual ~ChunkGenerator() = default;
    virtual std::string_view type() const = 0;
    // TERRAIN: fills a fresh chunk.
    virtual void generateTerrain(Chunk& chunk) const = 0;
    // DECORATE: features that originate in `center` (may write into its neighbors).
    virtual void decorate(const TerrainNeighborhood& terrain, DecorationResult& out) const { (void)terrain, (void)out; }
    virtual glm::dvec3 spawnPoint() const = 0;
};

// Superflat: layers from the bottom of the world upward.
class FlatGenerator final : public ChunkGenerator {
public:
    struct Layer {
        StateId state;
        int count;
    };
    // Default layers: bedrock, 2 dirt, grass block (vanilla "Classic Flat").
    explicit FlatGenerator(const BlockRegistry& registry);
    FlatGenerator(const BlockRegistry& registry, std::vector<Layer> layers);
    std::string_view type() const override { return "flat"; }
    void generateTerrain(Chunk& chunk) const override;
    glm::dvec3 spawnPoint() const override;

private:
    std::vector<Layer> layers_;
};

// Vanilla debug world: every block state on a grid at y=70 (odd x and z), over a
// barrier floor at y=60.
class DebugGenerator final : public ChunkGenerator {
public:
    explicit DebugGenerator(const BlockRegistry& registry);
    std::string_view type() const override { return "debug"; }
    void generateTerrain(Chunk& chunk) const override;
    glm::dvec3 spawnPoint() const override { return {-5.0, 75.0, -5.0}; }
    StateId stateAt(int x, int z) const;
    int gridWidth() const { return width_; }

private:
    std::vector<StateId> states_;
    StateId barrier_ = 0;
    int width_ = 1, height_ = 1;
};

} // namespace mc
