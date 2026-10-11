#include <cmath>
#include <filesystem>
#include <memory>
#include <map>
#include <set>
#include <tuple>

#include <gtest/gtest.h>

#include "core/JobSystem.h"
#include "core/Noise.h"
#include "resources/Atlas.h"
#include "resources/BlockModels.h"
#include "resources/ResourcePack.h"
#include "world/biome/Biome.h"
#include "world/block/BlockRegistry.h"
#include "world/chunk/ChunkManager.h"
#include "world/gen/BiomeLayout.h"
#include "world/gen/OverworldGenerator.h"
#include "world/gen/TerrainSplines.h"
#include "world/gen/feature/BiomeDecorator.h"
#include "world/gen/feature/DecorationRegion.h"
#include "world/storage/ChunkStorage.h"

using namespace mc;

namespace {

struct WorldgenFixture : ::testing::Test {
    static void SetUpTestSuite() {
        pack = std::make_unique<ResourcePack>(MC_DEFAULT_PACK_DIR);
        atlas = std::make_unique<Atlas>(*buildAtlas(*pack, "blocks", 0));
        store = std::make_unique<ModelStore>(*pack);
        baker = std::make_unique<ModelBaker>(*store, *atlas);
        models = std::make_unique<BlockModels>();
        models->load(*pack, *store, *baker);
        registry = std::make_unique<BlockRegistry>();
        registry->build(*models, *atlas, MC_DEFAULT_DATA_DIR, &registryError);
        biomes = std::make_unique<BiomeRegistry>();
        std::string err;
        ASSERT_TRUE(biomes->load(MC_DEFAULT_DATA_DIR, MC_DEFAULT_PACK_DIR, &err)) << err;
    }
    static void TearDownTestSuite() {
        biomes.reset();
        registry.reset();
        models.reset();
        baker.reset();
        store.reset();
        atlas.reset();
        pack.reset();
    }

    static uint64_t hashChunk(const Chunk& c, uint64_t h = 1469598103934665603ull) {
        for (int y = kWorldMinY; y <= kWorldMaxY; ++y)
            for (int z = 0; z < 16; ++z)
                for (int x = 0; x < 16; ++x) h = (h ^ c.get(x, y, z)) * 1099511628211ull;
        for (const Section& s : c.sections)
            for (uint8_t b : s.biomes) h = (h ^ b) * 1099511628211ull;
        return h;
    }

    // Runs the pipeline to a ready view and hashes the requested block-final chunks.
    static std::vector<uint64_t> pipelineHashes(std::shared_ptr<const ChunkGenerator> gen, int threads, ChunkPos center,
                                                int rd, const std::vector<ChunkPos>& wanted) {
        JobSystem jobs(threads);
        ChunkManager cm(*registry, gen, jobs);
        cm.setView(center, rd);
        for (int i = 0; i < 2000 && !cm.viewReady(); ++i) {
            cm.update();
            jobs.waitIdle();
        }
        EXPECT_TRUE(cm.viewReady());
        std::vector<uint64_t> out;
        for (ChunkPos p : wanted) {
            const Chunk* c = cm.chunk(p);
            EXPECT_NE(c, nullptr) << p.x << "," << p.z;
            out.push_back(c ? hashChunk(*c) : 0);
        }
        return out;
    }

    static inline std::unique_ptr<ResourcePack> pack;
    static inline std::unique_ptr<Atlas> atlas;
    static inline std::unique_ptr<ModelStore> store;
    static inline std::unique_ptr<ModelBaker> baker;
    static inline std::unique_ptr<BlockModels> models;
    static inline std::unique_ptr<BlockRegistry> registry;
    static inline std::unique_ptr<BiomeRegistry> biomes;
    static inline std::string registryError;
};

} // namespace

TEST(Noise, NormalNoiseIsDeterministicAndBounded) {
    const Xoroshiro seed(42);
    const NormalNoise a(seed, "test", -7, {1, 1, 1});
    const NormalNoise b(seed, "test", -7, {1, 1, 1});
    const NormalNoise other(seed, "other", -7, {1, 1, 1});
    double sum = 0, sumSq = 0;
    int differs = 0;
    for (int i = 0; i < 2000; ++i) {
        const double x = i * 13.7, z = i * -7.3;
        const double v = a.value(x, 0, z);
        EXPECT_EQ(v, b.value(x, 0, z));
        differs += v != other.value(x, 0, z);
        EXPECT_LT(std::abs(v), 2.0);
        sum += v;
        sumSq += v * v;
    }
    EXPECT_GT(differs, 1990); // a different name gives an unrelated field
    const double mean = sum / 2000, sd = std::sqrt(sumSq / 2000 - mean * mean);
    EXPECT_LT(std::abs(mean), 0.1);
    EXPECT_GT(sd, 0.15); // normalized noises have a standard deviation around 1/3
    EXPECT_LT(sd, 0.6);
}

