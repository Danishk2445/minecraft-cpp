// VegetationIce.cpp -- vanilla frozen-biome features: iceberg (round or
// elliptical bergs of packed or blue ice, snow caps, carved arches), blue_ice
// (clusters under icebergs) and ice_spike (packed ice spires).
#include <algorithm>
#include <cmath>

#include "world/gen/feature/Vegetation.h"

namespace mc {
namespace {

constexpr double kPi = 3.14159265358979323846;

// Vanilla's sea level is the first air layer above the ocean. Our terrain fills
// water up to WorldGenRegion::seaLevel() itself (one block higher than vanilla),
// so step up when that block is not air.
int airAboveSea(const WorldGenRegion& level, int x, int z) {
    const int y = level.seaLevel();
    return level.isAir({x, y, z}) ? y : y + 1;
}

// ---------------------------------------------------------------- iceberg

// Vanilla IcebergFeature, from sea level: round (70%) or elliptical, up to 42
// layers above the water and 17 below, snow-capped 30% of the time, and often
// with an arch or cave carved through it.
class IcebergFeature final : public Feature {
public:
    StateId main = 0, snowBlock = 0, air = 0, waterState = 0;
    uint16_t water = 0, packedIce = 0, blueIce = 0, snowBlockId = 0, ice = 0, snowLayer = 0;

    bool place(FeatureContext& ctx) const override {
        WorldGenRegion& level = ctx.level;
        Xoroshiro& r = ctx.random;
        const BlockPos o(ctx.origin.x, airAboveSea(level, ctx.origin.x, ctx.origin.z), ctx.origin.z);
        const bool snowCap = r.nextDouble() > 0.7;
        const double slant = r.nextDouble() * 2.0 * kPi;
        const int ellipseC = 11 - r.nextInt(5);
        const int minor = 3 + r.nextInt(3);
        const bool elliptical = r.nextDouble() > 0.7;
        int height = elliptical ? r.nextInt(6) + 6 : r.nextInt(15) + 3;
        if (!elliptical && r.nextDouble() > 0.9) height += r.nextInt(19) + 7;
        const int depth = std::min(height + r.nextInt(11), 18);
        const int grow = r.nextInt(7);
        const int major = std::min(height + grow - r.nextInt(5), 11);
        const int reach = elliptical ? ellipseC : 11;
        // Above the water. For round bergs, vanilla's `x < radius` test only drops the
        // top layers, whose radius went negative.
        for (int x = -reach; x < reach; ++x)
            for (int z = -reach; z < reach; ++z)
                for (int y = 0; y < height; ++y) {
                    const int rad = elliptical ? radiusEllipse(y, height, major) : radiusRound(r, y, height, major);
                    if (elliptical || x < rad)
                        icebergBlock(level, r, o, height, x, y, z, rad, reach, elliptical, minor, slant, snowCap);
                }
        smooth(level, o, major, height, elliptical, ellipseC);
        // Under the water.
        for (int x = -reach; x < reach; ++x)
            for (int z = -reach; z < reach; ++z)
                for (int y = -1; y > -depth; --y) {
                    const int out =
                        elliptical ? veg::ceil(float(reach) * (1.0f - float(y * y) / (float(depth) * 8.0f))) : reach;
                    const int rad = radiusSteep(r, -y, depth, major);
                    if (x < rad) icebergBlock(level, r, o, depth, x, y, z, rad, out, elliptical, minor, slant, snowCap);
                }
        const bool cut = elliptical ? r.nextDouble() > 0.1 : r.nextDouble() > 0.7;
        if (cut) cutOut(level, r, major, height, o, elliptical, ellipseC, slant, minor);
        return true;
    }

private:
    bool isIceberg(uint16_t b) const { return b == packedIce || b == snowBlockId || b == blueIce; }

    void icebergBlock(WorldGenRegion& level, Xoroshiro& r, const BlockPos& o, int height, int x, int y, int z,
                      int radius, int majorRadius, bool elliptical, int minor, double slant, bool snowCap) const {
        const double d = elliptical ? ellipse(x, z, 0, 0, majorRadius, ellipseC(y, height, minor), slant)
                                    : circle(r, x, z, radius);
        if (!(d < 0.0)) return;
        // Ragged rim: skip 10% of the blocks near the edge.
        const double rim = elliptical ? -0.5 : double(-6 - r.nextInt(3));
        if (d > rim && r.nextDouble() > 0.9) return;
        setIcebergBlock(level, r, o.offset(x, y, z), height - y, height, elliptical, snowCap);
    }

