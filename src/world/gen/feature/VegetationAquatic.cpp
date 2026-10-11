// VegetationAquatic.cpp -- vanilla water plant features: seagrass, kelp,
// sea_pickle and the warm-ocean coral reefs (coral_tree, coral_claw, coral_mushroom).
#include <array>
#include <format>

#include "world/block/BlockRules.h"
#include "world/gen/feature/FeatureUtil.h"
#include "world/gen/feature/Providers.h"
#include "world/gen/feature/Vegetation.h"

namespace mc {
namespace {

// Vanilla Util.shuffle: Fisher-Yates from the back.
template <size_t N>
void shuffle(std::array<Direction, N>& a, Xoroshiro& r) {
    for (int j = int(N); j > 1; --j) std::swap(a[size_t(j - 1)], a[size_t(r.nextInt(j))]);
}

Direction counterClockwise(Direction d) { return kHorizontal[size_t((horizontalIndex(d) + 3) % 4)]; }

// ---------------------------------------------------------------- seagrass

// Vanilla SeagrassFeature: one plant on the ocean floor within 7 blocks of the
// origin, tall with `probability`.
class SeagrassFeature final : public Feature {
public:
    float tallChance = 0.0f;
    StateId seagrass = 0, tallLower = 0, tallUpper = 0;
    uint16_t water = 0;

    bool place(FeatureContext& ctx) const override {
        WorldGenRegion& level = ctx.level;
        Xoroshiro& r = ctx.random;
        const int x = ctx.origin.x + veg::triangle(r, 8);
        const int z = ctx.origin.z + veg::triangle(r, 8);
        const BlockPos p(x, level.height(Heightmap::OceanFloor, x, z), z);
        if (!veg::isBlock(level, p, water)) return false;
        const bool tall = r.nextDouble() < tallChance;
        if (!level.registry().rules().canSurvive(level, tall ? tallLower : seagrass, p)) return false;
        if (!tall) {
            level.setBlock(p, seagrass);
        } else if (veg::isBlock(level, p.above(), water)) {
            level.setBlock(p, tallLower);
            level.setBlock(p.above(), tallUpper);
        }
        return true;
    }
};

std::unique_ptr<Feature> makeSeagrass(const Json& c, FeatureLoader& l) {
    auto f = std::make_unique<SeagrassFeature>();
    f->tallChance = l.getFloat(c, "probability", 0.0f);
    auto seagrass = l.state(Json("seagrass"));
    auto lower = l.state(Json("tall_seagrass[half=lower]"));
    auto upper = l.state(Json("tall_seagrass[half=upper]"));
    auto water = veg::requireBlock(l, "water");
    if (!seagrass || !lower || !upper || !water) return nullptr;
    f->seagrass = *seagrass;
    f->tallLower = *lower;
    f->tallUpper = *upper;
    f->water = *water;
    return f;
}

// ---------------------------------------------------------------- kelp

// Vanilla KelpFeature: a kelp column 2..11 tall from the ocean floor, cut short
// below the surface, ending in a kelp tip of age 20..23 (near the cap of 25).
class KelpFeature final : public Feature {
public:
    StateId plant = 0;
    std::array<StateId, 4> tips{}; // ages 20..23
    uint16_t water = 0, kelp = 0, kelpPlant = 0, magma = 0;

