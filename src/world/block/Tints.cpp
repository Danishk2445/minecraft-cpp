#include "world/block/Tints.h"

#include <algorithm>
#include <array>
#include <string>

namespace mc {

TintRule tintRuleFor(std::string_view id) {
    if (id.starts_with("minecraft:")) id.remove_prefix(10);
    static constexpr std::array<std::string_view, 8> grass = {
        "grass_block", "short_grass", "tall_grass", "fern", "large_fern", "potted_fern", "bush", "sugar_cane"};
    if (std::find(grass.begin(), grass.end(), id) != grass.end()) return {TintSource::Grass};
    if (id == "pink_petals" || id == "wildflowers") return {TintSource::Grass, 0xFFFFFF, 1};
    static constexpr std::array<std::string_view, 6> foliage = {"oak_leaves",      "jungle_leaves",   "acacia_leaves",
                                                                "dark_oak_leaves", "mangrove_leaves", "vine"};
    if (std::find(foliage.begin(), foliage.end(), id) != foliage.end()) return {TintSource::Foliage};
    if (id == "birch_leaves") return {TintSource::Constant, 0x80A755};
    if (id == "spruce_leaves") return {TintSource::Constant, 0x619961};
    if (id == "lily_pad") return {TintSource::Constant, 0x208030};
    if (id == "leaf_litter") return {TintSource::DryFoliage};
    if (id == "water" || id == "bubble_column" || id == "water_cauldron") return {TintSource::Water};
    if (id == "redstone_wire") return {TintSource::Redstone};
    if (id == "melon_stem" || id == "pumpkin_stem") return {TintSource::Stem};
    if (id == "attached_melon_stem" || id == "attached_pumpkin_stem") return {TintSource::Constant, 0xE0C71C};
    return {};
}

uint32_t redstoneColor(int power) {
    const float f = float(std::clamp(power, 0, 15)) / 15.0f;
    const float r = f * 0.6f + (f > 0.0f ? 0.4f : 0.3f);
    const float g = std::clamp(f * f * 0.7f - 0.5f, 0.0f, 1.0f);
    const float b = std::clamp(f * f * 0.6f - 0.7f, 0.0f, 1.0f);
    return (uint32_t(r * 255.0f) << 16) | (uint32_t(g * 255.0f) << 8) | uint32_t(b * 255.0f);
}

uint32_t stemColor(int age) {
    age = std::clamp(age, 0, 7);
    return (uint32_t(age * 32) << 16) | (uint32_t(255 - age * 8) << 8) | uint32_t(age * 4);
}

uint32_t resolveTint(const TintRule& rule, int tintIndex, const BiomeColors& biome, int power, int age) {
    if (tintIndex < 0 || rule.source == TintSource::None) return 0xFFFFFF;
    if (rule.onlyTintIndex >= 0 && tintIndex != rule.onlyTintIndex) return 0xFFFFFF;
    switch (rule.source) {
    case TintSource::Grass: return biome.grass;
    case TintSource::Foliage: return biome.foliage;
    case TintSource::DryFoliage: return biome.dryFoliage;
    case TintSource::Water: return biome.water;
    case TintSource::Constant: return rule.constant;
    case TintSource::Redstone: return redstoneColor(power);
    case TintSource::Stem: return stemColor(age);
    case TintSource::None: break;
    }
    return 0xFFFFFF;
}

uint32_t Colormap::sample(double temperature, double downfall, uint32_t fallback) const {
    if (image_.empty()) return fallback;
    const double t = std::clamp(temperature, 0.0, 1.0);
    const double d = std::clamp(downfall, 0.0, 1.0) * t;
    const int x = int((1.0 - t) * 255.0);
    const int y = int((1.0 - d) * 255.0);
    if (x < 0 || y < 0 || x >= image_.width || y >= image_.height) return fallback;
    const uint8_t* p = image_.px(x, y);
    return (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | uint32_t(p[2]);
}

} // namespace mc
