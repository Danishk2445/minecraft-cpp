// Placement.cpp -- vanilla placement modifiers.
#include <cmath>

#include "world/gen/feature/BiomeDecorator.h"
#include "world/gen/feature/Feature.h"
#include "world/gen/feature/Providers.h"

namespace mc {
namespace {

std::optional<Heightmap> parseHeightmap(const Json& j, FeatureLoader& l) {
    std::string s = j.is_string() ? j.get<std::string>() : "";
    for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    if (s == "WORLD_SURFACE_WG" || s == "WORLD_SURFACE") return Heightmap::WorldSurface;
    if (s == "OCEAN_FLOOR_WG" || s == "OCEAN_FLOOR") return Heightmap::OceanFloor;
    if (s == "MOTION_BLOCKING") return Heightmap::MotionBlocking;
    if (s == "MOTION_BLOCKING_NO_LEAVES") return Heightmap::MotionBlockingNoLeaves;
    l.error("unknown heightmap " + (j.is_string() ? j.get<std::string>() : j.dump()));
    return std::nullopt;
}

// count: repeat the position N times.
class CountModifier final : public PlacementModifier {
public:
    explicit CountModifier(IntProvider c) : count_(c) {}
    void apply(PlacementContext&, Xoroshiro& r, const BlockPos& pos, std::vector<BlockPos>& out) const override {
        const int n = count_.sample(r);
        for (int i = 0; i < n; ++i) out.push_back(pos);
    }

private:
    IntProvider count_;
};

class InSquare final : public PlacementModifier {
public:
    void apply(PlacementContext&, Xoroshiro& r, const BlockPos& pos, std::vector<BlockPos>& out) const override {
        const int x = r.nextInt(16) + pos.x;
        const int z = r.nextInt(16) + pos.z;
        out.push_back({x, pos.y, z});
    }
};

class HeightRange final : public PlacementModifier {
public:
    explicit HeightRange(HeightProvider h) : height_(h) {}
    void apply(PlacementContext&, Xoroshiro& r, const BlockPos& pos, std::vector<BlockPos>& out) const override {
        out.push_back({pos.x, height_.sample(r), pos.z});
    }

private:
    HeightProvider height_;
};

class HeightmapModifier final : public PlacementModifier {
public:
    explicit HeightmapModifier(Heightmap t) : type_(t) {}
    void apply(PlacementContext& ctx, Xoroshiro&, const BlockPos& pos, std::vector<BlockPos>& out) const override {
        const int y = ctx.level.height(type_, pos.x, pos.z);
        if (y > kWorldMinY) out.push_back({pos.x, y, pos.z});
    }

private:
    Heightmap type_;
};

class RarityFilter final : public PlacementModifier {
public:
    explicit RarityFilter(int chance) : chance_(chance) {}
    void apply(PlacementContext&, Xoroshiro& r, const BlockPos& pos, std::vector<BlockPos>& out) const override {
        if (r.nextFloat() < 1.0f / float(chance_)) out.push_back(pos);
    }

private:
    int chance_;
};

class SurfaceWaterDepthFilter final : public PlacementModifier {
public:
    explicit SurfaceWaterDepthFilter(int depth) : depth_(depth) {}
    void apply(PlacementContext& ctx, Xoroshiro&, const BlockPos& pos, std::vector<BlockPos>& out) const override {
        const int floor = ctx.level.height(Heightmap::OceanFloor, pos.x, pos.z);
        const int surface = ctx.level.height(Heightmap::WorldSurface, pos.x, pos.z);
        if (surface - floor <= depth_) out.push_back(pos);
    }

private:
    int depth_;
};

class SurfaceRelativeThreshold final : public PlacementModifier {
public:
    SurfaceRelativeThreshold(Heightmap t, int lo, int hi) : type_(t), lo_(lo), hi_(hi) {}
    void apply(PlacementContext& ctx, Xoroshiro&, const BlockPos& pos, std::vector<BlockPos>& out) const override {
        const long h = ctx.level.height(type_, pos.x, pos.z);
        if (h + lo_ <= pos.y && pos.y <= h + hi_) out.push_back(pos);
    }

private:
    Heightmap type_;
    long lo_, hi_;
};

class BiomeFilter final : public PlacementModifier {
public:
    void apply(PlacementContext& ctx, Xoroshiro&, const BlockPos& pos, std::vector<BlockPos>& out) const override {
        if (!ctx.decorator || ctx.decorator->biomeHas(ctx.level.biome(pos), ctx.step, ctx.feature)) out.push_back(pos);
    }
};

class PredicateFilter final : public PlacementModifier {
public:
    explicit PredicateFilter(BlockPredicate p) : predicate_(std::move(p)) {}
    void apply(PlacementContext& ctx, Xoroshiro&, const BlockPos& pos, std::vector<BlockPos>& out) const override {
        if (predicate_.test(ctx.level, pos)) out.push_back(pos);
    }

private:
    BlockPredicate predicate_;
};

class EnvironmentScan final : public PlacementModifier {
public:
    EnvironmentScan(Direction d, BlockPredicate target, BlockPredicate allowed, int steps)
        : dir_(d), target_(std::move(target)), allowed_(std::move(allowed)), steps_(steps) {}
    void apply(PlacementContext& ctx, Xoroshiro&, const BlockPos& pos, std::vector<BlockPos>& out) const override {
        BlockPos p = pos;
        if (!allowed_.test(ctx.level, p)) return;
        for (int i = 0; i < steps_; ++i) {
            if (target_.test(ctx.level, p)) {
                out.push_back(p);
                return;
            }
            p = p.offset(dir_);
            if (!WorldGenRegion::inBuildHeight(p.y)) return;
            if (!allowed_.test(ctx.level, p)) break;
        }
        if (target_.test(ctx.level, p)) out.push_back(p);
    }

private:
    Direction dir_;
    BlockPredicate target_, allowed_;
    int steps_;
};

class RandomOffset final : public PlacementModifier {
public:
    RandomOffset(IntProvider xz, IntProvider y) : xz_(xz), y_(y) {}
    void apply(PlacementContext&, Xoroshiro& r, const BlockPos& pos, std::vector<BlockPos>& out) const override {
        const int x = pos.x + xz_.sample(r);
        const int y = pos.y + y_.sample(r);
        const int z = pos.z + xz_.sample(r);
        out.push_back({x, y, z});
    }

private:
    IntProvider xz_, y_;
};

class NoiseThresholdCount final : public PlacementModifier {
public:
    NoiseThresholdCount(double level, int below, int above) : level_(level), below_(below), above_(above) {}
    void apply(PlacementContext&, Xoroshiro&, const BlockPos& pos, std::vector<BlockPos>& out) const override {
        const double d = biomeInfoNoise(pos.x / 200.0, pos.z / 200.0);
        const int n = d < level_ ? below_ : above_;
        for (int i = 0; i < n; ++i) out.push_back(pos);
    }

private:
    double level_;
    int below_, above_;
};

class NoiseBasedCount final : public PlacementModifier {
public:
    NoiseBasedCount(int ratio, double factor, double offset) : ratio_(ratio), factor_(factor), offset_(offset) {}
    void apply(PlacementContext&, Xoroshiro&, const BlockPos& pos, std::vector<BlockPos>& out) const override {
        const double d = biomeInfoNoise(pos.x / factor_, pos.z / factor_);
        const int n = int(std::ceil((d + offset_) * ratio_));
        for (int i = 0; i < n; ++i) out.push_back(pos);
    }

private:
    int ratio_;
    double factor_, offset_;
};

std::optional<Direction> directionNamed(std::string_view s) {
    static constexpr std::pair<std::string_view, Direction> names[] = {
        {"down", Direction::Down}, {"up", Direction::Up},     {"north", Direction::North},
        {"south", Direction::South}, {"west", Direction::West}, {"east", Direction::East}};
    for (const auto& [n, d] : names)
        if (n == s) return d;
    return std::nullopt;
}

} // namespace

void registerPlacementModifiers(FeatureSet& set) {
    set.registerModifier("count", [](const Json& j, FeatureLoader& l) -> std::unique_ptr<PlacementModifier> {
        auto c = IntProvider::parse(j.value("count", Json(1)), l);
        if (!c) return nullptr;
        return std::make_unique<CountModifier>(*c);
    });
    set.registerModifier("in_square", [](const Json&, FeatureLoader&) -> std::unique_ptr<PlacementModifier> {
        return std::make_unique<InSquare>();
    });
    set.registerModifier("height_range", [](const Json& j, FeatureLoader& l) -> std::unique_ptr<PlacementModifier> {
        auto h = HeightProvider::parse(j.value("height", Json()), l);
        if (!h) return nullptr;
        return std::make_unique<HeightRange>(*h);
    });
    set.registerModifier("heightmap", [](const Json& j, FeatureLoader& l) -> std::unique_ptr<PlacementModifier> {
        auto t = parseHeightmap(j.value("heightmap", Json()), l);
        if (!t) return nullptr;
        return std::make_unique<HeightmapModifier>(*t);
    });
    set.registerModifier("rarity_filter", [](const Json& j, FeatureLoader& l) -> std::unique_ptr<PlacementModifier> {
        const int chance = l.getInt(j, "chance", 1);
        if (chance <= 0) return nullptr;
        return std::make_unique<RarityFilter>(chance);
    });
    set.registerModifier("surface_water_depth_filter", [](const Json& j, FeatureLoader& l) -> std::unique_ptr<PlacementModifier> {
        return std::make_unique<SurfaceWaterDepthFilter>(l.getInt(j, "max_water_depth", 0));
    });
    set.registerModifier("surface_relative_threshold_filter",
                         [](const Json& j, FeatureLoader& l) -> std::unique_ptr<PlacementModifier> {
                             auto t = parseHeightmap(j.value("heightmap", Json()), l);
                             if (!t) return nullptr;
                             return std::make_unique<SurfaceRelativeThreshold>(*t, l.getInt(j, "min_inclusive", INT32_MIN / 2),
                                                                               l.getInt(j, "max_inclusive", INT32_MAX / 2));
                         });
    set.registerModifier("biome", [](const Json&, FeatureLoader&) -> std::unique_ptr<PlacementModifier> {
        return std::make_unique<BiomeFilter>();
    });
    set.registerModifier("block_predicate_filter", [](const Json& j, FeatureLoader& l) -> std::unique_ptr<PlacementModifier> {
        auto p = BlockPredicate::parse(j.value("predicate", Json()), l);
        if (!p) return nullptr;
        return std::make_unique<PredicateFilter>(std::move(*p));
    });
    set.registerModifier("environment_scan", [](const Json& j, FeatureLoader& l) -> std::unique_ptr<PlacementModifier> {
        auto d = directionNamed(jsonGet<std::string>(j, "direction_of_search", "down"));
        auto target = BlockPredicate::parse(j.value("target_condition", Json()), l);
        std::optional<BlockPredicate> allowed = BlockPredicate::alwaysTrue();
        if (j.contains("allowed_search_condition")) allowed = BlockPredicate::parse(j["allowed_search_condition"], l);
        if (!d || !target || !allowed) return nullptr;
        return std::make_unique<EnvironmentScan>(*d, std::move(*target), std::move(*allowed), l.getInt(j, "max_steps", 1));
    });
    set.registerModifier("random_offset", [](const Json& j, FeatureLoader& l) -> std::unique_ptr<PlacementModifier> {
        auto xz = IntProvider::parse(j.value("xz_spread", Json(0)), l);
        auto y = IntProvider::parse(j.value("y_spread", Json(0)), l);
        if (!xz || !y) return nullptr;
        return std::make_unique<RandomOffset>(*xz, *y);
    });
    set.registerModifier("noise_threshold_count", [](const Json& j, FeatureLoader& l) -> std::unique_ptr<PlacementModifier> {
        return std::make_unique<NoiseThresholdCount>(l.getFloat(j, "noise_level", 0.0f), l.getInt(j, "below_noise", 0),
                                                     l.getInt(j, "above_noise", 0));
    });
    set.registerModifier("noise_based_count", [](const Json& j, FeatureLoader& l) -> std::unique_ptr<PlacementModifier> {
        return std::make_unique<NoiseBasedCount>(l.getInt(j, "noise_to_count_ratio", 1), l.getFloat(j, "noise_factor", 1.0f),
                                                 l.getFloat(j, "noise_offset", 0.0f));
    });
}

} // namespace mc