TEST(Noise, CubicSplineInterpolates) {
    using I = CubicSpline::Input;
    const auto s = CubicSpline::Builder(I::Continents).add(-1.0f, 0.0f).add(1.0f, 1.0f).build();
    EXPECT_FLOAT_EQ(s->eval({-1.0f}), 0.0f);
    EXPECT_FLOAT_EQ(s->eval({1.0f}), 1.0f);
    EXPECT_NEAR(s->eval({0.0f}), 0.5f, 1e-6);
    EXPECT_FLOAT_EQ(s->eval({-3.0f}), 0.0f); // zero derivative: flat outside the points
    EXPECT_FLOAT_EQ(s->eval({3.0f}), 1.0f);
    // Nested splines: the value at a point may itself be a spline of another input.
    const auto inner = CubicSpline::Builder(I::Erosion).add(0.0f, 2.0f).add(1.0f, 4.0f).build();
    const auto outer = CubicSpline::Builder(I::Continents).add(0.0f, inner).add(1.0f, 10.0f).build();
    SplinePoint p;
    p.continents = 0.0f;
    p.erosion = 1.0f;
    EXPECT_FLOAT_EQ(outer->eval(p), 4.0f);
}

TEST(BiomeLayout, CaveBiomesUseNearestPointRule) {
    ClimatePoint p{0.0f, 0.5f, 0.2f, 0.0f, 0.3f, 0.0f};
    EXPECT_NE(pickOverworldBiome(p), "lush_caves"); // at the surface
    p.depth = 0.55f;                                // deep: (0.7 - 0.5)^2 beats the 0.45^2 depth distance
    EXPECT_EQ(pickOverworldBiome(p), "lush_caves");
    p.humidity = 0.0f; // too dry for lush caves, not continental enough for dripstone
    EXPECT_NE(pickOverworldBiome(p), "lush_caves");
    EXPECT_NE(pickOverworldBiome(p), "dripstone_caves");
    p.continentalness = 0.9f;
    EXPECT_EQ(pickOverworldBiome(p), "dripstone_caves");
}

TEST_F(WorldgenFixture, TagsResolve) {
    EXPECT_EQ(registryError.find("tag"), std::string::npos) << registryError;
    const BlockTags& tags = registry->tags();
    EXPECT_GE(tags.size(), 60u);
    auto has = [&](const char* tag, const char* block) {
        const auto t = registry->tag(tag);
        EXPECT_TRUE(t.has_value()) << tag;
        return t && registry->hasTag(registry->defaultState(block), *t);
    };
    EXPECT_TRUE(has("logs", "oak_log"));
    EXPECT_TRUE(has("logs", "stripped_cherry_wood"));
    EXPECT_TRUE(has("minecraft:logs", "warped_stem"));
    EXPECT_FALSE(has("logs", "oak_planks"));
    EXPECT_TRUE(has("#minecraft:base_stone_overworld", "deepslate"));
    EXPECT_TRUE(has("terracotta", "red_terracotta"));
    EXPECT_FALSE(has("terracotta", "red_glazed_terracotta"));
    EXPECT_TRUE(has("standing_signs", "oak_sign"));
    EXPECT_FALSE(has("standing_signs", "oak_wall_sign"));
    EXPECT_FALSE(has("standing_signs", "oak_hanging_sign"));
    EXPECT_TRUE(has("wall_signs", "oak_wall_sign"));
    EXPECT_FALSE(has("wall_signs", "oak_wall_hanging_sign"));
    EXPECT_TRUE(has("overworld_carver_replaceables", "grass_block")); // through #dirt
    EXPECT_TRUE(has("flowers", "poppy"));                             // through #small_flowers
    EXPECT_FALSE(has("saplings", "bamboo_sapling"));
    EXPECT_EQ(tags.blocks(*registry->tag("wool")).size(), 16u);
    EXPECT_FALSE(registry->tag("no_such_tag").has_value());
}

