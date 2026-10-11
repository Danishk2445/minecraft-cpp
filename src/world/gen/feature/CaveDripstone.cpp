// CaveDripstone.cpp -- vanilla dripstone features: dripstone_cluster,
// large_dripstone and pointed_dripstone, on vanilla's DripstoneUtils.
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>

#include "world/gen/feature/CaveCommon.h"
#include "world/gen/feature/FeatureUtil.h"
#include "world/gen/feature/Providers.h"

namespace mc {
namespace {

constexpr float kPiF = 3.14159265358979323846f;

// Vanilla Mth.clampedMap.
double clampedMap(double v, double fromLo, double fromHi, double toLo, double toHi) {
    const double t = (v - fromLo) / (fromHi - fromLo);
    return t < 0.0 ? toLo : t > 1.0 ? toHi : toLo + t * (toHi - toLo);
}

// Vanilla DripstoneUtils.getDripstoneHeight: the profile of a large dripstone,
// height at `radius` from its axis (capped: zero bluntness gives an infinite axis).
double dripstoneHeight(double radius, double maxRadius, double scale, double bluntness) {
    if (radius < bluntness) radius = bluntness;
    const double r = radius / maxRadius * 0.384;
    const double h = scale * (0.75 * std::pow(r, 4.0 / 3.0) - std::pow(r, 2.0 / 3.0) - std::log(r) / 3.0);
    return std::min(std::max(h, 0.0) / 0.384 * maxRadius, double(kWorldHeight));
}

enum Thickness : uint8_t { kTipMerge, kTip, kFrustum, kMiddle, kBase };

// Vanilla DripstoneUtils: the block tests and pointed dripstone columns the three
// features share.
struct Dripstone {
    const BlockRegistry* reg;
    uint16_t water, lava, dripstoneBlock, pointedBlock;
    StateId block;
    TagId replaceable, baseStone;
    std::array<StateId, 2 * 5 * 2> pointed{}; // [up][thickness][waterlogged]

    explicit Dripstone(const BlockRegistry& r) : reg(&r) {
        const FeatureTags& tags = featureTags(r);
        auto index = [&](const char* id) -> uint16_t {
            const Block* b = r.find(id);
            return b ? b->index : uint16_t(0xFFFF);
        };
        water = index("water");
        lava = index("lava");
        dripstoneBlock = index("dripstone_block");
        pointedBlock = index("pointed_dripstone");
        block = r.defaultState("dripstone_block");
        replaceable = tags.dripstoneReplaceable;
        baseStone = tags.baseStone;
        static constexpr std::string_view thickness[] = {"tip_merge", "tip", "frustum", "middle", "base"};
        const StateId def = r.defaultState("pointed_dripstone");
        for (int up = 0; up < 2; ++up)
            for (int t = 0; t < 5; ++t)
                for (int w = 0; w < 2; ++w) {
                    StateId s = withProp(r, def, "vertical_direction", std::string_view(up ? "up" : "down"));
                    s = withProp(r, s, "thickness", thickness[t]);
                    pointed[size_t((up * 5 + t) * 2 + w)] = withProp(r, s, "waterlogged", w == 1);
                }
    }

    bool emptyOrWater(StateId s) const {
        const StateInfo& i = reg->info(s);
        return (i.flags & kAir) || i.block == water;
    }
    bool emptyOrWaterOrLava(StateId s) const { return emptyOrWater(s) || reg->info(s).block == lava; }
    bool base(StateId s) const { return reg->info(s).block == dripstoneBlock || reg->hasTag(s, replaceable); }
    bool baseOrLava(StateId s) const { return base(s) || reg->info(s).block == lava; }

    // placeDripstoneBlockIfPossible
    bool placeBlock(WorldGenRegion& level, const BlockPos& p) const {
        if (!level.hasTag(p, replaceable)) return false;
        level.setBlock(p, block);
        return true;
    }

    // growPointedDripstone: base, middles, frustum and tip from `p` toward `dir`,
    // if the block behind `p` can hold it.
    void grow(WorldGenRegion& level, BlockPos p, Direction dir, int height, bool merge) const {
        if (!base(level.getBlock(p.offset(opposite(dir))))) return;
        const int up = dir == Direction::Up ? 1 : 0;
        auto put = [&](Thickness t) {
            const int wet = level.fluid(p).kind == FluidKind::Water ? 1 : 0;
            level.setBlock(p, pointed[size_t((up * 5 + t) * 2 + wet)]);
            p = p.offset(dir);
        };
        if (height >= 3) {
            put(kBase);
            for (int i = 0; i < height - 3; ++i) put(kMiddle);
        }
        if (height >= 2) put(kFrustum);
        if (height >= 1) put(merge ? kTipMerge : kTip);
    }

