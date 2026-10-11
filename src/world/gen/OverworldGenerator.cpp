#include "world/gen/OverworldGenerator.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <unordered_map>

#include "resources/ResourcePack.h"
#include "world/block/BlockRegistry.h"
#include "world/gen/TerrainSplines.h"

namespace mc {
namespace {

constexpr int kCellW = 4;  // horizontal density cell size
constexpr int kCellH = 8;  // vertical density cell size
constexpr int kCornersXZ = 16 / kCellW + 1;            // 5
constexpr int kCornersY = kWorldHeight / kCellH + 1;   // 49
constexpr double kPi = 3.14159265358979323846;

double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
double lerpd(double t, double a, double b) { return a + t * (b - a); }
// Vanilla yClampedGradient(fromY, toY, fromValue, toValue).
double yGradient(int y, int fromY, int toY, double fromValue, double toValue) {
    return lerpd(clampd(double(y - fromY) / double(toY - fromY), 0.0, 1.0), fromValue, toValue);
}
// Maps noise in [-1, 1] onto [lo, hi] (vanilla mappedNoise).
double mapped(double n, double lo, double hi) { return lo + (n + 1.0) * 0.5 * (hi - lo); }

double rarity3D(double d) { return d < -0.5 ? 0.75 : d < 0.0 ? 1.0 : d < 0.5 ? 1.5 : 2.0; }
double rarity2D(double d) { return d < -0.75 ? 0.5 : d < -0.5 ? 0.75 : d < 0.5 ? 1.0 : d < 0.75 ? 2.0 : 3.0; }

uint64_t blockHash(int64_t seed, int x, int y, int z) {
    return mixStafford13(uint64_t(seed) ^ uint64_t(positionSeed(x, y, z)) ^ 0x9E3779B97F4A7C15ull);
}
double blockRandom(int64_t seed, int x, int y, int z, uint64_t salt) {
    return double(mixStafford13(blockHash(seed, x, y, z) ^ salt) >> 11) * 0x1.0p-53;
}

} // namespace

struct OverworldGenerator::Impl {
    NormalNoise shift, temperature, vegetation, continentalness, erosion, ridge, jagged;
    BlendedNoise base3d;
    NormalNoise caveLayer, caveCheese, caveEntrance;
    NormalNoise sp3dRarity, sp3d1, sp3d2, sp3dThickness, roughness, roughnessMod;
    NormalNoise sp2dMod, sp2d, sp2dElevation, sp2dThickness;
    NormalNoise noodle, noodleThickness, noodleRidgeA, noodleRidgeB;
    NormalNoise surface, surfaceSecondary, gravelPatches;
    const TerrainSplines& splines = TerrainSplines::overworld();

    // Block states.
    StateId air, stone, deepslate, water, lava, bedrock, grass, dirt, coarseDirt, podzol, mycelium, sand, redSand,
        sandstone, redSandstone, gravel, clay, snowBlock, packedIce, calcite, terracotta, mud;
    std::array<StateId, 64> badlandsBands{};
    std::unordered_map<std::string, BiomeId> biomeIds;

    struct Column {
        float t, h, c, e, w;
        float offset, factor, jaggedness, jaggedNoise;
    };

    Column column(int x, int z) const {
        const double sx = shift.value(x * 0.25, 0.0, z * 0.25) * 4.0;
        const double sz = shift.value(z * 0.25, x * 0.25, 0.0) * 4.0;
        const double qx = x * 0.25 + sx, qz = z * 0.25 + sz;
        Column col;
        col.t = float(temperature.value(qx, 0.0, qz));
        col.h = float(vegetation.value(qx, 0.0, qz));
        col.c = float(continentalness.value(qx, 0.0, qz));
        col.e = float(erosion.value(qx, 0.0, qz));
        col.w = float(ridge.value(qx, 0.0, qz));
        const SplinePoint sp{col.c, col.e, col.w, peaksAndValleys(col.w)};
        col.offset = splines.offset->eval(sp) - 0.50375f;
        col.factor = splines.factor->eval(sp);
        col.jaggedness = splines.jaggedness->eval(sp);
        col.jaggedNoise = float(jagged.value(x * 1500.0, 0.0, z * 1500.0));
        return col;
    }

    static double depthAt(const Column& c, int y) { return yGradient(y, -64, 320, 1.5, -1.5) + c.offset; }

