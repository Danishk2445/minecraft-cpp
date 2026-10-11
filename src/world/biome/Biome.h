// Biome.h -- biome definitions (data/worldgen/biomes.json) and their colors.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "world/block/Tints.h"

namespace mc {

using BiomeId = uint8_t;

enum class GrassModifier : uint8_t { None, Swamp, DarkForest };

struct Biome {
    std::string id; // "minecraft:plains"
    BiomeId index = 0;
    float temperature = 0.5f;
    float downfall = 0.5f;
    bool precipitation = true;
    uint32_t waterColor = 0x3F76E4;
    uint32_t waterFogColor = 0x050533;
    uint32_t fogColor = 0xC0D8FF;
    uint32_t skyColor = 0x78A7FF;
    GrassModifier grassModifier = GrassModifier::None;
    std::string surface;  // surface rule name
    std::string features; // feature group (M5)
    // Resolved colors (colormap or override).
    uint32_t grassColor = 0, foliageColor = 0, dryFoliageColor = 0;

    // Snow instead of rain at this height (vanilla height-adjusted temperature, no noise).
    bool coldAt(int y) const;
};

class BiomeRegistry {
public:
    // Reads dataDir/worldgen/biomes.json; colormaps come from the resource pack.
    bool load(const std::filesystem::path& dataDir, const std::filesystem::path& packRoot, std::string* error);

    size_t size() const { return biomes_.size(); }
    const Biome& get(BiomeId id) const { return biomes_[id]; }
    std::optional<BiomeId> find(std::string_view id) const; // namespace optional
    BiomeId require(std::string_view id) const;           // plains if unknown

    // Grass/foliage/water colors at a block column (the swamp modifier varies with x/z).
    BiomeColors colors(BiomeId id, int x, int z) const;

private:
    std::vector<Biome> biomes_;
    std::unordered_map<std::string, BiomeId> index_;
};

// Vanilla Biome.calculateSkyColor.
uint32_t skyColorForTemperature(float temperature);

} // namespace mc