    void setIcebergBlock(WorldGenRegion& level, Xoroshiro& r, const BlockPos& p, int remaining, int height,
                         bool elliptical, bool snowCap) const {
        const uint16_t b = level.info(p).block;
        if (!level.isAir(p) && b != snowBlockId && b != ice && b != water) return;
        const bool snowOk = !elliptical || r.nextDouble() > 0.05;
        const int div = elliptical ? 3 : 2;
        if (snowCap && b != water &&
            double(remaining) <= double(r.nextInt(std::max(1, height / div))) + double(height) * 0.6 && snowOk)
            level.setBlock(p, snowBlock);
        else
            level.setBlock(p, main);
    }

    // Vanilla signedDistanceEllipse. A zero radius divides by zero in Java (an
    // infinite or NaN distance, never inside); return +inf instead.
    static double ellipse(int x, int z, int cx, int cz, int majorRadius, int minorRadius, double angle) {
        if (majorRadius == 0 || minorRadius == 0) return HUGE_VAL;
        const double dx = x - cx, dz = z - cz, c = std::cos(angle), s = std::sin(angle);
        const double u = (dx * c - dz * s) / majorRadius, v = (dx * s + dz * c) / minorRadius;
        return u * u + v * v - 1.0;
    }

    // Vanilla signedDistanceCircle with a random 2..8/radius fuzz.
    static double circle(Xoroshiro& r, int x, int z, int radius) {
        const float fuzz = std::clamp(r.nextFloat(), 0.2f, 0.8f);
        if (radius == 0) return HUGE_VAL;
        const float f = 10.0f * fuzz / float(radius);
        return double(f) + double(x) * x + double(z) * z - double(radius) * radius;
    }

    // The elliptical berg's minor radius shrinks over its top three layers.
    static int ellipseC(int y, int height, int minor) {
        return y > 0 && height - y <= 3 ? minor - (4 - (height - y)) : minor;
    }

    static int radiusRound(Xoroshiro& r, int y, int height, int maxRadius) {
        const float f = 3.5f - r.nextFloat();
        float g = (1.0f - float(y * y) / (float(height) * f)) * float(maxRadius);
        if (height > 15 + r.nextInt(5)) {
            const int i = y < 3 + r.nextInt(6) ? y / 2 : y;
            g = (1.0f - float(i) / (float(height) * f * 0.4f)) * float(maxRadius);
        }
        return veg::ceil(g / 2.0f);
    }

    static int radiusEllipse(int y, int height, int maxRadius) {
        const float g = (1.0f - float(y * y) / float(height)) * float(maxRadius);
        return veg::ceil(g / 2.0f);
    }

    static int radiusSteep(Xoroshiro& r, int y, int height, int maxRadius) {
        const float f = 1.0f + r.nextFloat() / 2.0f;
        const float g = (1.0f - float(y) / (float(height) * f)) * float(maxRadius);
        return veg::ceil(g / 2.0f);
    }

    // Removes overhanging blocks (with what stands on them) and lone spikes above the water.
    void smooth(WorldGenRegion& level, const BlockPos& o, int major, int height, bool elliptical, int ellipseC) const {
        const int reach = elliptical ? ellipseC : major / 2;
        for (int x = -reach; x <= reach; ++x)
            for (int z = -reach; z <= reach; ++z)
                for (int y = 0; y <= height; ++y) {
                    const BlockPos p = o.offset(x, y, z);
                    const uint16_t b = level.info(p).block;
                    if (!isIceberg(b) && b != snowLayer) continue;
                    if (level.isAir(p.below())) {
                        level.setBlock(p, air);
                        level.setBlock(p.above(), air);
                    } else if (isIceberg(b)) {
                        int open = 0;
                        for (Direction d : {Direction::West, Direction::East, Direction::North, Direction::South})
                            open += !isIceberg(level.info(p.offset(d)).block);
                        if (open >= 3) level.setBlock(p, air);
                    }
                }
    }

    // An elliptical tunnel through the berg: air above the water, water below it.
    void cutOut(WorldGenRegion& level, Xoroshiro& r, int major, int height, const BlockPos& o, bool elliptical,
                int ellipseC, double slant, int minor) const {
        const int sx = r.nextBool() ? -1 : 1;
        const int sz = r.nextBool() ? -1 : 1;
        int ox = r.nextInt(std::max(major / 2 - 2, 1));
        if (r.nextBool()) ox = major / 2 + 1 - r.nextInt(std::max(major - major / 2 - 1, 1));
        int oz = r.nextInt(std::max(major / 2 - 2, 1));
        if (r.nextBool()) oz = major / 2 + 1 - r.nextInt(std::max(major - major / 2 - 1, 1));
        if (elliptical) ox = oz = r.nextInt(std::max(ellipseC - 5, 1));
        const int cx = sx * ox, cz = sz * oz;
        const double angle = elliptical ? slant + kPi / 2 : r.nextDouble() * 2.0 * kPi;
        for (int y = 0; y < height - 3; ++y)
            carve(level, radiusRound(r, y, height, major), y, o, false, angle, cx, cz, ellipseC, minor);
        for (int y = -1; y > -height + r.nextInt(5); --y)
            carve(level, radiusSteep(r, -y, height, major), y, o, true, angle, cx, cz, ellipseC, minor);
    }

