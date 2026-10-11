// Vegetation.cpp -- vanilla land vegetation feature types: bamboo, vines and
// forest_rock (BlockBlobFeature), and the registration of every vegetation
// family. Grass, flowers, bushes, crops, cacti and sugar cane need no code of
// their own: they are random_patch, simple_block and block_column
// configurations in data/worldgen/features/vegetation.json.
#include "world/gen/feature/Vegetation.h"

#include <array>
#include <format>

#include "world/block/BlockRules.h"

namespace mc {
namespace {

// ---------------------------------------------------------------- bamboo

// Vanilla BambooFeature: a stalk 5..16 tall with a leafy top, standing on a
// podzol disk of radius 1..4 with probability `probability`.
class BambooFeature final : public Feature {
public:
    float podzolChance = 0.0f;
    StateId bamboo = 0, trunk = 0, finalLarge = 0, topLarge = 0, topSmall = 0, podzol = 0;
    TagId dirt = 0;

    bool place(FeatureContext& ctx) const override {
        WorldGenRegion& level = ctx.level;
        Xoroshiro& r = ctx.random;
        const BlockPos o = ctx.origin;
        if (!level.isAir(o)) return false;
        if (level.registry().rules().canSurvive(level, bamboo, o)) {
            const int height = r.nextInt(12) + 5;
            if (r.nextFloat() < podzolChance) {
                const int rad = r.nextInt(4) + 1;
                for (int dx = -rad; dx <= rad; ++dx)
                    for (int dz = -rad; dz <= rad; ++dz) {
                        if (dx * dx + dz * dz > rad * rad) continue;
                        const int x = o.x + dx, z = o.z + dz;
                        const BlockPos ground(x, level.height(Heightmap::WorldSurface, x, z) - 1, z);
                        if (level.hasTag(ground, dirt)) level.setBlock(ground, podzol);
                    }
            }
            BlockPos p = o;
            for (int i = 0; i < height && level.isAir(p); ++i, p = p.above()) level.setBlock(p, trunk);
            // The top replaces whatever stopped the stalk, as in vanilla.
            if (p.y - o.y >= 3) {
                level.setBlock(p, finalLarge);
                level.setBlock(p.below(), topLarge);
                level.setBlock(p.below(2), topSmall);
            }
        }
        return true;
    }
};

std::unique_ptr<Feature> makeBamboo(const Json& c, FeatureLoader& l) {
    auto f = std::make_unique<BambooFeature>();
    f->podzolChance = l.getFloat(c, "probability", 0.0f);
    auto bamboo = l.state(Json("bamboo"));
    auto trunk = l.state(Json("bamboo[age=1,leaves=none,stage=0]"));
    auto finalLarge = l.state(Json("bamboo[age=1,leaves=large,stage=1]"));
    auto topLarge = l.state(Json("bamboo[age=1,leaves=large,stage=0]"));
    auto topSmall = l.state(Json("bamboo[age=1,leaves=small,stage=0]"));
    auto podzol = l.state(Json("podzol"));
    auto dirt = l.tag(Json("dirt"));
    if (!bamboo || !trunk || !finalLarge || !topLarge || !topSmall || !podzol || !dirt) return nullptr;
    f->bamboo = *bamboo;
    f->trunk = *trunk;
    f->finalLarge = *finalLarge;
    f->topLarge = *topLarge;
    f->topSmall = *topSmall;
    f->podzol = *podzol;
    f->dirt = *dirt;
    return f;
}

// ---------------------------------------------------------------- vines

// Vanilla VinesFeature: a single vine in an air block, hanging from the block
// above or else clinging to the first sturdy side (north, south, west, east).
class VinesFeature final : public Feature {
public:
    std::array<StateId, 6> vine{}; // by Direction: the vine attached toward that side

    bool place(FeatureContext& ctx) const override {
        WorldGenRegion& level = ctx.level;
        const BlockPos o = ctx.origin;
        if (!level.isAir(o)) return false;
        for (Direction d : kDirections) {
            if (d == Direction::Down || !level.sturdy(o.offset(d), opposite(d))) continue;
            level.setBlock(o, vine[size_t(d)]);
            return true;
        }
        return false;
    }
};

std::unique_ptr<Feature> makeVines(const Json&, FeatureLoader& l) {
    auto f = std::make_unique<VinesFeature>();
    for (Direction d : kDirections) {
        if (d == Direction::Down) continue;
        auto s = l.state(Json(std::format("vine[{}=true]", directionName(d))));
        if (!s) return nullptr;
        f->vine[size_t(d)] = *s;
    }
    return f;
}

// ---------------------------------------------------------------- forest_rock

// Vanilla BlockBlobFeature: sinks to the first dirt or stone below, then places
// three small overlapping balls, each one stepping down and sideways.
class BlockBlobFeature final : public Feature {
public:
    StateId state = 0;
    TagId dirt = 0, stone = 0;

    bool place(FeatureContext& ctx) const override {
        WorldGenRegion& level = ctx.level;
        Xoroshiro& r = ctx.random;
        BlockPos p = ctx.origin;
        for (; p.y > kWorldMinY + 3; p = p.below()) {
            const BlockPos below = p.below();
            if (!level.isAir(below) && (level.hasTag(below, dirt) || level.hasTag(below, stone))) break;
        }
        if (p.y <= kWorldMinY + 3) return false;
        for (int i = 0; i < 3; ++i) {
            const int rx = r.nextInt(2), ry = r.nextInt(2), rz = r.nextInt(2);
            const float f = float(rx + ry + rz) * 0.333f + 0.5f;
            for (int dy = -ry; dy <= ry; ++dy)
                for (int dz = -rz; dz <= rz; ++dz)
                    for (int dx = -rx; dx <= rx; ++dx)
                        if (double(dx * dx + dy * dy + dz * dz) <= double(f * f))
                            level.setBlock(p.offset(dx, dy, dz), state);
            const int ox = r.nextInt(2) - 1;
            const int oy = -r.nextInt(2);
            p = p.offset(ox, oy, r.nextInt(2) - 1);
        }
        return true;
    }
};

std::unique_ptr<Feature> makeBlockBlob(const Json& c, FeatureLoader& l) {
    auto state = l.state(c.value("state", Json()));
    auto dirt = l.tag(Json("dirt"));
    auto stone = l.tag(Json("base_stone_overworld"));
    if (!state || !dirt || !stone) return nullptr;
    auto f = std::make_unique<BlockBlobFeature>();
    f->state = *state;
    f->dirt = *dirt;
    f->stone = *stone;
    return f;
}

} // namespace

void registerVegetationFeatures(FeatureSet& set) {
    set.registerFeature("bamboo", makeBamboo);
    set.registerFeature("vines", makeVines);
    set.registerFeature("forest_rock", makeBlockBlob);
    registerAquaticFeatures(set);
    registerIceFeatures(set);
}

} // namespace mc