TEST_F(WorldgenFixture, TerrainIsAPureFunction) {
    const OverworldGenerator gen(*registry, *biomes, 1234);
    Chunk a({5, -3}), b({5, -3});
    gen.generateTerrain(a);
    gen.generateTerrain(b);
    EXPECT_EQ(hashChunk(a), hashChunk(b));
    // Bedrock floor, and stone somewhere below the surface.
    const StateId bedrock = registry->defaultState("bedrock");
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) EXPECT_EQ(a.get(x, kWorldMinY, z), bedrock);
    // Another seed gives another world.
    const OverworldGenerator other(*registry, *biomes, 4321);
    Chunk c({5, -3});
    other.generateTerrain(c);
    EXPECT_NE(hashChunk(a), hashChunk(c));
}

std::shared_ptr<BiomeDecorator> loadDecorator(const BlockRegistry& reg, const BiomeRegistry& biomes);

TEST_F(WorldgenFixture, OverworldPipelineIsDeterministic) {
    // Decorated: features read neighbor snapshots and MERGE combines nine sources.
    auto gen = std::make_shared<OverworldGenerator>(*registry, *biomes, 99, loadDecorator(*registry, *biomes));
    const std::vector<ChunkPos> wanted = {{0, 0}, {2, -2}, {4, 4}, {-3, 1}};
    // Thread counts: inline, one worker, many workers.
    const auto inlineRun = pipelineHashes(gen, 0, {0, 0}, 2, wanted);
    EXPECT_EQ(inlineRun, pipelineHashes(gen, 1, {0, 0}, 2, wanted));
    EXPECT_EQ(inlineRun, pipelineHashes(gen, 22, {0, 0}, 2, wanted));
    // Load order: (4,4) reached from a different view center ends up identical.
    const auto shifted = pipelineHashes(gen, 8, {8, 8}, 2, {{4, 4}});
    EXPECT_EQ(inlineRun[2], shifted[0]);
}

TEST_F(WorldgenFixture, SpawnIsOnLand) {
    for (int64_t seed : {1, 2, 12345}) {
        const OverworldGenerator gen(*registry, *biomes, seed);
        const glm::dvec3 sp = gen.spawnPoint();
        EXPECT_GT(sp.y, OverworldGenerator::kSeaLevel + 1) << seed;
        const int x = int(std::floor(sp.x)), z = int(std::floor(sp.z));
        EXPECT_EQ(gen.terrainTop(x, z) + 1, int(sp.y)) << seed;
        const std::string_view b = shortId(biomes->get(gen.biomeAt(x, int(sp.y), z)).id);
        EXPECT_EQ(b.find("ocean"), std::string_view::npos) << seed << " " << b;
        EXPECT_EQ(b.find("river"), std::string_view::npos) << seed << " " << b;
    }
}

// A 32768-block square per seed: the continentalness noise varies over ~2000
// blocks, so smaller samples swing between mostly-ocean and mostly-land.
TEST_F(WorldgenFixture, BiomeDistribution) {
    for (int64_t seed : {1, 2, 3}) {
        const OverworldGenerator gen(*registry, *biomes, seed);
        std::set<BiomeId> seen;
        int ocean = 0, total = 0, peaks = 0;
        for (int z = -16384; z < 16384; z += 128)
            for (int x = -16384; x < 16384; x += 128) {
                const BiomeId b = gen.biomeAt(x, gen.estimatedSurface(x, z), z);
                seen.insert(b);
                const std::string_view id = shortId(biomes->get(b).id);
                ocean += id.find("ocean") != std::string_view::npos;
                peaks += id.find("peaks") != std::string_view::npos;
                ++total;
            }
        const double oceanShare = double(ocean) / total;
        EXPECT_GE(seen.size(), 30u) << "seed " << seed;
        EXPECT_GT(oceanShare, 0.25) << "seed " << seed;
        EXPECT_LT(oceanShare, 0.40) << "seed " << seed;
        EXPECT_GT(peaks, 0) << "seed " << seed;
    }
}

