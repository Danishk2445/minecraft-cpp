// BasicFeatures.cpp -- vanilla ore, disk, simple_block, random_patch (flower),
// random_selector, simple_random_selector and block_column features.
#include <cmath>

#include "world/block/BlockRules.h"
#include "world/gen/feature/Feature.h"
#include "world/gen/feature/FeatureUtil.h"
#include "world/gen/feature/Providers.h"

namespace mc {
namespace {

constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------- ore

class OreFeature final : public Feature {
public:
    struct Target {
        RuleTest test;
        StateId state;
    };
    std::vector<Target> targets;
    int size = 1;
    float discardOnAir = 0.0f;

    bool place(FeatureContext& ctx) const override {
        Xoroshiro& r = ctx.random;
        const BlockPos o = ctx.origin;
        const float angle = r.nextFloat() * float(kPi);
        const float spread = float(size) / 8.0f;
        const int radius = int(std::ceil((float(size) / 16.0f * 2.0f + 1.0f) / 2.0f));
        const double x0 = o.x + std::sin(angle) * spread, x1 = o.x - std::sin(angle) * spread;
        const double z0 = o.z + std::cos(angle) * spread, z1 = o.z - std::cos(angle) * spread;
        const double y0 = o.y + r.nextInt(3) - 2, y1 = o.y + r.nextInt(3) - 2;
        const int minX = o.x - int(std::ceil(spread)) - radius, minY = o.y - 2 - radius;
        const int minZ = o.z - int(std::ceil(spread)) - radius;
        const int width = 2 * (int(std::ceil(spread)) + radius), height = 2 * (2 + radius);
        // Only where the blob reaches below the ocean floor somewhere (vanilla).
        for (int x = minX; x <= minX + width; ++x)
            for (int z = minZ; z <= minZ + width; ++z)
                if (minY <= ctx.level.height(Heightmap::OceanFloor, x, z))
                    return doPlace(ctx, x0, x1, z0, z1, y0, y1, minX, minY, minZ, width, height);
        return false;
    }

private:
    bool doPlace(FeatureContext& ctx, double minX, double maxX, double minZ, double maxZ, double minY, double maxY, int x,
                 int y, int z, int width, int height) const {
        Xoroshiro& r = ctx.random;
        const BlockRegistry& reg = ctx.level.registry();
        std::vector<bool> done(size_t((width + 1) * (height + 1) * (width + 1)), false);
        std::vector<double> ds(size_t(size) * 4);
        for (int k = 0; k < size; ++k) {
            const float f = float(k) / float(size);
            const double h = r.nextDouble() * size / 16.0;
            ds[size_t(k) * 4 + 0] = minX + f * (maxX - minX);
            ds[size_t(k) * 4 + 1] = minY + f * (maxY - minY);
            ds[size_t(k) * 4 + 2] = minZ + f * (maxZ - minZ);
            ds[size_t(k) * 4 + 3] = ((std::sin(float(kPi) * f) + 1.0f) * h + 1.0) / 2.0;
        }
        // Drop spheres swallowed by a bigger neighbor.
        for (int k = 0; k < size - 1; ++k) {
            if (ds[size_t(k) * 4 + 3] <= 0.0) continue;
            for (int m = k + 1; m < size; ++m) {
                if (ds[size_t(m) * 4 + 3] <= 0.0) continue;
                const double dx = ds[size_t(k) * 4] - ds[size_t(m) * 4], dy = ds[size_t(k) * 4 + 1] - ds[size_t(m) * 4 + 1];
                const double dz = ds[size_t(k) * 4 + 2] - ds[size_t(m) * 4 + 2];
                const double dr = ds[size_t(k) * 4 + 3] - ds[size_t(m) * 4 + 3];
                if (dr * dr > dx * dx + dy * dy + dz * dz) {
                    if (dr > 0.0) ds[size_t(m) * 4 + 3] = -1.0;
                    else ds[size_t(k) * 4 + 3] = -1.0;
                }
            }
        }
        int placed = 0;
        for (int k = 0; k < size; ++k) {
            const double rad = ds[size_t(k) * 4 + 3];
            if (rad < 0.0) continue;
            const double cx = ds[size_t(k) * 4], cy = ds[size_t(k) * 4 + 1], cz = ds[size_t(k) * 4 + 2];
            const int bx0 = std::max(int(std::floor(cx - rad)), x), by0 = std::max(int(std::floor(cy - rad)), y);
            const int bz0 = std::max(int(std::floor(cz - rad)), z);
            const int bx1 = std::max(int(std::floor(cx + rad)), bx0), by1 = std::max(int(std::floor(cy + rad)), by0);
            const int bz1 = std::max(int(std::floor(cz + rad)), bz0);
            for (int bx = bx0; bx <= bx1; ++bx) {
                const double u = (bx + 0.5 - cx) / rad;
                if (u * u >= 1.0) continue;
                for (int by = by0; by <= by1; ++by) {
                    const double v = (by + 0.5 - cy) / rad;
                    if (u * u + v * v >= 1.0) continue;
                    for (int bz = bz0; bz <= bz1; ++bz) {
                        const double w = (bz + 0.5 - cz) / rad;
                        if (u * u + v * v + w * w >= 1.0 || !WorldGenRegion::inBuildHeight(by)) continue;
                        const size_t bit = size_t(bx - x + (by - y) * width + (bz - z) * width * height);
                        if (bit >= done.size() || done[bit]) continue;
                        done[bit] = true;
                        const BlockPos p(bx, by, bz);
                        if (!ctx.level.canWrite(p)) continue;
                        const StateId cur = ctx.level.getBlock(p);
                        for (const Target& t : targets) {
                            if (!t.test.test(reg, cur)) continue;
                            if (!skipAirCheck(r) && adjacentToAir(ctx.level, p)) continue;
                            ctx.level.setBlock(p, t.state);
                            ++placed;
                            break;
                        }
                    }
                }
            }
        }
        return placed > 0;
    }