    // isCircleMostlyEmbeddedInStone
    bool circleEmbedded(const WorldGenRegion& level, const BlockPos& c, int radius) const {
        if (emptyOrWaterOrLava(level.getBlock(c))) return false;
        const float step = 6.0f / float(radius);
        for (float a = 0.0f; a < kPiF * 2.0f; a += step) {
            const int dx = int(std::cos(a) * float(radius)), dz = int(std::sin(a) * float(radius));
            if (emptyOrWaterOrLava(level.getBlock(c.offset(dx, 0, dz)))) return false;
        }
        return true;
    }
};

// ---------------------------------------------------------------- dripstone_cluster

// Vanilla DripstoneClusterFeature: over an area of floor-to-ceiling columns, a
// layer of dripstone blocks with stalactites and stalagmites that lengthen toward
// the middle and sometimes meet, and now and then a one-block pool.
class DripstoneClusterFeature final : public Feature {
public:
    explicit DripstoneClusterFeature(const BlockRegistry& r) : ds(r), water(r.defaultState("water")) {}

    Dripstone ds;
    StateId water;
    int searchRange = 12;
    IntProvider height, radius, layerThickness;
    int maxHeightDiff = 1, heightDeviation = 3;
    FloatProvider density = FloatProvider::constant(0.5f), wetness = FloatProvider::constant(0.5f);
    float edgeChance = 0.1f;
    int edgeDistance = 3, centerDistance = 8;

    bool place(FeatureContext& ctx) const override {
        if (!ds.emptyOrWater(ctx.level.getBlock(ctx.origin))) return false;
        Xoroshiro& r = ctx.random;
        const int maxHeight = height.sample(r);
        const float wet = wetness.sample(r), dens = density.sample(r);
        const int rx = radius.sample(r), rz = radius.sample(r);
        for (int dx = -rx; dx <= rx; ++dx)
            for (int dz = -rz; dz <= rz; ++dz) {
                const int fromEdge = std::min(rx - std::abs(dx), rz - std::abs(dz));
                const double chance = clampedMap(fromEdge, 0.0, edgeDistance, edgeChance, 1.0);
                placeColumn(ctx.level, r, ctx.origin.offset(dx, 0, dz), dx, dz, wet, chance, maxHeight, dens);
            }
        return true;
    }

private:
    void placeColumn(WorldGenRegion& level, Xoroshiro& r, const BlockPos& p, int dx, int dz, float wet, double chance,
                     int maxHeight, float dens) const {
        const auto found = scanColumn(
            level, p, searchRange, [&](StateId s) { return ds.emptyOrWater(s); },
            [&](StateId s) { return !ds.emptyOrWater(s); });
        if (!found || (!found->ceiling && !found->floor)) return;
        const std::optional<int> ceiling = found->ceiling;
        CaveColumn column = *found;
        if (r.nextFloat() < wet && found->floor && canPlacePool(level, {p.x, *found->floor, p.z})) {
            level.setBlock({p.x, *found->floor, p.z}, water);
            column.floor = *found->floor - 1;
        }
        const std::optional<int> floor = column.floor;
        int down = 0, up = 0; // stalactite and stalagmite lengths
        if (r.nextDouble() < chance && ceiling && !isLava(level, {p.x, *ceiling, p.z})) {
            replaceWithDripstoneBlocks(level, {p.x, *ceiling, p.z}, layerThickness.sample(r), Direction::Up);
            down = columnHeight(r, dx, dz, dens, floor ? std::min(maxHeight, *ceiling - *floor) : maxHeight);
        }
        if (r.nextDouble() < chance && floor && !isLava(level, {p.x, *floor, p.z})) {
            replaceWithDripstoneBlocks(level, {p.x, *floor, p.z}, layerThickness.sample(r), Direction::Down);
            up = ceiling ? std::max(0, down + randomBetweenInclusive(r, -maxHeightDiff, maxHeightDiff))
                         : columnHeight(r, dx, dz, dens, maxHeight);
        }
        if (ceiling && floor && *ceiling - down <= *floor + up) {
            // They would overlap: pick where they meet.
            const int lo = std::max(*ceiling - down, *floor + 1), hi = std::min(*floor + up, *ceiling - 1);
            const int meet = randomBetweenInclusive(r, lo, hi + 1);
            down = *ceiling - meet;
            up = meet - 1 - *floor;
        }
        const bool merge = r.nextBool() && down > 0 && up > 0 && column.height() && down + up == *column.height();
        if (ceiling) ds.grow(level, {p.x, *ceiling - 1, p.z}, Direction::Down, down, merge);
        if (floor) ds.grow(level, {p.x, *floor + 1, p.z}, Direction::Up, up, merge);
    }

