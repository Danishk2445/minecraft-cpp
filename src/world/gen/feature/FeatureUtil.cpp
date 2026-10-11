#include "world/gen/feature/FeatureUtil.h"

#include <memory>
#include <mutex>
#include <unordered_map>

namespace mc {

StateId withProp(const BlockRegistry& reg, StateId state, std::string_view prop, std::string_view value) {
    return reg.with(state, prop, value).value_or(state);
}
StateId withProp(const BlockRegistry& reg, StateId state, std::string_view prop, int value) {
    return withProp(reg, state, prop, std::string_view(std::to_string(value)));
}
StateId withProp(const BlockRegistry& reg, StateId state, std::string_view prop, bool value) {
    return withProp(reg, state, prop, value ? std::string_view("true") : std::string_view("false"));
}

bool placeBlock(WorldGenRegion& level, StateId state, const BlockPos& pos, WritePolicy policy) {
    const BlockRegistry& reg = level.registry();
    auto waterlog = [&](StateId s, const BlockPos& p) {
        if (!reg.value(s, "waterlogged").empty()) return withProp(reg, s, "waterlogged", level.isWaterSource(p));
        return s;
    };
    if (reg.value(state, "half") == "lower") {
        const BlockPos up = pos.above();
        if (!level.isAir(up) && !level.isWaterSource(up)) return false;
        const StateId upper = withProp(reg, state, "half", std::string_view("upper"));
        if (!level.setBlock(pos, waterlog(state, pos), policy)) return false;
        level.setBlock(up, waterlog(upper, up), policy);
        return true;
    }
    return level.setBlock(pos, waterlog(state, pos), policy);
}

FeatureTags::FeatureTags(const BlockRegistry& reg) {
    auto tag = [&](const char* n) { return reg.tag(n).value_or(TagId(0)); };
    auto block = [&](const char* n) -> uint16_t {
        const Block* b = reg.find(n);
        return b ? b->index : uint16_t(0xFFFF);
    };
    dirt = tag("dirt");
    leaves = tag("leaves");
    logs = tag("logs");
    replaceableByTrees = tag("replaceable_by_trees");
    sand = tag("sand");
    baseStone = tag("base_stone_overworld");
    moss = tag("moss_replaceable");
    lushGround = tag("lush_ground_replaceable");
    azaleaRoots = tag("azalea_root_replaceable");
    dripstoneReplaceable = tag("dripstone_replaceable_blocks");
    featuresCannotReplace = tag("features_cannot_replace");
    ice = tag("ice");
    snow = tag("snow");
    air = block("air");
    water = block("water");
    lava = block("lava");
    grassBlock = block("grass_block");
    podzol = block("podzol");
    mycelium = block("mycelium");
    farmland = block("farmland");
    moss_block = block("moss_block");
    clay = block("clay");
    vine = block("vine");
    snowLayer = block("snow");
}

const FeatureTags& featureTags(const BlockRegistry& reg) {
    // One per registry, never freed, so references stay valid on every thread.
    static std::mutex mutex;
    static std::unordered_map<const BlockRegistry*, std::unique_ptr<FeatureTags>> cache;
    std::lock_guard lock(mutex);
    auto& tags = cache[&reg];
    if (!tags) tags = std::make_unique<FeatureTags>(reg);
    return *tags;
}

} // namespace mc
