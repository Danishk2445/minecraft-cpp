#include <memory>

#include <gtest/gtest.h>

#include "core/JobSystem.h"
#include "game/Commands.h"
#include "resources/Atlas.h"
#include "resources/BlockModels.h"
#include "world/block/BlockRegistry.h"
#include "world/chunk/ChunkManager.h"
#include "world/mesh/ChunkMesher.h"

using namespace mc;

namespace {

struct WorldFixture : ::testing::Test {
    static void SetUpTestSuite() {
        pack = std::make_unique<ResourcePack>(MC_DEFAULT_PACK_DIR);
        atlas = std::make_unique<Atlas>(*buildAtlas(*pack, "blocks", 4));
        store = std::make_unique<ModelStore>(*pack);
        baker = std::make_unique<ModelBaker>(*store, *atlas);
        models = std::make_unique<BlockModels>();
        models->load(*pack, *store, *baker);
        registry = std::make_unique<BlockRegistry>();
        std::string err;
        registry->build(*models, *atlas, MC_DEFAULT_DATA_DIR, &err);
    }
    static void TearDownTestSuite() {
        registry.reset();
        models.reset();
        baker.reset();
        store.reset();
        atlas.reset();
        pack.reset();
    }
    static StateId S(const char* text) { return *registry->parse(text); }

    // Runs the pipeline until the view is ready; returns a hash of every final chunk.
    static uint64_t generateAndHash(std::shared_ptr<const ChunkGenerator> gen, int threads, ChunkPos center, int rd) {
        JobSystem jobs(threads);
        ChunkManager cm(*registry, gen, jobs);
        cm.setView(center, rd);
        for (int i = 0; i < 100000 && !cm.viewReady(); ++i) {
            cm.update();
            jobs.drainMain();
            if (threads > 0) std::this_thread::yield();
        }
        EXPECT_TRUE(cm.viewReady());
        uint64_t h = 1469598103934665603ull;
        for (int dz = -rd; dz <= rd; ++dz)
            for (int dx = -rd; dx <= rd; ++dx) {
                const Chunk* c = cm.chunk({center.x + dx, center.z + dz});
                EXPECT_NE(c, nullptr);
                if (!c) continue;
                for (int y = kWorldMinY; y <= kWorldMaxY; ++y)
                    for (int z = 0; z < 16; ++z)
                        for (int x = 0; x < 16; ++x) h = (h ^ c->get(x, y, z)) * 1099511628211ull;
            }
        return h;
    }

    // Meshes the section containing y=64 of a single chunk built by `fill`.
    template <class Fill>
    static SectionMesh meshOne(Fill fill) {
        Chunk c({0, 0});
        fill(c);
        Neighborhood n;
        n.center = {0, 0};
        n.cols[4] = ColumnSnapshot::of(c);
        std::vector<SectionMesh> out;
        meshSections(*registry, MesherOptions{}, n, 1u << sectionIndexOfY(64), out);
        return out.at(0);
    }

    static inline std::unique_ptr<ResourcePack> pack;
    static inline std::unique_ptr<Atlas> atlas;
    static inline std::unique_ptr<ModelStore> store;
    static inline std::unique_ptr<ModelBaker> baker;
    static inline std::unique_ptr<BlockModels> models;
    static inline std::unique_ptr<BlockRegistry> registry;
};

} // namespace

TEST_F(WorldFixture, FlatWorldLayers) {
    JobSystem jobs(4);
    ChunkManager cm(*registry, std::make_shared<FlatGenerator>(*registry), jobs);
    cm.setView({0, 0}, 2);
    for (int i = 0; i < 100000 && !cm.viewReady(); ++i) {
        cm.update();
        jobs.drainMain();
    }
    ASSERT_TRUE(cm.viewReady());
    EXPECT_EQ(cm.getBlock({5, -64, 7}), S("bedrock"));
    EXPECT_EQ(cm.getBlock({5, -63, 7}), S("dirt"));
    EXPECT_EQ(cm.getBlock({5, -62, 7}), S("dirt"));
    EXPECT_EQ(registry->blockOf(cm.getBlock({5, -61, 7})).id, "minecraft:grass_block");
    EXPECT_EQ(cm.getBlock({5, -60, 7}), BlockRegistry::kAirState);
    // Edits work on ready chunks and are copy-on-write.
    EXPECT_TRUE(cm.setBlock({1, -60, 1}, S("stone")));
    EXPECT_EQ(cm.getBlock({1, -60, 1}), S("stone"));
}

TEST_F(WorldFixture, PipelineIsDeterministicAcrossThreadCounts) {
    auto flat = std::make_shared<FlatGenerator>(*registry);
    EXPECT_EQ(generateAndHash(flat, 0, {3, -2}, 3), generateAndHash(flat, 8, {3, -2}, 3));
    auto debug = std::make_shared<DebugGenerator>(*registry);
    const uint64_t a = generateAndHash(debug, 0, {6, 6}, 4);
    const uint64_t b = generateAndHash(debug, 8, {6, 6}, 4);
    const uint64_t c = generateAndHash(debug, 3, {6, 6}, 4);
    EXPECT_EQ(a, b);
    EXPECT_EQ(a, c);
}