    bool isLava(const WorldGenRegion& level, const BlockPos& p) const { return level.info(p).block == ds.lava; }

    // A pool needs stone (or water) on four sides and below, and no water above.
    bool canPlacePool(const WorldGenRegion& level, const BlockPos& p) const {
        const uint16_t b = level.info(p).block;
        if (b == ds.water || b == ds.dripstoneBlock || b == ds.pointedBlock) return false;
        if (level.fluid(p.above()).kind == FluidKind::Water) return false;
        auto holdsWater = [&](const BlockPos& q) {
            return level.hasTag(q, ds.baseStone) || level.fluid(q).kind == FluidKind::Water;
        };
        for (Direction d : kHorizontal)
            if (!holdsWater(p.offset(d))) return false;
        return holdsWater(p.below());
    }

    void replaceWithDripstoneBlocks(WorldGenRegion& level, BlockPos p, int n, Direction d) const {
        for (int i = 0; i < n && ds.placeBlock(level, p); ++i) p = p.offset(d);
    }

    // Vanilla getDripstoneHeight: none past the density, else a normal spread that is
    // tallest near the middle of the cluster.
    int columnHeight(Xoroshiro& r, int dx, int dz, float dens, int maxHeight) const {
        if (r.nextFloat() > dens) return 0;
        const int dist = std::abs(dx) + std::abs(dz);
        const float mean = float(clampedMap(dist, 0.0, centerDistance, maxHeight / 2.0, 0.0));
        const float h = mean + float(r.nextGaussian()) * float(heightDeviation);
        return int(std::clamp(h, 0.0f, float(maxHeight)));
    }
};

std::unique_ptr<Feature> makeDripstoneCluster(const Json& c, FeatureLoader& l) {
    auto height = IntProvider::parse(c.value("height", Json()), l);
    auto radius = IntProvider::parse(c.value("radius", Json()), l);
    auto layer = IntProvider::parse(c.value("dripstone_block_layer_thickness", Json()), l);
    auto density = FloatProvider::parse(c.value("density", Json()), l);
    auto wetness = FloatProvider::parse(c.value("wetness", Json()), l);
    if (!height || !radius || !layer || !density || !wetness) return nullptr;
    auto f = std::make_unique<DripstoneClusterFeature>(l.registry);
    f->searchRange = l.getInt(c, "floor_to_ceiling_search_range", 12);
    f->height = *height;
    f->radius = *radius;
    f->maxHeightDiff = l.getInt(c, "max_stalagmite_stalactite_height_diff", 1);
    f->heightDeviation = l.getInt(c, "height_deviation", 3);
    f->layerThickness = *layer;
    f->density = *density;
    f->wetness = *wetness;
    f->edgeChance = l.getFloat(c, "chance_of_dripstone_column_at_max_distance_from_center", 0.1f);
    f->edgeDistance = l.getInt(c, "max_distance_from_edge_affecting_chance_of_dripstone_column", 3);
    f->centerDistance = l.getInt(c, "max_distance_from_center_affecting_height_bias", 8);
    if (f->maxHeightDiff < 0 || f->edgeDistance < 1 || f->centerDistance < 1) {
        l.error("dripstone_cluster distances out of range");
        return nullptr;
    }
    return f;
}

// ---------------------------------------------------------------- large_dripstone

// Vanilla LargeDripstoneFeature.WindOffsetter: shears a dripstone sideways in
// proportion to the distance from the origin's height.
struct Wind {
    bool active = false;
    int originY = 0;
    double vx = 0.0, vz = 0.0;

    static Wind blowing(int originY, Xoroshiro& r, const FloatProvider& speed) {
        const float f = speed.sample(r);
        const float a = r.nextFloat() * kPiF;
        return {true, originY, double(std::cos(a) * f), double(std::sin(a) * f)};
    }
    BlockPos offset(const BlockPos& p) const {
        if (!active) return p;
        const int dy = originY - p.y;
        return p.offset(int(std::floor(vx * dy)), 0, int(std::floor(vz * dy)));
    }
};

// Vanilla LargeDripstoneFeature.LargeDripstone: one cone of dripstone blocks.
struct Spike {
    BlockPos root;
    bool up; // a stalagmite
    int radius;
    double bluntness, scale;