    double spaghettiRoughness(int x, int y, int z) const {
        const double r = roughness.value(x, y, z);
        const double mod = mapped(roughnessMod.value(x, y, z), 0.0, -0.1);
        return mod * (std::abs(r) - 0.4);
    }
    double spaghetti3d(int x, int y, int z) const {
        const double e = rarity3D(sp3dRarity.value(x * 2.0, y, z * 2.0));
        const double a = e * std::abs(sp3d1.value(x / e, y / e, z / e));
        const double b = e * std::abs(sp3d2.value(x / e, y / e, z / e));
        const double thick = mapped(sp3dThickness.value(x, y, z), -0.065, -0.088);
        return clampd(std::max(a, b) + thick, -1.0, 1.0);
    }
    double entrances(int x, int y, int z) const {
        const double ce = caveEntrance.value(x * 0.75, y * 0.5, z * 0.75) + 0.37 + yGradient(y, -10, 30, 0.3, 0.0);
        if (ce < -0.5) return ce; // the tunnel term below cannot go much lower
        return std::min(ce, spaghettiRoughness(x, y, z) + spaghetti3d(x, y, z));
    }
    double spaghetti2d(int x, int y, int z) const {
        const double e = rarity2D(sp2dMod.value(x * 2.0, y, z * 2.0));
        const double s = e * std::abs(sp2d.value(x / e, y / e, z / e));
        const double elevation = mapped(sp2dElevation.value(x, 0.0, z), -8.0, 8.0);
        const double thick = mapped(sp2dThickness.value(x * 2.0, y, z * 2.0), -0.6, -1.3);
        const double g = std::abs(elevation + yGradient(y, -64, 320, 8.0, -40.0));
        const double h = std::pow(g + thick, 3.0);
        const double k = s + 0.083 * thick;
        return clampd(std::max(k, h), -1.0, 1.0);
    }
    double underground(int x, int y, int z, double slopedCheese) const {
        const double layer = caveLayer.value(x, y * 8.0, z);
        const double cheese = caveCheese.value(x, y * 0.6666666666666666, z);
        const double cheeseCaves = clampd(0.27 + cheese, -1.0, 1.0) + clampd(1.5 - 0.64 * slopedCheese, 0.0, 0.5);
        const double caves = 4.0 * layer * layer + cheeseCaves;
        const double result = std::min(caves, entrances(x, y, z));
        return std::min(result, spaghetti2d(x, y, z) + spaghettiRoughness(x, y, z));
    }
    // Noodle cave inputs at a cell corner. Vanilla interpolates each one on its own
    // and only then picks per block (noodleValue): interpolating the final value
    // would smear the "no noodle here" constant into neighboring cells.
    struct Noodle {
        float select, thickness, ridgeA, ridgeB;
    };
    Noodle noodleInputs(int x, int y, int z) const {
        if (y < -60 || y > 320) return {-1.0f, 0.0f, 0.0f, 0.0f};
        constexpr double k = 2.6666666666666665;
        return {float(noodle.value(x, y, z)), float(mapped(noodleThickness.value(x, y, z), -0.05, -0.1)),
                float(noodleRidgeA.value(x * k, y * k, z * k)), float(noodleRidgeB.value(x * k, y * k, z * k))};
    }
    static double noodleValue(const Noodle& n) {
        if (n.select < 0.0f) return 64.0;
        return n.thickness + 1.5 * std::max(std::abs(n.ridgeA), std::abs(n.ridgeB));
    }
    double noodleAt(int x, int y, int z) const { return noodleValue(noodleInputs(x, y, z)); }
    // Final density before squeeze (only its sign is used).
    double density(int x, int y, int z, const Column& c) const {
        const double jag = c.jaggedness * (c.jaggedNoise > 0 ? c.jaggedNoise : c.jaggedNoise * 0.5);
        const double d = (depthAt(c, y) + jag) * c.factor;
        double f;
        // Far above or below the surface the 3D noise cannot flip the sign: skip it.
        const double base = 4.0 * (d > 0 ? d : d * 0.25);
        if (base > 3.0) {
            f = underground(x, y, z, base);
        } else if (base < -1.5) {
            f = base;
        } else {
            const double sloped = base + base3d.value(x, y, z);
            if (sloped < 1.5625) f = sloped <= 0.0 ? sloped : std::min(sloped, 5.0 * entrances(x, y, z));
            else f = underground(x, y, z, sloped);
        }
        // Slides: air above y 256, solid toward the bottom of the world.
        f = lerpd(yGradient(y, 240, 256, 1.0, 0.0), -0.078125, f);
        f = lerpd(yGradient(y, -64, -40, 0.0, 1.0), 0.1171875, f);
        return f;
    }
};

OverworldGenerator::OverworldGenerator(const BlockRegistry& blocks, const BiomeRegistry& biomes, int64_t seed)
    : blocks_(blocks), biomes_(biomes), seed_(seed), impl_(std::make_unique<Impl>()) {
    const Xoroshiro base(seed);
    Impl& m = *impl_;
    m.shift = NormalNoise(base, "offset", -3, {1, 1, 1, 0});
    m.temperature = NormalNoise(base, "temperature", -10, {1.5, 0, 1, 0, 0, 0});
    m.vegetation = NormalNoise(base, "vegetation", -8, {1, 1, 0, 0, 0, 0});
    m.continentalness = NormalNoise(base, "continentalness", -9, {1, 1, 2, 2, 2, 1, 1, 1, 1});
    m.erosion = NormalNoise(base, "erosion", -9, {1, 1, 0, 1, 1});
    m.ridge = NormalNoise(base, "ridge", -7, {1, 2, 1, 0, 0, 0});
    m.jagged = NormalNoise(base, "jagged", -16, std::vector<double>(16, 1.0));
    m.base3d = BlendedNoise(base.named("terrain"), 0.25, 0.125, 80.0, 160.0, 8.0);
    m.caveLayer = NormalNoise(base, "cave_layer", -8, {1});
    m.caveCheese = NormalNoise(base, "cave_cheese", -8, {0.5, 1, 2, 1, 2, 1, 0, 2, 0});
    m.caveEntrance = NormalNoise(base, "cave_entrance", -7, {0.4, 0.5, 1});
    m.sp3dRarity = NormalNoise(base, "spaghetti_3d_rarity", -11, {1});
    m.sp3d1 = NormalNoise(base, "spaghetti_3d_1", -7, {1});
    m.sp3d2 = NormalNoise(base, "spaghetti_3d_2", -7, {1});
    m.sp3dThickness = NormalNoise(base, "spaghetti_3d_thickness", -8, {1});
    m.roughness = NormalNoise(base, "spaghetti_roughness", -5, {1});
    m.roughnessMod = NormalNoise(base, "spaghetti_roughness_modulator", -8, {1});
    m.sp2dMod = NormalNoise(base, "spaghetti_2d_modulator", -11, {1});
    m.sp2d = NormalNoise(base, "spaghetti_2d", -7, {1});
    m.sp2dElevation = NormalNoise(base, "spaghetti_2d_elevation", -8, {1});
    m.sp2dThickness = NormalNoise(base, "spaghetti_2d_thickness", -11, {1});
    m.noodle = NormalNoise(base, "noodle", -8, {1});
    m.noodleThickness = NormalNoise(base, "noodle_thickness", -8, {1});
    m.noodleRidgeA = NormalNoise(base, "noodle_ridge_a", -7, {1});
    m.noodleRidgeB = NormalNoise(base, "noodle_ridge_b", -7, {1});
    m.surface = NormalNoise(base, "surface", -6, {1, 1, 1});
    m.surfaceSecondary = NormalNoise(base, "surface_secondary", -6, {1, 1, 0, 1});
    m.gravelPatches = NormalNoise(base, "gravel", -8, {1, 1, 1, 1});

    auto S = [&](const char* id) { return blocks.defaultState(id); };
    m.air = BlockRegistry::kAirState;
    m.stone = S("stone");
    m.deepslate = S("deepslate");
    m.water = S("water");
    m.lava = S("lava");
    m.bedrock = S("bedrock");
    m.grass = S("grass_block");
    m.dirt = S("dirt");
    m.coarseDirt = S("coarse_dirt");
    m.podzol = S("podzol");
    m.mycelium = S("mycelium");
    m.sand = S("sand");
    m.redSand = S("red_sand");
    m.sandstone = S("sandstone");
    m.redSandstone = S("red_sandstone");
    m.gravel = S("gravel");
    m.clay = S("clay");
    m.snowBlock = S("snow_block");
    m.packedIce = S("packed_ice");
    m.calcite = S("calcite");
    m.terracotta = S("terracotta");
    m.mud = S("mud");
    // Badlands bands: seeded runs of colored terracotta over plain terracotta.
    {
        const char* colors[] = {"orange_terracotta", "yellow_terracotta", "brown_terracotta", "red_terracotta",
                                "white_terracotta", "light_gray_terracotta"};
        Xoroshiro r = base.named("badlands_bands");
        m.badlandsBands.fill(m.terracotta);
        for (int i = 0; i < 64; i += 1 + r.nextInt(4)) {
            const StateId s = S(colors[r.nextInt(6)]);
            const int len = 1 + r.nextInt(3);
            for (int k = 0; k < len && i + k < 64; ++k) m.badlandsBands[size_t(i + k)] = s;
            i += len;
        }
    }
    for (size_t i = 0; i < biomes.size(); ++i) m.biomeIds.emplace(std::string(shortId(biomes.get(BiomeId(i)).id)), BiomeId(i));

    // Spawn: the nearest land column (not ocean/river/beach/peaks) along a square
    // spiral. The climate picks candidates cheaply; the real height comes from
    // generating the candidate's chunk, since the 3D noise moves the surface by
    // tens of blocks from the spline estimate.
    spawn_ = {0.5, 100.0, 0.5};
    for (int r = 0; r <= 1024; r += 16) {
        bool found = false;
        for (int dz = -r; dz <= r && !found; dz += 16)
            for (int dx = -r; dx <= r && !found; dx += 16) {
                if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
                const int estimate = estimatedSurface(dx, dz);
                if (estimate <= kSeaLevel + 2) continue;
                const std::string_view b = shortId(biomes.get(biomeAt(dx, estimate, dz)).id);
                if (b.find("ocean") != std::string_view::npos || b.find("river") != std::string_view::npos ||
                    b.find("beach") != std::string_view::npos || b == "stony_shore" || b.find("peaks") != std::string_view::npos)
                    continue;
                const int top = terrainTop(dx, dz);
                if (top <= kSeaLevel) continue;
                spawn_ = {dx + 0.5, double(top + 1), dz + 0.5};
                found = true;
            }
        if (found) break;
    }
}

int OverworldGenerator::terrainTop(int x, int z) const {
    Chunk chunk(ChunkPos(x >> 4, z >> 4));
    generateTerrain(chunk);
    const int lx = x & 15, lz = z & 15;
    for (int y = kWorldMaxY; y > kWorldMinY; --y)
        if (chunk.get(lx, y, lz) != 0) return chunk.get(lx, y, lz) == impl_->water ? kSeaLevel - 1 : y;
    return kWorldMinY;
}

OverworldGenerator::~OverworldGenerator() = default;

ClimatePoint OverworldGenerator::climateAt(int x, int y, int z) const {
    const Impl::Column c = impl_->column(x, z);
    return {c.t, c.h, c.c, c.e, c.w, float(Impl::depthAt(c, y))};
}

BiomeId OverworldGenerator::biomeAt(int x, int y, int z) const {
    const std::string_view name = pickOverworldBiome(climateAt(x, y, z));
    auto it = impl_->biomeIds.find(std::string(name));
    return it == impl_->biomeIds.end() ? biomes_.require("plains") : it->second;
}

int OverworldGenerator::estimatedSurface(int x, int z) const {
    const Impl::Column c = impl_->column(x, z);
    const double jag = c.jaggedness * (c.jaggedNoise > 0 ? c.jaggedNoise : c.jaggedNoise * 0.5);
    return int(std::floor((1.5 + c.offset + jag) * 128.0 - 64.0));
}

glm::dvec3 OverworldGenerator::spawnPoint() const { return spawn_; }

std::string OverworldGenerator::describeColumn(int x, int z) const {
    const Impl::Column c = impl_->column(x, z);
    std::string out = std::format("column {} {}: T {:.3f} H {:.3f} C {:.3f} E {:.3f} W {:.3f} pv {:.3f}\n", x, z, c.t, c.h,
                                  c.c, c.e, c.w, peaksAndValleys(c.w));
    out += std::format("  offset {:.4f} factor {:.3f} jaggedness {:.3f} jaggedNoise {:.3f} estimatedSurface {}\n",
                       c.offset, c.factor, c.jaggedness, c.jaggedNoise, estimatedSurface(x, z));
    const Impl& m = *impl_;
    for (int y = 320; y >= -64; y -= 8) {
        const double d = m.density(x, y, z, c);
        const double jag = c.jaggedness * (c.jaggedNoise > 0 ? c.jaggedNoise : c.jaggedNoise * 0.5);
        const double dd = (Impl::depthAt(c, y) + jag) * c.factor;
        const double layer = m.caveLayer.value(x, y * 8.0, z);
        const double cheese = m.caveCheese.value(x, y * 0.6666666666666666, z);
        out += std::format("  y {:4} depth {:6.3f} base {:6.3f} b3d {:6.3f} layer {:6.3f} cheese {:6.3f} entr {:6.3f} "
                           "sp2d {:6.3f} density {:6.3f} noodle {:6.3f} {}\n",
                           y, Impl::depthAt(c, y), 4.0 * (dd > 0 ? dd : dd * 0.25), m.base3d.value(x, y, z), layer,
                           cheese, m.entrances(x, y, z), m.spaghetti2d(x, y, z) + m.spaghettiRoughness(x, y, z), d,
                           m.noodleAt(x, y, z), d > 0 ? "#" : ".");
    }
    return out;
}

void OverworldGenerator::decorate(const TerrainNeighborhood&, DecorationResult&) const {}

namespace {

// Vanilla carvers, run for the target chunk from every start chunk within 8:
// "cave" (worm tunnels and rooms), "cave_extra_underground" (the same, deeper)
// and "canyon" (ravines). Each start chunk draws its own seeded random.
class Carvers {
public:
    Carvers(Chunk& chunk, int64_t seed, const BlockRegistry& reg, StateId air, StateId lava, StateId water,
            StateId grass, StateId dirt)
        : chunk_(chunk), seed_(seed), reg_(reg), air_(air), lava_(lava), water_(water), grass_(grass), dirt_(dirt) {
        replaceable_ = reg.tag("overworld_carver_replaceables").value_or(TagId(0));
    }