namespace {

// Flat stone below y 0; DECORATE emits scripted writes per source chunk.
class ScriptedGenerator final : public ChunkGenerator {
public:
    ScriptedGenerator(const BlockRegistry& reg) : stone_(reg.defaultState("stone")) {}
    std::string_view type() const override { return "scripted"; }
    void generateTerrain(Chunk& c) const override {
        for (int y = kWorldMinY; y < 0; ++y)
            for (int z = 0; z < 16; ++z)
                for (int x = 0; x < 16; ++x) c.set(x, y, z, stone_);
    }
    void decorate(const TerrainNeighborhood& t, DecorationResult& out) const override {
        for (const auto& [src, w] : writes)
            if (src == t.center) out.writes.push_back(w);
    }
    glm::dvec3 spawnPoint() const override { return {0, 1, 0}; }
    std::vector<std::pair<ChunkPos, BlockWrite>> writes;

private:
    StateId stone_;
};

} // namespace

TEST_F(WorldgenFixture, MergeAppliesWritePolicies) {
    auto gen = std::make_shared<ScriptedGenerator>(*registry);
    const StateId glass = registry->defaultState("glass"), log = registry->defaultState("oak_log");
    const StateId leaves = registry->defaultState("oak_leaves"), gold = registry->defaultState("gold_block");
    auto w = [](BlockPos p, StateId s, StateId expect, uint16_t feature, uint8_t policy) {
        return BlockWrite{p, s, expect, feature, 9, policy};
    };
    // Same block from two sources: the lower feature index lands first, and the later
    // if-unchanged write sees a changed block and backs off.
    gen->writes.push_back({{0, 0}, w({15, 1, 8}, glass, 0, 1, kWriteIfUnchanged)});
    gen->writes.push_back({{1, 0}, w({15, 1, 8}, log, 0, 0, kWriteIfUnchanged)});
    // A tree write goes over another source's leaves.
    gen->writes.push_back({{0, 0}, w({16, 1, 8}, leaves, 0, 0, kWriteTree)});
    gen->writes.push_back({{1, 0}, w({16, 1, 8}, log, 0, 1, kWriteTree)});
    // ...but an if-unchanged write does not, and a forced one always does.
    gen->writes.push_back({{0, 0}, w({17, 1, 8}, leaves, 0, 0, kWriteIfUnchanged)});
    gen->writes.push_back({{1, 0}, w({17, 1, 8}, glass, 0, 1, kWriteIfUnchanged)});
    gen->writes.push_back({{0, 0}, w({18, 1, 8}, leaves, 0, 0, kWriteIfUnchanged)});
    gen->writes.push_back({{1, 0}, w({18, 1, 8}, gold, 0, 1, kWriteForce)});
    // Writes stay deterministic when the same feature writes from two sources (source order).
    gen->writes.push_back({{1, 0}, w({19, 1, 8}, glass, 0, 2, kWriteIfUnchanged)});
    gen->writes.push_back({{0, 0}, w({19, 1, 8}, gold, 0, 2, kWriteIfUnchanged)});
    for (int threads : {0, 4}) {
        JobSystem jobs(threads);
        ChunkManager cm(*registry, gen, jobs);
        cm.setView({0, 0}, 2);
        for (int i = 0; i < 2000 && !cm.viewReady(); ++i) {
            cm.update();
            jobs.waitIdle();
        }
        ASSERT_TRUE(cm.viewReady());
        EXPECT_EQ(cm.getBlock({15, 1, 8}), log);
        EXPECT_EQ(cm.getBlock({16, 1, 8}), log);
        EXPECT_EQ(cm.getBlock({17, 1, 8}), leaves);
        EXPECT_EQ(cm.getBlock({18, 1, 8}), gold);
        EXPECT_EQ(cm.getBlock({19, 1, 8}), gold); // source (0,0) sorts before (1,0)
    }
}