    bool skipAirCheck(Xoroshiro& r) const {
        if (discardOnAir <= 0.0f) return true;
        if (discardOnAir >= 1.0f) return false;
        return r.nextFloat() >= discardOnAir;
    }
    static bool adjacentToAir(const WorldGenRegion& level, const BlockPos& p) {
        for (Direction d : kDirections)
            if (level.isAir(p.offset(d))) return true;
        return false;
    }
};

std::unique_ptr<Feature> makeOre(const Json& c, FeatureLoader& l) {
    auto f = std::make_unique<OreFeature>();
    for (const Json& t : c.value("targets", Json::array())) {
        auto test = RuleTest::parse(t.value("target", Json()), l);
        auto state = l.state(t.value("state", Json()));
        if (!test || !state) return nullptr;
        f->targets.push_back({*test, *state});
    }
    f->size = l.getInt(c, "size", 1);
    f->discardOnAir = l.getFloat(c, "discard_chance_on_air_exposure", 0.0f);
    if (f->targets.empty() || f->size <= 0) return nullptr;
    return f;
}

// ---------------------------------------------------------------- disk

class DiskFeature final : public Feature {
public:
    struct Rule {
        BlockPredicate when;
        BlockStateProvider then;
    };
    BlockStateProvider fallback = BlockStateProvider::simple(0);
    std::vector<Rule> rules;
    BlockPredicate target;
    IntProvider radius;
    int halfHeight = 1;