    void run() {
        const ChunkPos c = chunk_.pos;
        for (int cz = c.z - 8; cz <= c.z + 8; ++cz)
            for (int cx = c.x - 8; cx <= c.x + 8; ++cx) {
                cave(cx, cz, 0, 0.15f, -56, 180);
                cave(cx, cz, 1, 0.07f, -56, 47);
                canyon(cx, cz, 2);
            }
    }

private:
    Xoroshiro startRandom(int cx, int cz, uint64_t carver) const {
        return Xoroshiro(mixStafford13(uint64_t(seed_) ^ uint64_t(ChunkPos(cx, cz).key()) ^ (0xCA7E5ull + carver * 0x9E37ull)),
                         uint64_t(seed_) * 31 + 7 + carver);
    }

    // CaveWorldCarver.
    void cave(int cx, int cz, uint64_t carver, float probability, int minY, int maxY) {
        Xoroshiro r = startRandom(cx, cz, carver);
        if (r.nextFloat() > probability) return;
        constexpr int kRange = (4 * 2 - 1) * 16; // 112 blocks
        const int count = r.nextInt(r.nextInt(r.nextInt(15) + 1) + 1);
        for (int i = 0; i < count; ++i) {
            const double x = cx * 16 + r.nextInt(16);
            const double y = r.nextIntBetween(minY, maxY);
            const double z = cz * 16 + r.nextInt(16);
            const double hs = 0.7 + r.nextDouble() * 0.7, vs = 0.8 + r.nextDouble() * 0.5;
            const double floor = -1.0 + r.nextDouble() * 0.6;
            auto skip = [floor](double ex, double ey, double ez, int) { return ey <= floor || ex * ex + ey * ey + ez * ez >= 1.0; };
            int tunnels = 1;
            if (r.nextInt(4) == 0) { // a room
                const double yScale = 0.1 + r.nextDouble() * 0.8;
                const double w = 1.5 + 1.0 + r.nextFloat() * 6.0f;
                carveEllipsoid(x + 1.0, y, z, w, w * yScale, skip);
                tunnels += r.nextInt(4);
            }
            for (int t = 0; t < tunnels; ++t) {
                const float yaw = r.nextFloat() * float(kPi) * 2.0f;
                const float pitch = (r.nextFloat() - 0.5f) / 4.0f;
                float thickness = r.nextFloat() * 2.0f + r.nextFloat();
                if (r.nextInt(10) == 0) thickness *= r.nextFloat() * r.nextFloat() * 3.0f + 1.0f;
                const int length = kRange - r.nextInt(kRange / 4);
                tunnel(r.nextLong(), x, y, z, hs, vs, thickness, yaw, pitch, 0, length, skip);
            }
        }
    }

