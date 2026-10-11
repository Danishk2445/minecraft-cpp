// Tints.h -- which color a tinted face gets (vanilla BlockColors) and the
// grass/foliage colormap lookup.
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "core/Image.h"

namespace mc {

enum class TintSource : uint8_t {
    None,       // white
    Grass,      // biome grass color
    Foliage,    // biome foliage color
    DryFoliage, // biome dry-foliage color (leaf litter)
    Water,      // biome water color
    Constant,   // fixed color (birch/spruce leaves, lily pad, attached stems)
    Redstone,   // by "power" 0..15
    Stem,       // by "age" 0..7
};

struct TintRule {
    TintSource source = TintSource::None;
    uint32_t constant = 0xFFFFFF; // 0xRRGGBB for Constant
    int onlyTintIndex = -1;       // >= 0: only faces with this tintindex are tinted
};

// Tint rule for a block id ("minecraft:oak_leaves").
TintRule tintRuleFor(std::string_view blockId);

uint32_t redstoneColor(int power); // 0xRRGGBB
uint32_t stemColor(int age);       // 0xRRGGBB

struct BiomeColors {
    uint32_t grass = 0x91BD59;
    uint32_t foliage = 0x77AB2F;
    uint32_t dryFoliage = 0xA67B4A;
    uint32_t water = 0x3F76E4;
};

// Resolves a face's tint to 0xRRGGBB (0xFFFFFF when untinted).
uint32_t resolveTint(const TintRule& rule, int tintIndex, const BiomeColors& biome, int power = 0, int age = 0);

// textures/colormap/*.png lookup: x = (1-T)*255, y = (1-D*T)*255, T and D clamped to [0,1].
class Colormap {
public:
    Colormap() = default;
    explicit Colormap(Image image) : image_(std::move(image)) {}
    bool valid() const { return !image_.empty(); }
    uint32_t sample(double temperature, double downfall, uint32_t fallback = 0xFF00FF) const;

private:
    Image image_;
};

} // namespace mc
