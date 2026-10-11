// OverworldGenerator.h -- 1.18-style overworld terrain (TERRAIN stage): climate
// noises, terrain splines, 3D density with caves, aquifer-lite fluids, surface
// rules, bedrock and carvers. Vanilla-like, not seed-compatible.
#pragma once

#include <array>
#include <memory>
#include <string>

#include "core/Noise.h"
#include "world/biome/Biome.h"
#include "world/gen/BiomeLayout.h"
#include "world/gen/ChunkGenerator.h"

namespace mc {

class BlockRegistry;

class OverworldGenerator final : public ChunkGenerator {
public:
    static constexpr int kSeaLevel = 63;

    OverworldGenerator(const BlockRegistry& blocks, const BiomeRegistry& biomes, int64_t seed);
    ~OverworldGenerator() override;

    std::string_view type() const override { return "default"; }
    void generateTerrain(Chunk& chunk) const override;
    void decorate(const TerrainNeighborhood& terrain, DecorationResult& out) const override;
    glm::dvec3 spawnPoint() const override;

    // Climate at a block column (y-independent parameters) plus depth at y.
    ClimatePoint climateAt(int x, int y, int z) const;
    BiomeId biomeAt(int x, int y, int z) const;
    // Estimated terrain height from the splines alone (no 3D noise or caves).
    int estimatedSurface(int x, int z) const;
    // Highest terrain block of a column after the TERRAIN stage (generates its chunk);
    // a column under water reports sea level - 1.
    int terrainTop(int x, int z) const;
    const BiomeRegistry& biomes() const { return biomes_; }
    // Diagnostics: climate, spline outputs and density samples down a column.
    std::string describeColumn(int x, int z) const;
    int64_t seed() const { return seed_; }

    struct Impl;

private:
    const BlockRegistry& blocks_;
    const BiomeRegistry& biomes_;
    int64_t seed_;
    std::unique_ptr<Impl> impl_;
    glm::dvec3 spawn_;
};

} // namespace mc