    template <class Skip>
    void tunnel(int64_t s, double x, double y, double z, double hs, double vs, float thickness, float yaw, float pitch,
                int start, int end, const Skip& skip) {
        Xoroshiro r(s);
        const int split = r.nextInt(end / 2) + end / 4;
        const bool steep = r.nextInt(6) == 0;
        float yawD = 0.0f, pitchD = 0.0f;
        for (int i = start; i < end; ++i) {
            const double w = 1.5 + std::sin(kPi * double(i) / double(end)) * thickness;
            const float cp = std::cos(pitch);
            x += std::cos(yaw) * cp;
            y += std::sin(pitch);
            z += std::sin(yaw) * cp;
            pitch *= steep ? 0.92f : 0.7f;
            pitch += pitchD * 0.1f;
            yaw += yawD * 0.1f;
            pitchD *= 0.9f;
            yawD *= 0.75f;
            pitchD += (r.nextFloat() - r.nextFloat()) * r.nextFloat() * 2.0f;
            yawD += (r.nextFloat() - r.nextFloat()) * r.nextFloat() * 4.0f;
            if (i == split && thickness > 1.0f) {
                tunnel(r.nextLong(), x, y, z, hs, vs, r.nextFloat() * 0.5f + 0.5f, yaw - float(kPi) / 2.0f, pitch / 3.0f,
                       i, end, skip);
                tunnel(r.nextLong(), x, y, z, hs, vs, r.nextFloat() * 0.5f + 0.5f, yaw + float(kPi) / 2.0f, pitch / 3.0f,
                       i, end, skip);
                return;
            }
            if (r.nextInt(4) == 0) continue;
            if (!canReach(x, z, i, end, thickness)) return;
            carveEllipsoid(x, y, z, w * hs, w * vs, skip);
        }
    }