    bool place(FeatureContext& ctx) const override {
        WorldGenRegion& level = ctx.level;
        Xoroshiro& r = ctx.random;
        const int x = ctx.origin.x, z = ctx.origin.z;
        BlockPos p(x, level.height(Heightmap::OceanFloor, x, z), z);
        if (!veg::isBlock(level, p, water)) return false;
        int placed = 0;
        const int height = 1 + r.nextInt(10);
        for (int i = 0; i <= height; ++i, p = p.above()) {
            if (veg::isBlock(level, p, water) && veg::isBlock(level, p.above(), water) && canSurvive(level, p)) {
                if (i == height) {
                    level.setBlock(p, tips[size_t(r.nextInt(4))]);
                    ++placed;
                } else {
                    level.setBlock(p, plant);
                }
            } else if (i > 0) {
                // Out of water: the last stem block becomes the tip.
                const BlockPos below = p.below();
                if (canSurvive(level, below) && !veg::isBlock(level, below.below(), kelp)) {
                    level.setBlock(below, tips[size_t(r.nextInt(4))]);
                    ++placed;
                }
                break;
            }
        }
        return placed > 0;
    }

private:
    // Vanilla GrowingPlantBlock.canSurvive for kelp, here rather than in BlockRules
    // because kelp_plant shares seagrass's rule there (which rejects kelp on kelp).
    bool canSurvive(const WorldGenRegion& level, const BlockPos& p) const {
        const BlockPos below = p.below();
        const uint16_t b = level.info(below).block;
        if (b == magma) return false;
        return b == kelp || b == kelpPlant || level.sturdy(below, Direction::Up);
    }
};

std::unique_ptr<Feature> makeKelp(const Json&, FeatureLoader& l) {
    auto f = std::make_unique<KelpFeature>();
    auto plant = l.state(Json("kelp_plant"));
    auto water = veg::requireBlock(l, "water");
    auto kelp = veg::requireBlock(l, "kelp");
    auto kelpPlant = veg::requireBlock(l, "kelp_plant");
    auto magma = veg::requireBlock(l, "magma_block");
    if (!plant || !water || !kelp || !kelpPlant || !magma) return nullptr;
    for (int age = 20; age < 24; ++age) {
        auto tip = l.state(Json(std::format("kelp[age={}]", age)));
        if (!tip) return nullptr;
        f->tips[size_t(age - 20)] = *tip;
    }
    f->plant = *plant;
    f->water = *water;
    f->kelp = *kelp;
    f->kelpPlant = *kelpPlant;
    f->magma = *magma;
    return f;
}

// Waterlogged sea pickles, 1..4.
std::optional<std::array<StateId, 4>> pickleStates(FeatureLoader& l) {
    std::array<StateId, 4> out{};
    for (int n = 1; n <= 4; ++n) {
        auto s = l.state(Json(std::format("sea_pickle[pickles={},waterlogged=true]", n)));
        if (!s) return std::nullopt;
        out[size_t(n - 1)] = *s;
    }
    return out;
}

// ---------------------------------------------------------------- sea_pickle

// Vanilla SeaPickleFeature: `count` tries on the ocean floor within 7 blocks.
class SeaPickleFeature final : public Feature {
public:
    IntProvider count;
    std::array<StateId, 4> pickles{};
    uint16_t water = 0;

    bool place(FeatureContext& ctx) const override {
        WorldGenRegion& level = ctx.level;
        Xoroshiro& r = ctx.random;
        const BlockRules& rules = level.registry().rules();
        int placed = 0;
        const int n = count.sample(r);
        for (int i = 0; i < n; ++i) {
            const int x = ctx.origin.x + veg::triangle(r, 8);
            const int z = ctx.origin.z + veg::triangle(r, 8);
            const BlockPos p(x, level.height(Heightmap::OceanFloor, x, z), z);
            const StateId s = pickles[size_t(r.nextInt(4))];
            if (veg::isBlock(level, p, water) && rules.canSurvive(level, s, p)) {
                level.setBlock(p, s);
                ++placed;
            }
        }
        return placed > 0;
    }
};

std::unique_ptr<Feature> makeSeaPickle(const Json& c, FeatureLoader& l) {
    auto count = IntProvider::parse(c.value("count", Json(1)), l);
    auto pickles = pickleStates(l);
    auto water = veg::requireBlock(l, "water");
    if (!count || !pickles || !water) return nullptr;
    auto f = std::make_unique<SeaPickleFeature>();
    f->count = *count;
    f->pickles = *pickles;
    f->water = *water;
    return f;
}

// ---------------------------------------------------------------- coral

// Vanilla CoralFeature and its three shapes, built from one random coral block
// color. Each coral block placed in open water may get a coral or fan (25%) or
// sea pickles (5%) on top and a wall fan on each open side (20% each).
class CoralFeature final : public Feature {
public:
    enum class Shape : uint8_t { Tree, Claw, Mushroom };
    Shape shape = Shape::Tree;
    std::vector<StateId> blocks, corals;           // #coral_blocks, #corals
    std::vector<std::array<StateId, 4>> wallFans;  // #wall_corals, facing each kHorizontal direction
    std::array<StateId, 4> pickles{};
    TagId coralsTag = 0;
    uint16_t water = 0;