    void carve(WorldGenRegion& level, int radius, int y, const BlockPos& o, bool underwater, double angle, int cx,
               int cz, int majorRadius, int minorRadius) const {
        const int a = radius + 1 + majorRadius / 3;
        const int b = std::min(radius - 3, 3) + minorRadius / 2 - 1;
        for (int x = -a; x < a; ++x)
            for (int z = -a; z < a; ++z) {
                if (!(ellipse(x, z, cx, cz, a, b, angle) < 0.0)) continue;
                const BlockPos p = o.offset(x, y, z);
                if (!isIceberg(level.info(p).block)) continue;
                if (underwater) {
                    level.setBlock(p, waterState);
                } else {
                    level.setBlock(p, air);
                    if (veg::isBlock(level, p.above(), snowLayer)) level.setBlock(p.above(), air);
                }
            }
    }
};

std::unique_ptr<Feature> makeIceberg(const Json& c, FeatureLoader& l) {
    auto f = std::make_unique<IcebergFeature>();
    auto main = l.state(c.value("state", Json()));
    auto snowBlock = l.state(Json("snow_block"));
    auto air = l.state(Json("air"));
    auto waterState = l.state(Json("water"));
    auto water = veg::requireBlock(l, "water");
    auto packedIce = veg::requireBlock(l, "packed_ice");
    auto blueIce = veg::requireBlock(l, "blue_ice");
    auto ice = veg::requireBlock(l, "ice");
    auto snowLayer = veg::requireBlock(l, "snow");
    if (!main || !snowBlock || !air || !waterState || !water || !packedIce || !blueIce || !ice || !snowLayer)
        return nullptr;
    f->main = *main;
    f->snowBlock = *snowBlock;
    f->air = *air;
    f->waterState = *waterState;
    f->water = *water;
    f->packedIce = *packedIce;
    f->blueIce = *blueIce;
    f->snowBlockId = l.registry.info(*snowBlock).block;
    f->ice = *ice;
    f->snowLayer = *snowLayer;
    return f;
}

// ---------------------------------------------------------------- blue_ice

// Vanilla BlueIceFeature: in the water under the sea surface, touching packed
// ice (an iceberg's underwater part), a blue ice block grown into a cluster by
// 200 tries next to blue ice.
class BlueIceFeature final : public Feature {
public:
    StateId blueIce = 0;
    uint16_t water = 0, packedIce = 0, ice = 0, blueIceId = 0;

    bool place(FeatureContext& ctx) const override {
        WorldGenRegion& level = ctx.level;
        Xoroshiro& r = ctx.random;
        const BlockPos o = ctx.origin;
        if (o.y > airAboveSea(level, o.x, o.z) - 1) return false;
        if (!veg::isBlock(level, o, water) && !veg::isBlock(level, o.below(), water)) return false;
        bool touches = false;
        for (Direction d : kDirections)
            if (d != Direction::Down && veg::isBlock(level, o.offset(d), packedIce)) {
                touches = true;
                break;
            }
        if (!touches) return false;
        level.setBlock(o, blueIce);
        for (int i = 0; i < 200; ++i) {
            const int up = r.nextInt(5);
            const int dy = up - r.nextInt(6);
            const int spread = dy < 2 ? 3 + dy / 2 : 3;
            if (spread < 1) continue;
            const int dx = veg::triangle(r, spread);
            const BlockPos p = o.offset(dx, dy, veg::triangle(r, spread));
            const uint16_t b = level.info(p).block;
            if (!level.isAir(p) && b != water && b != packedIce && b != ice) continue;
            for (Direction d : kDirections)
                if (veg::isBlock(level, p.offset(d), blueIceId)) {
                    level.setBlock(p, blueIce);
                    break;
                }
        }
        return true;
    }
};

std::unique_ptr<Feature> makeBlueIce(const Json&, FeatureLoader& l) {
    auto blueIce = l.state(Json("blue_ice"));
    auto water = veg::requireBlock(l, "water");
    auto packedIce = veg::requireBlock(l, "packed_ice");
    auto ice = veg::requireBlock(l, "ice");
    if (!blueIce || !water || !packedIce || !ice) return nullptr;
    auto f = std::make_unique<BlueIceFeature>();
    f->blueIce = *blueIce;
    f->blueIceId = l.registry.info(*blueIce).block;
    f->water = *water;
    f->packedIce = *packedIce;
    f->ice = *ice;
    return f;
}

// ---------------------------------------------------------------- ice_spike

// Vanilla IceSpikeFeature: a packed ice spire 7..10 tall and 1..3 wide (rarely
// lifted 10..39 blocks), mirrored below its base when wide, with roots reaching
// down through dirt and snow toward y 50.
class IceSpikeFeature final : public Feature {
public:
    StateId packedIce = 0;
    std::vector<uint16_t> base; // what the spike may stand on (vanilla: snow_block)
    TagId dirt = 0;
    uint16_t snowBlock = 0, ice = 0, packedIceId = 0;