    // CanyonWorldCarver: one long, tall, wobbling ellipsoid chain.
    void canyon(int cx, int cz, uint64_t carver) {
        Xoroshiro r = startRandom(cx, cz, carver);
        if (r.nextFloat() > 0.01f) return;
        double x = cx * 16 + r.nextInt(16);
        double y = r.nextIntBetween(10, 67);
        double z = cz * 16 + r.nextInt(16);
        float yaw = r.nextFloat() * float(kPi) * 2.0f;
        float pitch = -0.125f + r.nextFloat() * 0.25f;
        const float thickness = r.nextFloat() * 4.0f + r.nextFloat() * 2.0f; // trapezoid(0, 6, plateau 2)
        const int length = int(112 * (0.75 + r.nextDouble() * 0.25));
        Xoroshiro t(r.nextLong());
        // Width varies with height in smooth runs (initWidthFactors).
        std::array<float, kWorldHeight> width{};
        float f = 1.0f;
        for (int j = 0; j < kWorldHeight; ++j) {
            if (j == 0 || t.nextInt(3) == 0) f = 1.0f + t.nextFloat() * t.nextFloat();
            width[size_t(j)] = f * f;
        }
        auto skip = [&width](double ex, double ey, double ez, int by) {
            return (ex * ex + ez * ez) * width[size_t(std::clamp(by - kWorldMinY - 1, 0, kWorldHeight - 1))] + ey * ey / 6.0 >= 1.0;
        };
        float yawD = 0.0f, pitchD = 0.0f;
        for (int i = 0; i < length; ++i) {
            double h = 1.5 + std::sin(double(i) * kPi / double(length)) * thickness;
            const double v = h * 3.0 * (0.75 + t.nextFloat() * 0.25);
            h *= 0.75 + t.nextDouble() * 0.25;
            const float cp = std::cos(pitch);
            x += std::cos(yaw) * cp;
            y += std::sin(pitch);
            z += std::sin(yaw) * cp;
            pitch *= 0.7f;
            pitch += pitchD * 0.05f;
            yaw += yawD * 0.05f;
            pitchD *= 0.8f;
            yawD *= 0.5f;
            pitchD += (t.nextFloat() - t.nextFloat()) * t.nextFloat() * 2.0f;
            yawD += (t.nextFloat() - t.nextFloat()) * t.nextFloat() * 4.0f;
            if (t.nextInt(4) == 0) continue;
            if (!canReach(x, z, i, length, thickness)) return;
            carveEllipsoid(x, y, z, h, v, skip);
        }
    }

    bool touchesWater(int lx, int y, int lz) const {
        if (chunk_.get(lx, y + 1, lz) == water_) return true;
        if (lx > 0 && chunk_.get(lx - 1, y, lz) == water_) return true;
        if (lx < 15 && chunk_.get(lx + 1, y, lz) == water_) return true;
        if (lz > 0 && chunk_.get(lx, y, lz - 1) == water_) return true;
        return lz < 15 && chunk_.get(lx, y, lz + 1) == water_;
    }

    bool canReach(double x, double z, int i, int end, float thickness) const {
        const double dx = x - (chunk_.pos.minBlockX() + 8.0), dz = z - (chunk_.pos.minBlockZ() + 8.0);
        const double remaining = end - i, reach = thickness + 2.0 + 16.0;
        return dx * dx + dz * dz - remaining * remaining <= reach * reach;
    }