    bool place(FeatureContext& ctx) const override {
        const BlockPos o = ctx.origin;
        const int top = o.y + halfHeight, bottom = o.y - halfHeight - 1;
        const int rad = radius.sample(ctx.random);
        bool any = false;
        for (int dx = -rad; dx <= rad; ++dx)
            for (int dz = -rad; dz <= rad; ++dz) {
                if (dx * dx + dz * dz > rad * rad) continue;
                for (int y = top; y > bottom; --y) {
                    const BlockPos p(o.x + dx, y, o.z + dz);
                    if (!target.test(ctx.level, p)) continue;
                    ctx.level.setBlock(p, stateAt(ctx, p));
                    any = true;
                }
            }
        return any;
    }

private:
    StateId stateAt(FeatureContext& ctx, const BlockPos& p) const {
        for (const Rule& r : rules)
            if (r.when.test(ctx.level, p)) return r.then.get(ctx.random, p);
        return fallback.get(ctx.random, p);
    }
};

std::unique_ptr<Feature> makeDisk(const Json& c, FeatureLoader& l) {
    auto f = std::make_unique<DiskFeature>();
    const Json sp = c.value("state_provider", Json());
    if (sp.is_object() && sp.contains("fallback")) {
        auto fb = BlockStateProvider::parse(sp["fallback"], l);
        if (!fb) return nullptr;
        f->fallback = *fb;
        for (const Json& rule : sp.value("rules", Json::array())) {
            auto when = BlockPredicate::parse(rule.value("if_true", Json()), l);
            auto then = BlockStateProvider::parse(rule.value("then", Json()), l);
            if (!when || !then) return nullptr;
            f->rules.push_back({std::move(*when), *then});
        }
    } else {
        auto p = BlockStateProvider::parse(sp, l);
        if (!p) return nullptr;
        f->fallback = *p;
    }
    auto target = BlockPredicate::parse(c.value("target", Json()), l);
    auto radius = IntProvider::parse(c.value("radius", Json(2)), l);
    if (!target || !radius) return nullptr;
    f->target = std::move(*target);
    f->radius = *radius;
    f->halfHeight = l.getInt(c, "half_height", 1);
    return f;
}

// ---------------------------------------------------------------- simple_block

class SimpleBlockFeature final : public Feature {
public:
    BlockStateProvider toPlace = BlockStateProvider::simple(0);

    bool place(FeatureContext& ctx) const override {
        const StateId s = toPlace.get(ctx.random, ctx.origin);
        if (!ctx.level.registry().rules().canSurvive(ctx.level, s, ctx.origin)) return false;
        return placeBlock(ctx.level, s, ctx.origin);
    }
};

std::unique_ptr<Feature> makeSimpleBlock(const Json& c, FeatureLoader& l) {
    auto p = BlockStateProvider::parse(c.value("to_place", Json()), l);
    if (!p) return nullptr;
    auto f = std::make_unique<SimpleBlockFeature>();
    f->toPlace = *p;
    return f;
}

// ---------------------------------------------------------------- random_patch / flower

class RandomPatchFeature final : public Feature {
public:
    int tries = 128, xzSpread = 7, ySpread = 3;
    const PlacedFeature* feature = nullptr;

    bool place(FeatureContext& ctx) const override {
        Xoroshiro& r = ctx.random;
        const int j = xzSpread + 1, k = ySpread + 1;
        int placed = 0;
        PlacementContext nested{ctx.level};
        for (int i = 0; i < tries; ++i) {
            // nextInt(n) - nextInt(n), left operand first as in Java (C++ leaves it unspecified).
            auto spread = [&r](int n) {
                const int a = r.nextInt(n);
                return a - r.nextInt(n);
            };
            const int dx = spread(j);
            const int dy = spread(k);
            const int dz = spread(j);
            if (feature->place(nested, r, ctx.origin.offset(dx, dy, dz))) ++placed;
        }
        return placed > 0;
    }
};

std::unique_ptr<Feature> makeRandomPatch(const Json& c, FeatureLoader& l) {
    auto f = std::make_unique<RandomPatchFeature>();
    f->tries = l.getInt(c, "tries", 128);
    f->xzSpread = l.getInt(c, "xz_spread", 7);
    f->ySpread = l.getInt(c, "y_spread", 3);
    f->feature = l.placed(c.value("feature", Json()));
    if (!f->feature) return nullptr;
    return f;
}

// ---------------------------------------------------------------- selectors

class RandomSelectorFeature final : public Feature {
public:
    std::vector<std::pair<const PlacedFeature*, float>> features;
    const PlacedFeature* fallback = nullptr;

    bool place(FeatureContext& ctx) const override {
        PlacementContext nested{ctx.level};
        for (const auto& [f, chance] : features)
            if (ctx.random.nextFloat() < chance) return f->place(nested, ctx.random, ctx.origin);
        return fallback->place(nested, ctx.random, ctx.origin);
    }
};

std::unique_ptr<Feature> makeRandomSelector(const Json& c, FeatureLoader& l) {
    auto f = std::make_unique<RandomSelectorFeature>();
    for (const Json& e : c.value("features", Json::array())) {
        const PlacedFeature* p = l.placed(e.value("feature", Json()));
        if (!p) return nullptr;
        f->features.emplace_back(p, l.getFloat(e, "chance", 0.0f));
    }
    f->fallback = l.placed(c.value("default", Json()));
    if (!f->fallback) return nullptr;
    return f;
}

class SimpleRandomSelectorFeature final : public Feature {
public:
    std::vector<const PlacedFeature*> features;