    int heightAt(float r) const { return int(dripstoneHeight(r, radius, scale, bluntness)); }

    // moveBackUntilBaseIsInsideStoneAndShrinkRadiusIfNecessary: sinks the root up to
    // 10 blocks into the rock until a circle around it is all rock, halving the
    // radius after each failed pass.
    bool settle(const WorldGenRegion& level, const Dripstone& ds, const Wind& wind) {
        while (radius > 1) {
            BlockPos p = root;
            const int steps = std::min(10, heightAt(0.0f));
            for (int i = 0; i < steps; ++i) {
                if (level.info(p).block == ds.lava) return false;
                if (ds.circleEmbedded(level, wind.offset(p), radius)) {
                    root = p;
                    return true;
                }
                p.y += up ? -1 : 1;
            }
            radius /= 2;
        }
        return false;
    }

    void place(WorldGenRegion& level, const Dripstone& ds, Xoroshiro& r, const Wind& wind) const {
        for (int dx = -radius; dx <= radius; ++dx)
            for (int dz = -radius; dz <= radius; ++dz) {
                const float dist = std::sqrt(float(dx * dx + dz * dz));
                if (dist > float(radius)) continue;
                int h = heightAt(dist);
                if (h <= 0) continue;
                if (double(r.nextFloat()) < 0.2) h = int(float(h) * (r.nextFloat() * 0.2f + 0.8f));
                BlockPos p = root.offset(dx, 0, dz);
                bool placed = false;
                // Stalagmites stop at the world surface.
                const int limit = up ? level.height(Heightmap::WorldSurface, p.x, p.z) : INT_MAX;
                for (int i = 0; i < h && p.y < limit; ++i) {
                    const BlockPos q = wind.offset(p);
                    const StateId s = level.getBlock(q);
                    if (ds.emptyOrWaterOrLava(s)) {
                        placed = true;
                        level.setBlock(q, ds.block);
                    } else if (placed && level.registry().hasTag(s, ds.baseStone)) {
                        break; // reached rock again across the cave
                    }
                    p.y += up ? 1 : -1;
                }
            }
    }
};

// Vanilla LargeDripstoneFeature: a stalactite and a stalagmite of dripstone
// blocks, sized to the cave's height, sometimes leaning with the "wind".
class LargeDripstoneFeature final : public Feature {
public:
    explicit LargeDripstoneFeature(const BlockRegistry& r) : ds(r) {}

    Dripstone ds;
    int searchRange = 30;
    IntProvider columnRadius;
    FloatProvider heightScale = FloatProvider::constant(1.0f), stalactiteBluntness = FloatProvider::constant(0.5f),
                  stalagmiteBluntness = FloatProvider::constant(0.5f), windSpeed = FloatProvider::constant(0.0f);
    float maxRadiusRatio = 0.33f, minBluntnessForWind = 0.6f;
    int minRadiusForWind = 4;