    template <class Skip>
    void carveEllipsoid(double x, double y, double z, double rh, double rv, const Skip& skip) {
        const int x0 = chunk_.pos.minBlockX(), z0 = chunk_.pos.minBlockZ();
        const double reach = 16.0 + rh * 2.0;
        if (std::abs(x - (x0 + 8.0)) > reach || std::abs(z - (z0 + 8.0)) > reach) return;
        const int lx0 = std::max(int(std::floor(x - rh)) - x0 - 1, 0), lx1 = std::min(int(std::floor(x + rh)) - x0, 15);
        const int lz0 = std::max(int(std::floor(z - rh)) - z0 - 1, 0), lz1 = std::min(int(std::floor(z + rh)) - z0, 15);
        const int yLo = std::max(int(std::floor(y - rv)) - 1, kWorldMinY + 1);
        const int yHi = std::min(int(std::floor(y + rv)) + 1, kWorldMaxY - 7);
        for (int lx = lx0; lx <= lx1; ++lx) {
            const double ex = (x0 + lx + 0.5 - x) / rh;
            for (int lz = lz0; lz <= lz1; ++lz) {
                const double ez = (z0 + lz + 0.5 - z) / rh;
                if (ex * ex + ez * ez >= 1.0) continue;
                bool reachedSurface = false;
                for (int by = yHi; by > yLo; --by) {
                    const double ey = (by - 0.5 - y) / rv;
                    if (skip(ex, ey, ez, by)) continue;
                    const StateId cur = chunk_.get(lx, by, lz);
                    if (cur == grass_) reachedSurface = true;
                    if (cur == air_ || cur == lava_ || !reg_.hasTag(cur, replaceable_)) continue;
                    // Aquifer stand-in: never open a block next to water (sea floors, lake
                    // beds, flooded caves), which would leave water walls and ceilings.
                    if (cur == water_ || touchesWater(lx, by, lz)) continue;
                    chunk_.set(lx, by, lz, by <= -56 ? lava_ : air_);
                    if (reachedSurface && chunk_.get(lx, by - 1, lz) == dirt_) chunk_.set(lx, by - 1, lz, grass_);
                }
            }
        }
    }

    Chunk& chunk_;
    int64_t seed_;
    const BlockRegistry& reg_;
    StateId air_, lava_, water_, grass_, dirt_;
    TagId replaceable_ = 0;
};

} // namespace

void OverworldGenerator::generateTerrain(Chunk& chunk) const {
    const Impl& m = *impl_;
    const int x0 = chunk.pos.minBlockX(), z0 = chunk.pos.minBlockZ();

    // 1. Climate and splines at the 5x5 corner columns (also the biome quart grid).
    std::array<Impl::Column, kCornersXZ * kCornersXZ> cols;
    for (int j = 0; j < kCornersXZ; ++j)
        for (int i = 0; i < kCornersXZ; ++i) cols[size_t(j * kCornersXZ + i)] = m.column(x0 + i * kCellW, z0 + j * kCellW);
    auto colAt = [&](int i, int j) -> const Impl::Column& { return cols[size_t(j * kCornersXZ + i)]; };

    // 2. Biomes per 4x4x4 cell.
    for (int si = 0; si < kSectionCount; ++si)
        for (int qy = 0; qy < 4; ++qy)
            for (int qz = 0; qz < 4; ++qz)
                for (int qx = 0; qx < 4; ++qx) {
                    const Impl::Column& c = colAt(qx, qz);
                    const int y = sectionMinY(si) + qy * 4;
                    const ClimatePoint cp{c.t, c.h, c.c, c.e, c.w, float(Impl::depthAt(c, y))};
                    auto it = m.biomeIds.find(std::string(pickOverworldBiome(cp)));
                    chunk.sections[size_t(si)].biomes[size_t((qy << 4) | (qz << 2) | qx)] =
                        it == m.biomeIds.end() ? BiomeId(0) : it->second;
                }

    // 3. Density at cell corners, then trilinear interpolation per block.
    static thread_local std::vector<float> dens;
    static thread_local std::vector<Impl::Noodle> nood;
    dens.assign(size_t(kCornersXZ * kCornersXZ * kCornersY), 0.0f);
    nood.assign(dens.size(), Impl::Noodle{-1.0f, 0.0f, 0.0f, 0.0f});
    auto cidx = [](int i, int j, int k) { return size_t((k * kCornersXZ + j) * kCornersXZ + i); };
    for (int j = 0; j < kCornersXZ; ++j)
        for (int i = 0; i < kCornersXZ; ++i) {
            const Impl::Column& c = colAt(i, j);
            const int x = x0 + i * kCellW, z = z0 + j * kCellW;
            for (int k = 0; k < kCornersY; ++k) {
                const int y = kWorldMinY + k * kCellH;
                dens[cidx(i, j, k)] = float(m.density(x, y, z, c));
                nood[cidx(i, j, k)] = m.noodleInputs(x, y, z);
            }
        }

    // Columns: topmost solid block (for aquifers and surface rules).
    std::array<int, 256> top;
    top.fill(kWorldMinY - 1);
    std::array<int, 256> estimate;
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) {
            const float fx = float(x % kCellW) / kCellW, fz = float(z % kCellW) / kCellW;
            const int i = x / kCellW, j = z / kCellW;
            auto est = [&](int a, int b) {
                const Impl::Column& c = colAt(a, b);
                const double jag = c.jaggedness * (c.jaggedNoise > 0 ? c.jaggedNoise : c.jaggedNoise * 0.5);
                return (1.5 + c.offset + jag) * 128.0 - 64.0;
            };
            const double e0 = lerpd(fx, est(i, j), est(i + 1, j)), e1 = lerpd(fx, est(i, j + 1), est(i + 1, j + 1));
            estimate[size_t(z * 16 + x)] = int(lerpd(fz, e0, e1));
        }

