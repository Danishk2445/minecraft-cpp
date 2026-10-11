#include <cmath>
#include <memory>
#include <set>

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

TEST_F(WorldgenFixture, OverworldPipelineIsDeterministic) {
    auto gen = std::make_shared<OverworldGenerator>(*registry, *biomes, 99);
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