    bool place(FeatureContext& ctx) const override {
        WorldGenRegion& level = ctx.level;
        Xoroshiro& r = ctx.random;
        BlockPos p = ctx.origin;
        while (level.isAir(p) && p.y > kWorldMinY + 2) p = p.below();
        if (std::find(base.begin(), base.end(), level.info(p).block) == base.end()) return false;
        p = p.above(r.nextInt(4));
        const int height = r.nextInt(4) + 7;
        const int width = height / 4 + r.nextInt(2);
        if (width > 1 && r.nextInt(60) == 0) p = p.above(10 + r.nextInt(30));
        for (int y = 0; y < height; ++y) {
            const float f = (1.0f - float(y) / float(height)) * float(width);
            const int l = veg::ceil(f);
            for (int x = -l; x <= l; ++x) {
                const float g = float(std::abs(x)) - 0.25f;
                for (int z = -l; z <= l; ++z) {
                    const float h = float(std::abs(z)) - 0.25f;
                    if (!(x == 0 && z == 0) && g * g + h * h > f * f) continue;
                    const bool rim = x == -l || x == l || z == -l || z == l;
                    if (rim && r.nextFloat() > 0.75f) continue;
                    if (canReplace(level, p.offset(x, y, z))) level.setBlock(p.offset(x, y, z), packedIce);
                    if (y != 0 && l > 1 && canReplace(level, p.offset(x, -y, z)))
                        level.setBlock(p.offset(x, -y, z), packedIce);
                }
            }
        }
        const int roots = std::clamp(width - 1, 0, 1);
        for (int x = -roots; x <= roots; ++x)
            for (int z = -roots; z <= roots; ++z) {
                BlockPos q = p.offset(x, -1, z);
                int run = std::abs(x) == 1 && std::abs(z) == 1 ? r.nextInt(5) : 50;
                while (q.y > 50 && (canReplace(level, q) || veg::isBlock(level, q, packedIceId))) {
                    level.setBlock(q, packedIce);
                    q = q.below();
                    if (--run <= 0) {
                        q = q.below(r.nextInt(5) + 1);
                        run = r.nextInt(5);
                    }
                }
            }
        return true;
    }

private:
    bool canReplace(const WorldGenRegion& level, const BlockPos& p) const {
        const StateInfo& i = level.info(p);
        return (i.flags & kAir) || level.hasTag(p, dirt) || i.block == snowBlock || i.block == ice;
    }
};

std::unique_ptr<Feature> makeIceSpike(const Json& c, FeatureLoader& l) {
    auto f = std::make_unique<IceSpikeFeature>();
    // Not in vanilla's (empty) config: which blocks a spike may stand on.
    for (const Json& b : c.value("base_blocks", Json::array({"snow_block"})))
        if (const Block* block = l.block(b)) f->base.push_back(block->index);
    auto packedIce = l.state(Json("packed_ice"));
    auto dirt = l.tag(Json("dirt"));
    auto snowBlock = veg::requireBlock(l, "snow_block");
    auto ice = veg::requireBlock(l, "ice");
    if (!packedIce || !dirt || !snowBlock || !ice || f->base.empty()) return nullptr;
    f->packedIce = *packedIce;
    f->packedIceId = l.registry.info(*packedIce).block;
    f->dirt = *dirt;
    f->snowBlock = *snowBlock;
    f->ice = *ice;
    return f;
}

} // namespace

void registerIceFeatures(FeatureSet& set) {
    set.registerFeature("iceberg", makeIceberg);
    set.registerFeature("blue_ice", makeBlueIce);
    set.registerFeature("ice_spike", makeIceSpike);
}

} // namespace mc