TEST_F(WorldFixture, DebugWorldContainsEveryState) {
    DebugGenerator gen(*registry);
    std::vector<bool> seen(registry->stateCount(), false);
    const int span = gen.gridWidth() * 2 + 2;
    for (int x = 0; x <= span; ++x)
        for (int z = 0; z <= span; ++z) seen[gen.stateAt(x, z)] = true;
    for (size_t s = 1; s < registry->stateCount(); ++s)
        if (!(registry->info(StateId(s)).flags & kAir)) {
            EXPECT_TRUE(seen[s]) << registry->describe(StateId(s));
        }
}

TEST_F(WorldFixture, MesherCullsHiddenFaces) {
    auto quads = [](const SectionMesh& m) { return m.quadCount(); };
    const StateId stone = S("stone"), glass = S("glass");
    EXPECT_EQ(quads(meshOne([&](Chunk& c) { c.set(5, 64, 5, stone); })), 6u);
    EXPECT_EQ(quads(meshOne([&](Chunk& c) {
                  c.set(5, 64, 5, stone);
                  c.set(6, 64, 5, stone);
              })),
              10u);
    // A solid 3x3x3 cube shows only its 54 outer faces.
    EXPECT_EQ(quads(meshOne([&](Chunk& c) {
                  for (int x = 4; x < 7; ++x)
                      for (int y = 64; y < 67; ++y)
                          for (int z = 4; z < 7; ++z) c.set(x, y, z, stone);
              })),
              54u);
    // Glass never hides stone; stone hides the glass face touching it.
    EXPECT_EQ(quads(meshOne([&](Chunk& c) {
                  c.set(5, 64, 5, stone);
                  c.set(6, 64, 5, glass);
              })),
              11u);
    // Identical glass hides the shared faces (skip_same).
    EXPECT_EQ(quads(meshOne([&](Chunk& c) {
                  c.set(5, 64, 5, glass);
                  c.set(6, 64, 5, glass);
              })),
              10u);
    // Grass block: 6 faces + 4 side overlays.
    EXPECT_EQ(quads(meshOne([&](Chunk& c) { c.set(5, 64, 5, S("grass_block")); })), 10u);
}

TEST_F(WorldFixture, MesherPartialCoverage) {
    const StateId stone = S("stone"), slab = S("oak_slab[type=bottom]");
    // Slab west of stone: the slab's east side is covered by the stone, but the
    // stone's west face stays (the slab covers only its lower half).
    const SectionMesh m = meshOne([&](Chunk& c) {
        c.set(5, 64, 5, slab);
        c.set(6, 64, 5, stone);
    });
    // slab: 6 faces - 1 (east, covered) = 5; stone: 6 faces (west partially open) = 6.
    EXPECT_EQ(m.quadCount(), 11u);
    // Two bottom slabs side by side hide their touching half-faces.
    const SectionMesh pair = meshOne([&](Chunk& c) {
        c.set(5, 64, 5, slab);
        c.set(6, 64, 5, slab);
    });
    EXPECT_EQ(pair.quadCount(), 10u);
    // Layers: grass goes to cutout_mipped, glass to cutout, stained glass to translucent.
    const SectionMesh layers = meshOne([&](Chunk& c) {
        c.set(1, 64, 1, S("grass_block"));
        c.set(5, 64, 5, S("glass"));
        c.set(9, 64, 9, S("white_stained_glass"));
    });
    EXPECT_EQ(layers.layers[size_t(RenderLayer::Solid)].size(), 0u);
    EXPECT_EQ(layers.layers[size_t(RenderLayer::CutoutMipped)].size(), 40u);
    EXPECT_EQ(layers.layers[size_t(RenderLayer::Cutout)].size(), 24u);
    EXPECT_EQ(layers.layers[size_t(RenderLayer::Translucent)].size(), 24u);
    EXPECT_EQ(layers.translucentCenters.size(), 6u);
}

TEST(Commands, Tokenize) {
    const auto t = tokenizeCommand("  /setblock ~ ~1 -3   oak_stairs[facing=east, half=top] ");
    ASSERT_EQ(t.size(), 5u);
    EXPECT_EQ(t[0], "/setblock");
    EXPECT_EQ(t[4], "oak_stairs[facing=east, half=top]");
    EXPECT_EQ(parseCoordinate("~", 5.5), 5.5);
    EXPECT_EQ(parseCoordinate("~-2", 5.5), 3.5);
    EXPECT_EQ(parseCoordinate("12.25", 5.5), 12.25);
    EXPECT_FALSE(parseCoordinate("abc", 0).has_value());
}