TEST_F(WorldgenFixture, DecorationRegionTracksHeightmapsAndOverlay) {
    Chunk c({0, 0});
    const StateId stone = registry->defaultState("stone"), water = registry->defaultState("water");
    const StateId grass = registry->defaultState("short_grass"), leaves = registry->defaultState("oak_leaves");
    for (int y = kWorldMinY; y <= 60; ++y) c.set(3, y, 3, stone);
    for (int y = 61; y <= 63; ++y) c.set(3, y, 3, water);
    TerrainNeighborhood n;
    n.center = {0, 0};
    n.chunks[4] = std::make_shared<const Chunk>(c);
    DecorationResult out;
    DecorationRegion r(*registry, *biomes, n, 7, out);
    EXPECT_EQ(r.height(Heightmap::OceanFloor, 3, 3), 61);
    EXPECT_EQ(r.height(Heightmap::WorldSurface, 3, 3), 64);
    EXPECT_EQ(r.height(Heightmap::MotionBlocking, 3, 3), 64);
    EXPECT_EQ(r.height(Heightmap::WorldSurface, 4, 4), kWorldMinY);
    // Writes are visible to later reads and keep the cached heightmaps current.
    r.setFeature(9, 5);
    EXPECT_TRUE(r.setBlock({3, 70, 3}, leaves, kWriteTree));
    EXPECT_EQ(r.getBlock({3, 70, 3}), leaves);
    EXPECT_EQ(r.height(Heightmap::MotionBlocking, 3, 3), 71);
    EXPECT_EQ(r.height(Heightmap::MotionBlockingNoLeaves, 3, 3), 64);
    EXPECT_TRUE(r.setBlock({4, 0, 4}, grass));
    EXPECT_EQ(r.height(Heightmap::WorldSurface, 4, 4), 1);
    EXPECT_EQ(r.height(Heightmap::OceanFloor, 4, 4), kWorldMinY); // plants do not block motion
    // Out of the 3x3 (only the center exists here) nothing is written.
    EXPECT_FALSE(r.setBlock({20, 0, 0}, stone));
    ASSERT_EQ(out.writes.size(), 2u);
    EXPECT_EQ(out.writes[0].expect, BlockRegistry::kAirState);
    EXPECT_EQ(out.writes[0].feature, 5);
    EXPECT_EQ(out.writes[0].policy, kWriteTree);
}

std::shared_ptr<BiomeDecorator> loadDecorator(const BlockRegistry& reg, const BiomeRegistry& biomes) {
    auto d = std::make_shared<BiomeDecorator>();
    std::string err;
    d->load(MC_DEFAULT_DATA_DIR, reg, biomes, &err);
    return d;
}

// Per-chunk ore counts over a 9x9-chunk area, against bands derived from
// vanilla's placements (blob sizes, counts, height ranges, air discards).
TEST_F(WorldgenFixture, OreCountsPerChunk) {
    auto gen = std::make_shared<OverworldGenerator>(*registry, *biomes, 4242, loadDecorator(*registry, *biomes));
    JobSystem jobs(8);
    ChunkManager cm(*registry, gen, jobs);
    cm.setLightFunction([](const Neighborhood&, Chunk&) {});
    cm.setView({0, 0}, 4);
    for (int i = 0; i < 4000 && !cm.viewReady(); ++i) {
        cm.update();
        jobs.waitIdle();
    }
    ASSERT_TRUE(cm.viewReady());
    std::map<std::string, double> count;
    std::map<std::string, int> maxY, minY;
    int chunks = 0;
    for (int cz = -4; cz <= 4; ++cz)
        for (int cx = -4; cx <= 4; ++cx) {
            const Chunk* c = cm.chunk({cx, cz});
            ASSERT_NE(c, nullptr);
            ++chunks;
            for (int y = kWorldMinY; y <= kWorldMaxY; ++y)
                for (int z = 0; z < 16; ++z)
                    for (int x = 0; x < 16; ++x) {
                        std::string id(shortId(registry->blockOf(c->get(x, y, z)).id));
                        if (id.starts_with("deepslate_") && id.ends_with("_ore")) id.erase(0, 10);
                        if (!id.ends_with("_ore")) continue;
                        count[id] += 1;
                        maxY[id] = std::max(maxY.contains(id) ? maxY[id] : -1000, y);
                        minY[id] = std::min(minY.contains(id) ? minY[id] : 1000, y);
                    }
        }
    for (auto& [id, n] : count) n /= chunks;
    // Measured once (seed 4242) and widened by about +-40%: catches broken blobs,
    // counts or height ranges without pinning the exact random layout.
    const std::map<std::string, std::pair<double, double>> bands = {
        {"coal_ore", {100, 230}},  {"copper_ore", {90, 210}},   {"iron_ore", {45, 105}}, {"gold_ore", {15, 38}},
        {"diamond_ore", {14, 34}}, {"redstone_ore", {20, 48}}, {"lapis_ore", {14, 34}},
    };
    for (const auto& [id, band] : bands) {
        EXPECT_GE(count[id], band.first) << id;
        EXPECT_LE(count[id], band.second) << id;
    }
    EXPECT_LE(maxY["diamond_ore"], 16 + 3); // blobs reach a few blocks past their center range
    EXPECT_LE(maxY["redstone_ore"], 15 + 3);
    EXPECT_LE(maxY["lapis_ore"], 64 + 3);
    EXPECT_GE(minY["coal_ore"], -8);
}

