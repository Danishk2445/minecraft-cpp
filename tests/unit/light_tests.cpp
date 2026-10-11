#include <memory>

#include <gtest/gtest.h>

#include "core/JobSystem.h"
#include "core/Rng.h"
#include "resources/Atlas.h"
#include "resources/BlockModels.h"
#include "world/block/BlockRegistry.h"
#include "world/chunk/ChunkManager.h"
#include "world/light/LightEngine.h"

using namespace mc;

namespace {

struct LightFixture : ::testing::Test {
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
        engine = std::make_shared<LightEngine>(*registry);
    }
    static void TearDownTestSuite() {
        engine.reset();
        registry.reset();
        models.reset();
        baker.reset();
        store.reset();
        atlas.reset();
        pack.reset();
    }
    static StateId S(const char* t) { return *registry->parse(t); }

    // A lit world of stone up to y=63 (ground surface at 64) around chunk (0,0).
    void SetUp() override {
        jobs = std::make_unique<JobSystem>(4);
        auto gen = std::make_shared<FlatGenerator>(*registry,
                                                   std::vector<FlatGenerator::Layer>{{S("stone"), 64 - kWorldMinY}});
        cm = std::make_unique<ChunkManager>(*registry, gen, *jobs);
        cm->setLightEngine(engine);
        cm->setView({0, 0}, 2);
        for (int i = 0; i < 1000 && !cm->viewReady(); ++i) {
            cm->update();
            jobs->waitIdle();
        }
        ASSERT_TRUE(cm->viewReady());
    }
    void TearDown() override {
        cm.reset();
        jobs.reset();
    }

    int sky(int x, int y, int z) const {
        const Chunk* c = cm->chunk(ChunkPos::of({x, y, z}));
        return c->lightAt(x & 15, y, z & 15) >> 4;
    }
    int block(int x, int y, int z) const {
        const Chunk* c = cm->chunk(ChunkPos::of({x, y, z}));
        return c->lightAt(x & 15, y, z & 15) & 15;
    }
    void set(int x, int y, int z, StateId s) { ASSERT_TRUE(cm->setBlock({x, y, z}, s)); }

    // Recomputes light from scratch for chunks within `radius` and compares.
    int mismatches(int radius) {
        int bad = 0;
        for (int cz = -radius; cz <= radius; ++cz)
            for (int cx = -radius; cx <= radius; ++cx) {
                const Chunk* live = cm->chunk({cx, cz});
                Chunk fresh(*live);
                engine->computeChunk(cm->neighborhood({cx, cz}), fresh);
                for (int y = kWorldMinY; y <= kWorldMaxY; ++y)
                    for (int z = 0; z < 16; ++z)
                        for (int x = 0; x < 16; ++x)
                            if (live->lightAt(x, y, z) != fresh.lightAt(x, y, z)) {
                                if (bad < 5)
                                    ADD_FAILURE() << "light differs at " << cx * 16 + x << "," << y << "," << cz * 16 + z
                                                  << ": incremental " << int(live->lightAt(x, y, z)) << " vs scratch "
                                                  << int(fresh.lightAt(x, y, z));
                                ++bad;
                            }
            }
        return bad;
    }

    static inline std::unique_ptr<ResourcePack> pack;
    static inline std::unique_ptr<Atlas> atlas;
    static inline std::unique_ptr<ModelStore> store;
    static inline std::unique_ptr<ModelBaker> baker;
    static inline std::unique_ptr<BlockModels> models;
    static inline std::unique_ptr<BlockRegistry> registry;
    static inline std::shared_ptr<LightEngine> engine;
    std::unique_ptr<JobSystem> jobs;
    std::unique_ptr<ChunkManager> cm;
};

} // namespace

TEST_F(LightFixture, OpenSkyAndDarkGround) {
    EXPECT_EQ(sky(3, 64, 3), 15);
    EXPECT_EQ(sky(3, 200, 3), 15);
    EXPECT_EQ(sky(3, 63, 3), 0); // inside stone
    EXPECT_EQ(block(3, 64, 3), 0);
}

TEST_F(LightFixture, TorchFalloffIsManhattan) {
    set(8, 64, 8, S("torch"));
    EXPECT_EQ(block(8, 64, 8), 14);
    for (int k = 1; k <= 14; ++k) EXPECT_EQ(block(8 + k, 64, 8), 14 - k) << k;
    EXPECT_EQ(block(8 + 3, 64 + 2, 8 - 1), 14 - 6);
    EXPECT_EQ(block(8 + 15, 64, 8), 0);
    // Removing the torch darkens everything again.
    set(8, 64, 8, S("air"));
    EXPECT_EQ(block(8, 64, 8), 0);
    EXPECT_EQ(block(10, 64, 8), 0);
}

TEST_F(LightFixture, OverhangShadow) {
    for (int x = -2; x <= 2; ++x)
        for (int z = -2; z <= 2; ++z) set(x, 69, z, S("stone"));
    // The nearest open column is 3 blocks away horizontally.
    EXPECT_EQ(sky(0, 65, 0), 12);
    EXPECT_EQ(sky(2, 65, 2), 14);
    EXPECT_EQ(sky(0, 70, 0), 15);
}

TEST_F(LightFixture, SlabsBlockLightThroughTheirFullFace) {
    for (int x = -2; x <= 2; ++x)
        for (int z = -2; z <= 2; ++z) set(x, 69, z, S("oak_slab[type=bottom]"));
    EXPECT_EQ(sky(0, 69, 0), 15); // light enters the slab's open top half
    EXPECT_EQ(sky(0, 68, 0), 12); // but not through its bottom face
    // A top slab blocks from above but light can enter its cell from the side.
    set(10, 66, 10, S("oak_slab[type=top]"));
    EXPECT_EQ(sky(10, 66, 10), 14);
}

TEST_F(LightFixture, WaterAndLeavesDimSkylight) {
    for (int y = 64; y < 68; ++y) set(5, y, 5, S("water[level=0]"));
    // Surrounded by open air, so side light wins below the top: check a shaft instead.
    for (int y = 64; y < 70; ++y)
        for (int d = -1; d <= 1; ++d)
            for (int e = -1; e <= 1; ++e)
                if (d || e) set(5 + d, y, 5 + e, S("stone"));
    EXPECT_EQ(sky(5, 68, 5), 15);
    EXPECT_EQ(sky(5, 67, 5), 14);
    EXPECT_EQ(sky(5, 64, 5), 11);
}

TEST_F(LightFixture, IncrementalEqualsFromScratchAfterRandomEdits) {
    const StateId palette[] = {S("air"),       S("air"),        S("air"),      S("stone"), S("glass"),
                               S("oak_leaves"), S("torch"),     S("glowstone"), S("oak_slab[type=bottom]"),
                               S("oak_slab[type=top]"), S("water[level=0]"), S("oak_stairs[half=top]"),
                               S("sea_lantern"), S("ice")};
    Xoroshiro rng(12345);
    for (int i = 0; i < 10000; ++i) {
        const int x = rng.nextInt(16), z = rng.nextInt(16), y = 58 + rng.nextInt(16);
        set(x, y, z, palette[size_t(rng.nextInt(int(std::size(palette))))]);
    }
    EXPECT_EQ(mismatches(1), 0);
}
