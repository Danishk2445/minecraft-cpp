// BiomeDecorator.h -- runs each biome's features over a chunk (vanilla
// ChunkGenerator.applyBiomeDecoration).
//
// data/worldgen/biome_features.json names the decoration steps and defines
// feature sets: per step, an ordered list of placed features, optionally
// including other sets first. biomes.json picks each biome's set. Within a step
// every feature has one global index, a stable topological merge of all the
// sets' lists, so each biome's own order holds. A chunk runs, step by step and
// in that global order, every feature that any of its biomes has; the `biome`
// placement filter then keeps positions whose own biome has it.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "world/gen/feature/Feature.h"

namespace mc {

class DecorationRegion;

class BiomeDecorator {
public:
    bool load(const std::filesystem::path& dataDir, const BlockRegistry& blocks, const BiomeRegistry& biomes,
              std::string* errors);

    // Decorates the region's center chunk.
    void decorate(DecorationRegion& region) const;

    bool biomeHas(BiomeId biome, uint8_t step, uint16_t feature) const;
    const FeatureSet& features() const { return features_; }
    size_t stepCount() const { return steps_.size(); }
    const std::vector<const PlacedFeature*>& order(size_t step) const { return order_[step]; }
    std::optional<size_t> stepIndex(std::string_view name) const;
    // Vanilla BiomeGenerationSettings.getFlowerFeatures: the single flowers of the biome's
    // "flower" patches, in decoration order (what bone meal on grass may place).
    const std::vector<const PlacedFeature*>& flowerFeatures(BiomeId biome) const;

private:
    FeatureSet features_;
    std::vector<std::string> steps_;
    std::vector<std::vector<const PlacedFeature*>> order_; // per step, by global index
    std::vector<size_t> stepBase_;                         // bit offset of each step in a membership row
    std::vector<std::vector<uint64_t>> membership_;        // per biome: bit per (step, feature)
    std::vector<std::vector<const PlacedFeature*>> flowers_; // per biome
};

} // namespace mc