    for (int k = 0; k < kCornersY - 1; ++k)
        for (int j = 0; j < 4; ++j)
            for (int i = 0; i < 4; ++i) {
                const float d000 = dens[cidx(i, j, k)], d100 = dens[cidx(i + 1, j, k)];
                const float d010 = dens[cidx(i, j + 1, k)], d110 = dens[cidx(i + 1, j + 1, k)];
                const float d001 = dens[cidx(i, j, k + 1)], d101 = dens[cidx(i + 1, j, k + 1)];
                const float d011 = dens[cidx(i, j + 1, k + 1)], d111 = dens[cidx(i + 1, j + 1, k + 1)];
                const Impl::Noodle* nc[8] = {&nood[cidx(i, j, k)],         &nood[cidx(i + 1, j, k)],
                                             &nood[cidx(i, j + 1, k)],     &nood[cidx(i + 1, j + 1, k)],
                                             &nood[cidx(i, j, k + 1)],     &nood[cidx(i + 1, j, k + 1)],
                                             &nood[cidx(i, j + 1, k + 1)], &nood[cidx(i + 1, j + 1, k + 1)]};
                bool noodleCell = false; // any corner selects noodles
                for (const Impl::Noodle* n : nc) noodleCell |= n->select >= 0.0f;
                for (int dy = 0; dy < kCellH; ++dy) {
                    const float ty = float(dy) / kCellH;
                    const int y = kWorldMinY + k * kCellH + dy;
                    for (int dz = 0; dz < kCellW; ++dz) {
                        const float tz = float(dz) / kCellW;
                        for (int dx = 0; dx < kCellW; ++dx) {
                            const float tx = float(dx) / kCellW;
                            auto tri = [&](float a, float b, float c, float d, float e, float f, float g, float h) {
                                const float x00 = a + tx * (b - a), x10 = c + tx * (d - c);
                                const float x01 = e + tx * (f - e), x11 = g + tx * (h - g);
                                const float z0v = x00 + tz * (x10 - x00), z1v = x01 + tz * (x11 - x01);
                                return z0v + ty * (z1v - z0v);
                            };
                            const float dv = tri(d000, d100, d010, d110, d001, d101, d011, d111);
                            if (dv <= 0.0f) continue;
                            if (noodleCell) {
                                auto field = [&](float Impl::Noodle::*f) {
                                    return tri(nc[0]->*f, nc[1]->*f, nc[2]->*f, nc[3]->*f, nc[4]->*f, nc[5]->*f,
                                               nc[6]->*f, nc[7]->*f);
                                };
                                const Impl::Noodle n{field(&Impl::Noodle::select), field(&Impl::Noodle::thickness),
                                                     field(&Impl::Noodle::ridgeA), field(&Impl::Noodle::ridgeB)};
                                if (Impl::noodleValue(n) <= 0.0) continue;
                            }
                            const int lx = i * kCellW + dx, lz = j * kCellW + dz;
                            const int wx = x0 + lx, wz = z0 + lz;
                            StateId s = m.stone;
                            if (y < 8 && (y < 0 || blockRandom(seed_, wx, y, wz, 1) < double(8 - y) / 8.0)) s = m.deepslate;
                            chunk.set(lx, y, lz, s);
                            top[size_t(lz * 16 + lx)] = std::max(top[size_t(lz * 16 + lx)], y);
                        }
                    }
                }
            }