    bool place(FeatureContext& ctx) const override {
        WorldGenRegion& level = ctx.level;
        Xoroshiro& r = ctx.random;
        const BlockPos o = ctx.origin;
        if (!ds.emptyOrWater(level.getBlock(o))) return false;
        const auto col = scanColumn(
            level, o, searchRange, [&](StateId s) { return ds.emptyOrWater(s); },
            [&](StateId s) { return ds.baseOrLava(s); });
        if (!col || !col->height() || *col->height() < 4) return false;
        const int lo = columnRadius.minValue(), hi = columnRadius.maxValue();
        const int cap = std::clamp(int(float(*col->height()) * maxRadiusRatio), lo, hi);
        const int rad = randomBetweenInclusive(r, lo, cap);
        Spike top{{o.x, *col->ceiling - 1, o.z}, false, rad, 0.0, 0.0};
        top.bluntness = stalactiteBluntness.sample(r);
        top.scale = heightScale.sample(r);
        Spike bottom{{o.x, *col->floor + 1, o.z}, true, rad, 0.0, 0.0};
        bottom.bluntness = stalagmiteBluntness.sample(r);
        bottom.scale = heightScale.sample(r);
        auto windy = [&](const Spike& s) { return s.radius >= minRadiusForWind && s.bluntness >= minBluntnessForWind; };
        const Wind wind = windy(top) && windy(bottom) ? Wind::blowing(o.y, r, windSpeed) : Wind{};
        const bool placeTop = top.settle(level, ds, wind), placeBottom = bottom.settle(level, ds, wind);
        if (placeTop) top.place(level, ds, r, wind);
        if (placeBottom) bottom.place(level, ds, r, wind);
        return true;
    }
};

std::unique_ptr<Feature> makeLargeDripstone(const Json& c, FeatureLoader& l) {
    auto radius = IntProvider::parse(c.value("column_radius", Json()), l);
    auto scale = FloatProvider::parse(c.value("height_scale", Json()), l);
    auto topBlunt = FloatProvider::parse(c.value("stalactite_bluntness", Json()), l);
    auto bottomBlunt = FloatProvider::parse(c.value("stalagmite_bluntness", Json()), l);
    auto wind = FloatProvider::parse(c.value("wind_speed", Json()), l);
    if (!radius || !scale || !topBlunt || !bottomBlunt || !wind) return nullptr;
    if (radius->minValue() > radius->maxValue()) {
        l.error("large_dripstone column_radius is empty");
        return nullptr;
    }
    auto f = std::make_unique<LargeDripstoneFeature>(l.registry);
    f->searchRange = l.getInt(c, "floor_to_ceiling_search_range", 30);
    f->columnRadius = *radius;
    f->heightScale = *scale;
    f->maxRadiusRatio = l.getFloat(c, "max_column_radius_to_cave_height_ratio", 0.33f);
    f->stalactiteBluntness = *topBlunt;
    f->stalagmiteBluntness = *bottomBlunt;
    f->windSpeed = *wind;
    f->minRadiusForWind = l.getInt(c, "min_radius_for_wind", 4);
    f->minBluntnessForWind = l.getFloat(c, "min_bluntness_for_wind", 0.6f);
    return f;
}

// ---------------------------------------------------------------- pointed_dripstone

// Vanilla PointedDripstoneFeature: a one or two block stalactite or stalagmite
// on a small patch of dripstone blocks.
class PointedDripstoneFeature final : public Feature {
public:
    explicit PointedDripstoneFeature(const BlockRegistry& r) : ds(r) {}

    Dripstone ds;
    float tallerChance = 0.2f, spreadChance = 0.7f, radius2Chance = 0.5f, radius3Chance = 0.5f;

    bool place(FeatureContext& ctx) const override {
        WorldGenRegion& level = ctx.level;
        Xoroshiro& r = ctx.random;
        const BlockPos o = ctx.origin;
        const bool above = ds.base(level.getBlock(o.above())), below = ds.base(level.getBlock(o.below()));
        if (!above && !below) return false;
        const Direction tip = above && below ? (r.nextBool() ? Direction::Down : Direction::Up)
                              : above        ? Direction::Down
                                             : Direction::Up;
        patch(level, r, o.offset(opposite(tip)));
        const int height = r.nextFloat() < tallerChance && ds.emptyOrWater(level.getBlock(o.offset(tip))) ? 2 : 1;
        ds.grow(level, o, tip, height, false);
        return true;
    }

private:
    // createPatchOfDripstoneBlocks: the base, and short random walks from its sides.
    void patch(WorldGenRegion& level, Xoroshiro& r, const BlockPos& p) const {
        ds.placeBlock(level, p);
        for (Direction d : kHorizontal) {
            if (r.nextFloat() > spreadChance) continue;
            const BlockPos a = p.offset(d);
            ds.placeBlock(level, a);
            if (r.nextFloat() > radius2Chance) continue;
            const BlockPos b = a.offset(kDirections[size_t(r.nextInt(6))]);
            ds.placeBlock(level, b);
            if (r.nextFloat() > radius3Chance) continue;
            ds.placeBlock(level, b.offset(kDirections[size_t(r.nextInt(6))]));
        }
    }
};

std::unique_ptr<Feature> makePointedDripstone(const Json& c, FeatureLoader& l) {
    auto f = std::make_unique<PointedDripstoneFeature>(l.registry);
    f->tallerChance = l.getFloat(c, "chance_of_taller_dripstone", 0.2f);
    f->spreadChance = l.getFloat(c, "chance_of_directional_spread", 0.7f);
    f->radius2Chance = l.getFloat(c, "chance_of_spread_radius2", 0.5f);
    f->radius3Chance = l.getFloat(c, "chance_of_spread_radius3", 0.5f);
    return f;
}

} // namespace

void registerDripstoneFeatures(FeatureSet& set) {
    set.registerFeature("dripstone_cluster", makeDripstoneCluster);
    set.registerFeature("large_dripstone", makeLargeDripstone);
    set.registerFeature("pointed_dripstone", makePointedDripstone);
}

} // namespace mc