    bool place(FeatureContext& ctx) const override {
        PlacementContext nested{ctx.level};
        const PlacedFeature* f = features[size_t(ctx.random.nextInt(int(features.size())))];
        return f->place(nested, ctx.random, ctx.origin);
    }
};

std::unique_ptr<Feature> makeSimpleRandomSelector(const Json& c, FeatureLoader& l) {
    auto f = std::make_unique<SimpleRandomSelectorFeature>();
    for (const Json& e : c.value("features", Json::array())) {
        const PlacedFeature* p = l.placed(e);
        if (!p) return nullptr;
        f->features.push_back(p);
    }
    if (f->features.empty()) return nullptr;
    return f;
}

// ---------------------------------------------------------------- block_column

class BlockColumnFeature final : public Feature {
public:
    struct Layer {
        IntProvider height;
        BlockStateProvider provider;
    };
    std::vector<Layer> layers;
    Direction direction = Direction::Up;
    BlockPredicate allowed;
    bool prioritizeTip = false;

    bool place(FeatureContext& ctx) const override {
        const size_t n = layers.size();
        std::vector<int> heights(n);
        int total = 0;
        for (size_t k = 0; k < n; ++k) total += heights[k] = layers[k].height.sample(ctx.random);
        if (total == 0) return false;
        // The column needs free space one step ahead of each block it places.
        BlockPos ahead = ctx.origin.offset(direction);
        for (int i = 0; i < total; ++i) {
            if (!allowed.test(ctx.level, ahead)) {
                truncate(heights, total - i);
                break;
            }
            ahead = ahead.offset(direction);
        }
        BlockPos p = ctx.origin;
        for (size_t k = 0; k < n; ++k)
            for (int i = 0; i < heights[k]; ++i) {
                ctx.level.setBlock(p, layers[k].provider.get(ctx.random, p));
                p = p.offset(direction);
            }
        return true;
    }

private:
    // Removes `excess` blocks from the tip end or the base end of the layer list.
    void truncate(std::vector<int>& heights, int excess) const {
        const int n = int(heights.size());
        const int step = prioritizeTip ? 1 : -1;
        for (int m = prioritizeTip ? 0 : n - 1; m != (prioritizeTip ? n : -1) && excess > 0; m += step) {
            const int take = std::min(heights[size_t(m)], excess);
            excess -= take;
            heights[size_t(m)] -= take;
        }
    }
};

std::unique_ptr<Feature> makeBlockColumn(const Json& c, FeatureLoader& l) {
    auto f = std::make_unique<BlockColumnFeature>();
    for (const Json& layer : c.value("layers", Json::array())) {
        auto h = IntProvider::parse(layer.value("height", Json(1)), l);
        auto p = BlockStateProvider::parse(layer.value("provider", Json()), l);
        if (!h || !p) return nullptr;
        f->layers.push_back({*h, *p});
    }
    const std::string dir = jsonGet<std::string>(c, "direction", "up");
    f->direction = dir == "down" ? Direction::Down : Direction::Up;
    auto allowed = BlockPredicate::parse(c.value("allowed_placement", Json{{"type", "true"}}), l);
    if (!allowed || f->layers.empty()) return nullptr;
    f->allowed = std::move(*allowed);
    f->prioritizeTip = l.getBool(c, "prioritize_tip", false);
    return f;
}

} // namespace

void registerBasicFeatures(FeatureSet& set) {
    set.registerFeature("ore", makeOre);
    set.registerFeature("disk", makeDisk);
    set.registerFeature("simple_block", makeSimpleBlock);
    set.registerFeature("random_patch", makeRandomPatch);
    set.registerFeature("flower", makeRandomPatch);
    set.registerFeature("no_bonemeal_flower", makeRandomPatch);
    set.registerFeature("random_selector", makeRandomSelector);
    set.registerFeature("simple_random_selector", makeSimpleRandomSelector);
    set.registerFeature("block_column", makeBlockColumn);
}

} // namespace mc