    // 4. Fluids (an aquifer stand-in). Below sea level, air down to a flood line 6
    //    blocks under the spline surface becomes water: oceans, rivers and flooded
    //    near-surface caves (the 3D noise lowers sea floors only a few blocks below
    //    the spline surface, since the factor is large there). Air just under the
    //    flood line becomes stone, a barrier like vanilla's between aquifers of
    //    different levels, so deeper caves stay dry; lava fills the bottom of the
    //    world. The flood line is a pure function of position, so neighboring
    //    chunks agree on it.
    constexpr int kBarrier = 3;
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) {
            const int flood = estimate[size_t(z * 16 + x)] - 6;
            for (int y = kSeaLevel; y >= kWorldMinY; --y) {
                if (chunk.get(x, y, z) != 0) continue;
                if (y >= flood) chunk.set(x, y, z, m.water);
                else if (y < -54) chunk.set(x, y, z, m.lava);
                else if (y >= flood - kBarrier) chunk.set(x, y, z, y < 8 ? m.deepslate : m.stone);
            }
        }

    // 5. Surface rules.
    auto biomeOf = [&](int x, int y, int z) -> const Biome& {
        const int si = sectionIndexOfY(std::clamp(y, kWorldMinY, kWorldMaxY));
        const int qy = ((y - kWorldMinY) & 15) >> 2;
        return biomes_.get(chunk.sections[size_t(si)].biomes[size_t((qy << 4) | ((z >> 2) << 2) | (x >> 2))]);
    };
    auto topAt = [&](int x, int z) { return top[size_t(std::clamp(z, 0, 15) * 16 + std::clamp(x, 0, 15))]; };
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) {
            const int wx = x0 + x, wz = z0 + z;
            const int est = estimate[size_t(z * 16 + x)];
            const double sn = m.surface.value(wx, 0.0, wz);
            const int surfaceDepth = int(sn * 2.75 + 3.0 + blockRandom(seed_, wx, 0, wz, 2) * 0.25);
            const bool steep = std::abs(topAt(x + 1, z) - topAt(x - 1, z)) >= 4 || std::abs(topAt(x, z + 1) - topAt(x, z - 1)) >= 4;
            int depth = -1;            // solid blocks since the last open block above
            bool underwater = false;   // the open block above that run was fluid
            int runTop = kWorldMaxY;
            for (int y = std::min(kWorldMaxY, std::max(top[size_t(z * 16 + x)], kSeaLevel) + 1); y >= kWorldMinY; --y) {
                const StateId cur = chunk.get(x, y, z);
                if (cur != m.stone && cur != m.deepslate) {
                    depth = -1;
                    underwater = cur == m.water;
                    continue;
                }
                ++depth;
                if (depth == 0) runTop = y;
                if (y < est - 16 || depth > surfaceDepth + 6) continue; // not a near-surface run
                const Biome& b = biomeOf(x, runTop, z);
                const std::string& surf = b.surface;
                StateId put = 0;
                auto beachy = [&](StateId topBlock, StateId under) {
                    if (depth <= surfaceDepth) put = topBlock;
                    else if (depth <= surfaceDepth + 3 && runTop < kSeaLevel + 6) put = under;
                };
                if (surf == "desert") {
                    if (depth <= surfaceDepth) put = m.sand;
                    else if (depth <= surfaceDepth + 4) put = m.sandstone;
                } else if (surf == "beach") {
                    beachy(m.sand, m.sandstone);
                } else if (surf == "ocean" || surf == "deep_ocean" || surf == "frozen_ocean") {
                    if (depth <= surfaceDepth) put = (surf == "ocean" && sn > -0.2) ? m.sand : m.gravel;
                } else if (surf == "warm_ocean") {
                    if (depth <= surfaceDepth) put = m.sand;
                } else if (surf == "river") {
                    if (depth <= surfaceDepth)
                        put = underwater ? (sn > 0.0 ? m.sand : m.gravel) : (depth == 0 ? m.grass : m.dirt);
                } else if (surf == "stony_shore" || surf == "stony_peaks") {
                    if (surf == "stony_peaks" && depth == 0 && m.surfaceSecondary.value(wx, 0, wz) > 0.4) put = m.calcite;
                } else if (surf == "jagged_peaks" || surf == "frozen_peaks" || surf == "snowy_slopes") {
                    if (depth == 0 && !steep) {
                        put = m.snowBlock;
                        if (surf == "frozen_peaks" && m.surfaceSecondary.value(wx, 0, wz) > 0.5) put = m.packedIce;
                    } else if (depth == 1 && surf == "snowy_slopes" && !steep) {
                        put = m.snowBlock;
                    }
                } else if (surf == "gravelly_hills") {
                    if (depth <= surfaceDepth && m.gravelPatches.value(wx, 0, wz) > -0.1) put = m.gravel;
                    else if (depth == 0 && !steep) put = m.grass;
                    else if (depth <= surfaceDepth && !steep) put = m.dirt;
                } else if (surf == "badlands" || (surf == "wooded_badlands" && runTop < 97)) {
                    if (depth == 0 && runTop < 80 && !steep) put = underwater ? m.redSand : m.redSand;
                    else if (depth <= surfaceDepth + 12) put = m.badlandsBands[size_t(((y % 64) + 64) % 64)];
                } else {
                    // Grass family (plains, forests, taiga, swamp, meadow, grove, hills, mushroom...).
                    const bool rocky = steep && (surf == "meadow" || surf == "windswept_hills" || surf == "grove" ||
                                                 surf == "windswept_savanna" || surf == "old_growth_taiga");
                    if (rocky) continue;
                    if (depth == 0) {
                        if (underwater) put = surf == "swamp" && sn > 0.1 ? m.clay : m.dirt;
                        else if (surf == "mushroom") put = m.mycelium;
                        else if (surf == "old_growth_taiga" && sn > 0.21) put = m.podzol;
                        else if (surf == "old_growth_taiga" && sn < -0.1) put = m.coarseDirt;
                        else if (surf == "windswept_savanna" && sn > 0.3) put = m.coarseDirt;
                        else if (surf == "wooded_badlands" && sn > 0.1) put = m.coarseDirt;
                        else put = m.grass;
                    } else if (depth <= surfaceDepth) {
                        put = m.dirt;
                    }
                }
                if (put) chunk.set(x, y, z, put);
            }
        }

    // 6. Bedrock floor: solid at y=-64, thinning out to y=-60.
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x)
            for (int y = kWorldMinY; y <= kWorldMinY + 4; ++y)
                if (y == kWorldMinY || blockRandom(seed_, x0 + x, y, z0 + z, 3) < double(kWorldMinY + 5 - y) / 5.0)
                    chunk.set(x, y, z, m.bedrock);

    // 7. Carvers.
    Carvers(chunk, seed_, blocks_, m.air, m.lava, m.water, m.grass, m.dirt).run();
}

} // namespace mc