namespace {

// The nearest sample (64-block grid, spline surface) whose surface biome is `id`.
std::optional<glm::ivec2> findBiome(const OverworldGenerator& gen, const BiomeRegistry& biomes, std::string_view id) {
    for (int r = 0; r <= 8192; r += 64)
        for (int dz = -r; dz <= r; dz += 64)
            for (int dx = -r; dx <= r; dx += 64) {
                if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
                if (shortId(biomes.get(gen.biomeAt(dx, gen.estimatedSurface(dx, dz), dz)).id) == id) return glm::ivec2(dx, dz);
            }
    return std::nullopt;
}

} // namespace

TEST_F(WorldgenFixture, FreezeAddsSnowAndIce) {
    auto gen = std::make_shared<OverworldGenerator>(*registry, *biomes, 31, loadDecorator(*registry, *biomes));
    const StateId snow = registry->defaultState("snow"), ice = registry->defaultState("ice");
    const TagId leaves = *registry->tag("leaves");
    int coldColumns = 0, warmColumns = 0;
    for (std::string_view biome : {"snowy_plains", "frozen_ocean", "plains"}) {
        const auto at = findBiome(*gen, *biomes, biome);
        ASSERT_TRUE(at.has_value()) << biome;
        const ChunkPos center = ChunkPos::of(BlockPos{at->x, 0, at->y});
        JobSystem jobs(8);
        ChunkManager cm(*registry, gen, jobs);
        cm.setLightFunction([](const Neighborhood&, Chunk&) {});
        cm.setView(center, 1);
        for (int i = 0; i < 2000 && !cm.viewReady(); ++i) {
            cm.update();
            jobs.waitIdle();
        }
        ASSERT_TRUE(cm.viewReady());
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                const Chunk& c = *cm.chunk({center.x + dx, center.z + dz});
                for (int z = 0; z < 16; ++z)
                    for (int x = 0; x < 16; ++x) {
                        // The motion-blocking top, as the freeze pass sees it.
                        int y = kWorldMaxY;
                        while (y > kWorldMinY && !countsForHeightmap(*registry, leaves, Heightmap::MotionBlocking, c.get(x, y, z))) --y;
                        const int si = sectionIndexOfY(y + 1);
                        const Biome& b = biomes->get(c.sections[size_t(si)].biomes[size_t(
                            ((((y + 1 - kWorldMinY) & 15) >> 2) << 4) | ((z >> 2) << 2) | (x >> 2))]);
                        const int wx = c.pos.minBlockX() + x, wz = c.pos.minBlockZ() + z;
                        const StateId top = c.get(x, y, z), above = c.get(x, y + 1, z);
                        const bool water = registry->info(top).fluid.kind == FluidKind::Water && registry->blockOf(top).className == "liquid";
                        if (water && b.coldEnoughToSnow(wx, y, wz)) {
                            ++coldColumns;
                            ADD_FAILURE() << "unfrozen cold water at " << wx << "," << y << "," << wz;
                        } else if (top == ice) {
                            ++coldColumns;
                            EXPECT_TRUE(b.coldEnoughToSnow(wx, y, wz));
                        } else if (b.coldEnoughToSnow(wx, y + 1, wz) && !water) {
                            ++coldColumns;
                            // Snow on top unless a plant took the spot.
                            if (above != snow) {
                                EXPECT_FALSE(registry->info(above).flags & kAir) << wx << "," << y + 1 << "," << wz;
                            }
                            if (above == snow && registry->blockOf(top).id == "minecraft:grass_block") {
                                EXPECT_EQ(registry->value(top, "snowy"), "true");
                            }
                        } else {
                            ++warmColumns;
                            EXPECT_NE(above, snow) << wx << "," << y + 1 << "," << wz;
                        }
                    }
            }
    }
    EXPECT_GT(coldColumns, 256);
    EXPECT_GT(warmColumns, 256);
}