    bool place(FeatureContext& ctx) const override {
        const StateId block = blocks[size_t(ctx.random.nextInt(int(blocks.size())))];
        switch (shape) {
        case Shape::Tree: return placeTree(ctx.level, ctx.random, ctx.origin, block);
        case Shape::Claw: return placeClaw(ctx.level, ctx.random, ctx.origin, block);
        case Shape::Mushroom: return placeMushroom(ctx.level, ctx.random, ctx.origin, block);
        }
        return false;
    }

private:
    // Vanilla CoralFeature.placeCoralBlock: only into water (or a coral) with water above.
    bool placeCoralBlock(WorldGenRegion& level, Xoroshiro& r, const BlockPos& p, StateId block) const {
        const BlockPos above = p.above();
        if (!veg::isBlock(level, p, water) && !level.hasTag(p, coralsTag)) return false;
        if (!veg::isBlock(level, above, water)) return false;
        level.setBlock(p, block);
        if (r.nextFloat() < 0.25f) level.setBlock(above, corals[size_t(r.nextInt(int(corals.size())))]);
        else if (r.nextFloat() < 0.05f) level.setBlock(above, pickles[size_t(r.nextInt(4))]);
        for (size_t d = 0; d < kHorizontal.size(); ++d) {
            if (r.nextFloat() >= 0.2f) continue;
            const BlockPos side = p.offset(kHorizontal[d]);
            if (!veg::isBlock(level, side, water)) continue;
            level.setBlock(side, wallFans[size_t(r.nextInt(int(wallFans.size())))][d]);
        }
        return true;
    }

    // A trunk of 1..3 with 2..4 branches that climb and step outward.
    bool placeTree(WorldGenRegion& level, Xoroshiro& r, const BlockPos& origin, StateId block) const {
        BlockPos p = origin;
        const int trunk = r.nextInt(3) + 1;
        for (int i = 0; i < trunk; ++i, p = p.above())
            if (!placeCoralBlock(level, r, p, block)) return true;
        const BlockPos top = p;
        const int branches = r.nextInt(3) + 2;
        std::array<Direction, 4> dirs = kHorizontal;
        shuffle(dirs, r);
        for (int b = 0; b < branches; ++b) {
            const Direction d = dirs[size_t(b)];
            p = top.offset(d);
            const int length = r.nextInt(5) + 2;
            int run = 0;
            for (int n = 0; n < length && placeCoralBlock(level, r, p, block); ++n) {
                ++run;
                p = p.above();
                if (n == 0 || (run >= 2 && r.nextFloat() < 0.25f)) {
                    p = p.offset(d);
                    run = 0;
                }
            }
        }
        return true;
    }

    // A base block with 2..3 arms reaching out and then forward, like a grasping hand.
    bool placeClaw(WorldGenRegion& level, Xoroshiro& r, const BlockPos& origin, StateId block) const {
        if (!placeCoralBlock(level, r, origin, block)) return false;
        const Direction forward = kHorizontal[size_t(r.nextInt(4))];
        const int arms = r.nextInt(2) + 2;
        std::array<Direction, 3> dirs = {forward, rotateYClockwise(forward), counterClockwise(forward)};
        shuffle(dirs, r);
        for (int a = 0; a < arms; ++a) {
            const Direction d = dirs[size_t(a)];
            const int reach = r.nextInt(2) + 1;
            BlockPos p = origin.offset(d);
            Direction step;
            int length;
            if (d == forward) {
                step = forward;
                length = r.nextInt(3) + 2;
            } else {
                p = p.above();
                step = r.nextInt(2) == 0 ? d : Direction::Up;
                length = r.nextInt(3) + 3;
            }
            for (int i = 0; i < reach && placeCoralBlock(level, r, p, block); ++i) p = p.offset(step);
            p = p.offset(opposite(step)).above();
            for (int i = 0; i < length; ++i) {
                p = p.offset(forward);
                if (!placeCoralBlock(level, r, p, block)) break;
                if (r.nextFloat() < 0.25f) p = p.above();
            }
        }
        return true;
    }

    // The faces of a 4..6 block box (no edges), sunk 1..3 into the floor, 10% holes.
    bool placeMushroom(WorldGenRegion& level, Xoroshiro& r, const BlockPos& origin, StateId block) const {
        const int sy = r.nextInt(3) + 3, sx = r.nextInt(3) + 3, sz = r.nextInt(3) + 3;
        const int sink = r.nextInt(3) + 1;
        for (int x = 0; x <= sx; ++x)
            for (int y = 0; y <= sy; ++y)
                for (int z = 0; z <= sz; ++z) {
                    const bool ex = x == 0 || x == sx, ey = y == 0 || y == sy, ez = z == 0 || z == sz;
                    if ((ex && ey) || (ez && ey) || (ex && ez) || !(ex || ey || ez)) continue;
                    if (r.nextFloat() < 0.1f) continue;
                    placeCoralBlock(level, r, origin.offset(x, y - sink, z), block);
                }
        return true;
    }
};

std::unique_ptr<Feature> makeCoral(FeatureLoader& l, CoralFeature::Shape shape) {
    auto blocksTag = l.tag(Json("coral_blocks"));
    auto coralsTag = l.tag(Json("corals"));
    auto wallTag = l.tag(Json("wall_corals"));
    auto pickles = pickleStates(l);
    auto water = veg::requireBlock(l, "water");
    if (!blocksTag || !coralsTag || !wallTag || !pickles || !water) return nullptr;
    const BlockRegistry& reg = l.registry;
    auto f = std::make_unique<CoralFeature>();
    f->shape = shape;
    for (uint16_t b : reg.tags().blocks(*blocksTag)) f->blocks.push_back(reg.block(b).defaultState);
    for (uint16_t b : reg.tags().blocks(*coralsTag)) f->corals.push_back(reg.block(b).defaultState);
    for (uint16_t b : reg.tags().blocks(*wallTag)) {
        std::array<StateId, 4> facing{};
        for (size_t d = 0; d < kHorizontal.size(); ++d)
            facing[d] = withProp(reg, reg.block(b).defaultState, "facing", directionName(kHorizontal[d]));
        f->wallFans.push_back(facing);
    }
    if (f->blocks.empty() || f->corals.empty() || f->wallFans.empty()) {
        l.error("coral needs non-empty #coral_blocks, #corals and #wall_corals");
        return nullptr;
    }
    f->pickles = *pickles;
    f->coralsTag = *coralsTag;
    f->water = *water;
    return f;
}

} // namespace

void registerAquaticFeatures(FeatureSet& set) {
    set.registerFeature("seagrass", makeSeagrass);
    set.registerFeature("kelp", makeKelp);
    set.registerFeature("sea_pickle", makeSeaPickle);
    set.registerFeature("coral_tree",
                        [](const Json&, FeatureLoader& l) { return makeCoral(l, CoralFeature::Shape::Tree); });
    set.registerFeature("coral_claw",
                        [](const Json&, FeatureLoader& l) { return makeCoral(l, CoralFeature::Shape::Claw); });
    set.registerFeature("coral_mushroom",
                        [](const Json&, FeatureLoader& l) { return makeCoral(l, CoralFeature::Shape::Mushroom); });
}

} // namespace mc