// Trees span chunk borders (a source chunk writes into its 3x3), so a tree cut at a
// border leaves orphaned leaves. Every leaf must reach a log within 6 steps
// through leaves, which is also vanilla's leaf-decay rule.
TEST_F(WorldgenFixture, TreesAreNotCutAtChunkBorders) {
    auto gen = std::make_shared<OverworldGenerator>(*registry, *biomes, 77, loadDecorator(*registry, *biomes));
    const TagId leavesTag = *registry->tag("leaves"), logsTag = *registry->tag("logs");
    int leaves = 0, orphans = 0;
    for (std::string_view biome : {"forest", "dark_forest", "jungle", "old_growth_spruce_taiga", "savanna"}) {
        const auto at = findBiome(*gen, *biomes, biome);
        ASSERT_TRUE(at.has_value()) << biome;
        const ChunkPos center = ChunkPos::of(BlockPos{at->x, 0, at->y});
        JobSystem jobs(8);
        ChunkManager cm(*registry, gen, jobs);
        cm.setLightFunction([](const Neighborhood&, Chunk&) {});
        cm.setView(center, 2);
        for (int i = 0; i < 4000 && !cm.viewReady(); ++i) {
            cm.update();
            jobs.waitIdle();
        }
        ASSERT_TRUE(cm.viewReady());
        auto get = [&](const BlockPos& p) { return cm.getBlock(p); };
        // Leaves in the 3x3 center chunks; the search may step into the ring around them.
        for (int cz = center.z - 1; cz <= center.z + 1; ++cz)
            for (int cx = center.x - 1; cx <= center.x + 1; ++cx) {
                const Chunk* c = cm.chunk({cx, cz});
                for (int y = 40; y < 260; ++y)
                    for (int z = 0; z < 16; ++z)
                        for (int x = 0; x < 16; ++x) {
                            const StateId s = c->get(x, y, z);
                            if (!registry->hasTag(s, leavesTag) || registry->value(s, "persistent") == "true") continue;
                            ++leaves;
                            // Breadth-first through leaves, at most 6 steps.
                            std::vector<BlockPos> frontier{BlockPos(cx * 16 + x, y, cz * 16 + z)}, next;
                            std::set<std::tuple<int, int, int>> seen{{frontier[0].x, frontier[0].y, frontier[0].z}};
                            bool found = false;
                            for (int step = 0; step < 6 && !found && !frontier.empty(); ++step) {
                                next.clear();
                                for (const BlockPos& p : frontier)
                                    for (Direction d : kDirections) {
                                        const BlockPos n = p.offset(d);
                                        const StateId ns = get(n);
                                        if (registry->hasTag(ns, logsTag)) found = true;
                                        else if (registry->hasTag(ns, leavesTag) && seen.insert({n.x, n.y, n.z}).second)
                                            next.push_back(n);
                                    }
                                frontier.swap(next);
                            }
                            orphans += !found;
                        }
            }
    }
    if (leaves == 0) GTEST_SKIP() << "no tree features loaded";
    EXPECT_GT(leaves, 2000);
    EXPECT_LE(orphans, leaves / 100) << orphans << " orphaned leaves of " << leaves;
}

TEST_F(WorldgenFixture, ChunkStorageRoundTrip) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "mc_storage_test";
    std::filesystem::remove_all(dir);
    const OverworldGenerator gen(*registry, *biomes, 5);
    Chunk c({-3, 7});
    gen.generateTerrain(c);
    c.set(1, 70, 1, registry->defaultState("oak_stairs"));
    ChunkStorage storage(dir, *registry, biomes.get());
    std::string err;
    const Json extra = {{"ticks", Json::array({{1, 2, 3, "minecraft:oak_button", 10}})}};
    ASSERT_TRUE(storage.save(c, extra, &err)) << err;
    EXPECT_TRUE(storage.exists({-3, 7}));
    EXPECT_FALSE(storage.exists({-3, 8}));
    EXPECT_EQ(storage.pathFor({-3, 7}).parent_path().filename(), "-1.0");
    Json loadedExtra;
    auto loaded = storage.load({-3, 7}, &loadedExtra, &err);
    ASSERT_NE(loaded, nullptr) << err;
    EXPECT_EQ(hashChunk(*loaded), hashChunk(c));
    EXPECT_EQ(loadedExtra, extra);
    EXPECT_LT(std::filesystem::file_size(storage.pathFor({-3, 7})), 20000u); // compressed
    std::filesystem::remove_all(dir);
}
