// The live world (M11): fluids flowing, and later random ticks, fire, explosions and
// weather, on a flat world with every chunk around the origin ticking.
#include <unistd.h>

#include <filesystem>
#include <map>
#include <memory>

#include <gtest/gtest.h>

#include "core/JobSystem.h"
#include "core/Rng.h"
#include "entity/FallingBlockEntity.h"
#include "entity/ItemEntity.h"
#include "entity/Player.h"
#include "game/Commands.h"
#include "game/Item.h"
#include "world/biome/Biome.h"
#include "world/gen/feature/BiomeDecorator.h"
#include "world/light/LightEngine.h"
#include "entity/LightningBolt.h"
#include "entity/PrimedTnt.h"
#include "world/sim/Explosion.h"
#include "world/sim/Fire.h"
#include "world/sim/Plants.h"
#include "world/sim/Weathering.h"
#include "world/sim/Simulation.h"
#include "resources/Atlas.h"
#include "resources/BlockModels.h"
#include "resources/ModelBaker.h"
#include "resources/ResourcePack.h"
#include "world/block/BlockRegistry.h"
#include "world/gen/ChunkGenerator.h"
#include "world/level/Level.h"
#include "world/sim/Fluids.h"
#include "world/storage/ChunkStorage.h"

using namespace mc;

namespace {

struct SimFixture : ::testing::Test {
    static void SetUpTestSuite() {
        pack = std::make_unique<ResourcePack>(MC_DEFAULT_PACK_DIR);
        atlas = std::make_unique<Atlas>(*buildAtlas(*pack, "blocks", 0));
        store = std::make_unique<ModelStore>(*pack);
        baker = std::make_unique<ModelBaker>(*store, *atlas);
        models = std::make_unique<BlockModels>();
        models->load(*pack, *store, *baker);
        registry = std::make_unique<BlockRegistry>();
        std::string err;
        registry->build(*models, *atlas, MC_DEFAULT_DATA_DIR, &err);
        items = std::make_unique<ItemRegistry>();
        items->load(*pack, *registry, MC_DEFAULT_DATA_DIR, &err);
        biomes = std::make_unique<BiomeRegistry>();
        biomes->load(MC_DEFAULT_DATA_DIR, MC_DEFAULT_PACK_DIR, &err);
        decorator = std::make_unique<BiomeDecorator>();
        decorator->load(MC_DEFAULT_DATA_DIR, *registry, *biomes, &err);
    }
    static void TearDownTestSuite() {
        decorator.reset();
        biomes.reset();
        items.reset();
        registry.reset();
        models.reset();
        baker.reset();
        store.reset();
        atlas.reset();
        pack.reset();
    }
    void SetUp() override {
        jobs = std::make_unique<JobSystem>(0);
        level = makeLevel();
    }
    std::unique_ptr<Level> makeLevel(std::shared_ptr<ChunkStorage> storage = nullptr) {
        auto flat = std::make_shared<FlatGenerator>(*registry);
        flat->biome = biomes->require("plains");
        auto l = std::make_unique<Level>(*registry, flat, *jobs, 1);
        if (storage) l->attachStorage(storage);
        l->chunks().setLightEngine(std::make_shared<LightEngine>(*registry));
        l->setItems(items.get());
        l->setWorldgen(decorator.get(), biomes.get());
        l->chunks().setView({0, 0}, 3);
        for (int i = 0; i < 1000 && !l->chunks().viewReady(); ++i) {
            l->chunks().update();
            jobs->waitIdle();
            jobs->drainMain(1000.0);
        }
        EXPECT_TRUE(l->chunks().viewReady());
        l->randomTickSpeed = 0; // only what the tests start
        return l;
    }
    void TearDown() override {
        level.reset();
        jobs.reset();
    }

    static StateId S(const char* text) { return *registry->parse(text); }
    std::string id(const Level& l, int x, int y, int z) const {
        return std::string(shortId(registry->blockOf(l.getBlock({x, y, z})).id));
    }
    std::string id(int x, int y, int z) const { return id(*level, x, y, z); }
    int fluidLevel(int x, int y, int z) const { return registry->intValue(level->getBlock({x, y, z}), "level", -1); }
    // Ticks until no fluid tick is pending (or `limit` ticks); the ticks it took.
    int settle(Level& l, int limit = 3000) {
        int n = 0;
        while ((l.pendingFluidTicks() > 0 || l.pendingTicks() > 0) && n < limit) {
            l.tick();
            ++n;
        }
        return n;
    }

    // Live entities of type T in the level.
    template <class T>
    std::vector<T*> all() const {
        std::vector<T*> out;
        for (const auto& e : level->entities())
            if (auto* t = dynamic_cast<T*>(e.get()); t && !e->removed) out.push_back(t);
        return out;
    }
    // Every block in the box that takes random ticks gets one, `rounds` times over (with
    // scheduled ticks run in between): random ticks, without waiting for the dice.
    void forceRandomTicks(BlockPos a, BlockPos b, int rounds) {
        for (int r = 0; r < rounds; ++r) {
            for (int y = a.y; y <= b.y; ++y)
                for (int z = a.z; z <= b.z; ++z)
                    for (int x = a.x; x <= b.x; ++x) {
                        const StateId st = level->getBlock({x, y, z});
                        if (sim::randomlyTicks(*registry, st)) sim::randomTick(*level, {x, y, z}, st);
                    }
            level->tick();
        }
    }
    int count(BlockPos a, BlockPos b, std::string_view block) const {
        int n = 0;
        for (int y = a.y; y <= b.y; ++y)
            for (int z = a.z; z <= b.z; ++z)
                for (int x = a.x; x <= b.x; ++x) n += id(x, y, z) == block;
        return n;
    }
    // Ticks until no block is falling (at most `limit` ticks); the ticks it took.
    int land(int limit = 400) {
        int n = 0;
        while (n < limit && (level->pendingTicks() > 0 || !all<FallingBlockEntity>().empty())) {
            level->tick();
            ++n;
        }
        return n;
    }

    static inline std::unique_ptr<ResourcePack> pack;
    static inline std::unique_ptr<Atlas> atlas;
    static inline std::unique_ptr<ModelStore> store;
    static inline std::unique_ptr<ModelBaker> baker;
    static inline std::unique_ptr<BlockModels> models;
    static inline std::unique_ptr<BlockRegistry> registry;
    static inline std::unique_ptr<ItemRegistry> items;
    static inline std::unique_ptr<BiomeRegistry> biomes;
    static inline std::unique_ptr<BiomeDecorator> decorator;
    std::unique_ptr<JobSystem> jobs;
    std::unique_ptr<Level> level;
};

constexpr int Y = -60; // the first air layer above the flat world's grass

TEST_F(SimFixture, WaterSpreadsSevenBlocksOnFlatGround) {
    level->setBlockAndUpdate({0, Y, 0}, S("water"));
    EXPECT_GT(level->pendingFluidTicks(), 0u);
    const int ticks = settle(*level);
    EXPECT_LT(ticks, 3000);
    // A diamond: level = distance (water's drop-off is 1), dry at 8.
    for (int dz = -9; dz <= 9; ++dz)
        for (int dx = -9; dx <= 9; ++dx) {
            const int d = std::abs(dx) + std::abs(dz);
            if (d <= 7) {
                EXPECT_EQ(id(dx, Y, dz), "water") << dx << "," << dz;
                EXPECT_EQ(fluidLevel(dx, Y, dz), d) << dx << "," << dz;
            } else {
                EXPECT_EQ(id(dx, Y, dz), "air") << dx << "," << dz;
            }
        }
    // Water steps every 5 ticks: the edge (7 steps out) was reached in about 35.
    EXPECT_GE(ticks, 35);
    EXPECT_LE(ticks, 60);
    // Taking the source away dries it all up again.
    level->setBlockAndUpdate({0, Y, 0}, BlockRegistry::kAirState);
    settle(*level);
    for (int dx = -8; dx <= 8; ++dx) EXPECT_EQ(id(dx, Y, 0), "air") << dx;
}

TEST_F(SimFixture, TwoSourcesMakeAThirdAndLavaReachesThree) {
    // Vanilla getNewLiquid: two sources beside a cell over solid ground make it one.
    level->setBlockAndUpdate({0, Y, 0}, S("water"));
    level->setBlockAndUpdate({2, Y, 0}, S("water"));
    settle(*level);
    EXPECT_EQ(fluidLevel(1, Y, 0), 0);
    // Lava (drop-off 2) reaches 3 blocks and never makes sources.
    level->setBlockAndUpdate({20, Y, 20}, S("lava"));
    level->setBlockAndUpdate({22, Y, 20}, S("lava"));
    settle(*level);
    EXPECT_EQ(id(21, Y, 20), "lava");
    EXPECT_NE(fluidLevel(21, Y, 20), 0);
    EXPECT_EQ(fluidLevel(20, Y, 23), 6); // amount 2 three blocks out
    EXPECT_EQ(id(20, Y, 24), "air");
}

TEST_F(SimFixture, LavaMeetingWaterHardens) {
    // A water source beside a lava source: obsidian.
    level->setBlockAndUpdate({0, Y, 0}, S("lava"));
    level->setBlockAndUpdate({1, Y, 0}, S("water"));
    EXPECT_EQ(id(0, Y, 0), "obsidian");
    // Water reaching flowing lava (spread 3 blocks first): cobblestone where they meet.
    level->setBlockAndUpdate({10, Y, 10}, S("lava"));
    settle(*level);
    EXPECT_EQ(fluidLevel(13, Y, 10), 6);
    level->setBlockAndUpdate({15, Y, 10}, S("water"));
    settle(*level);
    EXPECT_EQ(id(13, Y, 10), "cobblestone");
    // Lava falling onto still water turns it to stone.
    level->setBlockAndUpdate({-10, Y, -10}, S("water"));
    settle(*level);
    level->setBlockAndUpdate({-10, Y + 2, -10}, S("lava"));
    settle(*level);
    EXPECT_EQ(id(-10, Y, -10), "stone");
}

TEST_F(SimFixture, WaterFlowsDownAndTowardTheNearestDrop) {
    // A hole two blocks east: water heads only that way (vanilla getSlopeDistance).
    level->setBlockAndUpdate({2, Y - 1, 0}, BlockRegistry::kAirState);
    level->setBlockAndUpdate({2, Y - 2, 0}, BlockRegistry::kAirState);
    level->setBlockAndUpdate({2, Y - 3, 0}, BlockRegistry::kAirState);
    level->setBlockAndUpdate({0, Y, 0}, S("water"));
    for (int i = 0; i < 12; ++i) level->tick();
    EXPECT_EQ(id(1, Y, 0), "water");
    EXPECT_EQ(id(-1, Y, 0), "air");
    EXPECT_EQ(id(0, Y, 1), "air");
    settle(*level);
    // It falls into the hole: falling water (level 8) down the shaft.
    EXPECT_EQ(fluidLevel(2, Y - 1, 0), 8);
    EXPECT_EQ(id(2, Y - 3, 0), "water");
    // Water washes a torch away, and it drops (no loot without items: just gone here).
    level->setBlockAndUpdate({5, Y, 5}, S("torch"));
    level->setBlockAndUpdate({4, Y, 5}, S("water"));
    settle(*level);
    EXPECT_EQ(id(5, Y, 5), "water");
}

TEST_F(SimFixture, WaterloggedBlocksPourOutAndFlowingWaterNeverWaterlogs) {
    // A waterlogged slab is a source: water pours out of it.
    level->setBlockAndUpdate({0, Y, 0}, S("oak_slab[type=top,waterlogged=true]"));
    level->setBlockAndUpdate({0, Y, 1}, S("stone")); // poke it so it updates
    level->setBlockAndUpdate({0, Y, 1}, BlockRegistry::kAirState);
    settle(*level);
    EXPECT_EQ(id(1, Y, 0), "water");
    EXPECT_EQ(fluidLevel(1, Y, 0), 1);
    // Flowing water next to an empty slab leaves it dry.
    level->setBlockAndUpdate({5, Y, 5}, S("oak_slab[type=bottom,waterlogged=false]"));
    level->setBlockAndUpdate({4, Y, 5}, S("water"));
    settle(*level);
    EXPECT_EQ(registry->value(level->getBlock({5, Y, 5}), "waterlogged"), "false");
}

TEST_F(SimFixture, FluidQueueDrainsAfterFiftyRandomPlacements) {
    Xoroshiro random(42);
    for (int i = 0; i < 50; ++i) {
        const int x = random.nextInt(32) - 16, z = random.nextInt(32) - 16, y = Y + random.nextInt(4);
        level->setBlockAndUpdate({x, y, z}, S(random.nextInt(3) == 0 ? "lava" : "water"));
    }
    const int ticks = settle(*level, 20000);
    EXPECT_LT(ticks, 20000);
    EXPECT_EQ(level->pendingFluidTicks(), 0u);
}

TEST_F(SimFixture, SavingMidFlowMatchesAnUninterruptedRun) {
    const auto dir = std::filesystem::temp_directory_path() / ("mc_fluid_test_" + std::to_string(::getpid()));
    std::filesystem::remove_all(dir);
    auto place = [&](Level& l) {
        l.setBlockAndUpdate({0, Y, 0}, S("water"));
        l.setBlockAndUpdate({6, Y + 3, 3}, S("water"));
        l.setBlockAndUpdate({-5, Y, -6}, S("lava"));
    };
    // Uninterrupted.
    place(*level);
    settle(*level);
    // Interrupted: saved after 12 ticks, reloaded into a new level, then finished.
    {
        auto storage = std::make_shared<ChunkStorage>(dir, *registry, nullptr);
        auto first = makeLevel(storage);
        place(*first);
        for (int i = 0; i < 12; ++i) first->tick();
        EXPECT_GT(first->pendingFluidTicks(), 0u);
        first->chunks().saveAll();
        while (first->chunks().savesPending()) {
            jobs->waitIdle();
            jobs->drainMain(1000.0);
        }
        first.reset();
        auto second = makeLevel(storage);
        EXPECT_GT(second->pendingFluidTicks(), 0u);
        settle(*second);
        for (int y = Y - 1; y <= Y + 4; ++y)
            for (int z = -16; z <= 16; ++z)
                for (int x = -16; x <= 16; ++x)
                    ASSERT_EQ(second->getBlock({x, y, z}), level->getBlock({x, y, z})) << x << "," << y << "," << z;
    }
    std::filesystem::remove_all(dir);
}

TEST_F(SimFixture, SandFallsAndLandsAsABlock) {
    level->setBlockAndUpdate({0, Y + 5, 0}, S("sand"));
    EXPECT_EQ(id(0, Y + 5, 0), "sand"); // it waits 2 ticks before checking its support
    level->tick();
    level->tick();
    EXPECT_EQ(id(0, Y + 5, 0), "air");
    ASSERT_EQ(all<FallingBlockEntity>().size(), 1u);
    const int ticks = land();
    // Five blocks under gravity 0.04 with drag 0.98: about 16 ticks.
    EXPECT_GE(ticks, 12);
    EXPECT_LE(ticks, 22);
    EXPECT_EQ(id(0, Y, 0), "sand");
    EXPECT_EQ(id(0, Y + 1, 0), "air");
    EXPECT_TRUE(all<ItemEntity>().empty());
}

TEST_F(SimFixture, AColumnFallsWhenItsSupportGoes) {
    level->setBlockAndUpdate({0, Y, 0}, S("stone"));
    level->setBlockAndUpdate({0, Y + 1, 0}, S("gravel"));
    level->setBlockAndUpdate({0, Y + 2, 0}, S("red_sand"));
    level->setBlockAndUpdate({0, Y + 3, 0}, S("sand"));
    land();
    EXPECT_EQ(id(0, Y + 3, 0), "sand"); // held up
    level->setBlockAndUpdate({0, Y, 0}, BlockRegistry::kAirState);
    land();
    EXPECT_EQ(id(0, Y, 0), "gravel");
    EXPECT_EQ(id(0, Y + 1, 0), "red_sand");
    EXPECT_EQ(id(0, Y + 2, 0), "sand");
    EXPECT_EQ(id(0, Y + 3, 0), "air");
}

TEST_F(SimFixture, SandLandingOnATorchOrSlabBreaksIntoAnItem) {
    level->setBlockAndUpdate({0, Y, 0}, S("torch"));
    level->setBlockAndUpdate({0, Y + 4, 0}, S("sand"));
    level->setBlockAndUpdate({4, Y, 0}, S("oak_slab"));
    level->setBlockAndUpdate({4, Y + 4, 0}, S("gravel"));
    land();
    EXPECT_EQ(id(0, Y, 0), "torch");
    EXPECT_EQ(id(4, Y, 0), "oak_slab");
    std::map<std::string, int> dropped;
    for (ItemEntity* e : all<ItemEntity>()) dropped[std::string(shortId(e->stack.item->id))] += e->stack.count;
    EXPECT_EQ(dropped, (std::map<std::string, int>{{"gravel", 1}, {"sand", 1}}));
    // Snow and grass don't hold it up: it falls through and replaces a single snow layer.
    level->setBlockAndUpdate({8, Y, 0}, S("snow[layers=1]"));
    level->setBlockAndUpdate({8, Y + 1, 0}, S("short_grass"));
    level->setBlockAndUpdate({8, Y + 2, 0}, S("sand"));
    land();
    EXPECT_EQ(id(8, Y, 0), "sand");
}

TEST_F(SimFixture, ConcretePowderSetsInWater) {
    // Into a pool: it sinks into the water and sets where it lands.
    level->setBlockAndUpdate({0, Y - 1, 0}, S("water"));
    level->setBlockAndUpdate({0, Y + 3, 0}, S("red_concrete_powder"));
    land();
    settle(*level);
    EXPECT_EQ(id(0, Y - 1, 0), "red_concrete");
    // Placed beside water it sets at once; water reaching it later sets it too.
    level->setBlockAndUpdate({5, Y - 1, 0}, S("water"));
    level->setBlockAndUpdate({6, Y - 1, 0}, S("blue_concrete_powder"));
    EXPECT_EQ(id(6, Y - 1, 0), "blue_concrete");
    level->setBlockAndUpdate({10, Y, 0}, S("lime_concrete_powder"));
    level->setBlockAndUpdate({12, Y, 0}, S("water"));
    settle(*level);
    EXPECT_EQ(id(10, Y, 0), "lime_concrete");
    // Dry powder on dry ground stays powder.
    level->setBlockAndUpdate({20, Y, 20}, S("white_concrete_powder"));
    land();
    EXPECT_EQ(id(20, Y, 20), "white_concrete_powder");
}

TEST_F(SimFixture, AFallingAnvilHurtsThePlayerUnderIt) {
    Player player;
    player.pos = player.oldPos = {0.5, double(Y), 0.5};
    level->setPlayer(&player);
    level->setBlockAndUpdate({0, Y + 10, 0}, S("anvil[facing=east]"));
    land();
    level->setPlayer(nullptr);
    // Ten blocks: ceil(10 - 1) x 2 = 18 damage, no armor.
    EXPECT_FLOAT_EQ(player.health, 2.0f);
    EXPECT_EQ(player.lastDamage, DamageType::Anvil);
    // It landed (perhaps chipped), keeping its facing.
    const std::string landed = id(0, Y, 0);
    EXPECT_TRUE(landed == "anvil" || landed == "chipped_anvil") << landed;
    EXPECT_EQ(registry->value(level->getBlock({0, Y, 0}), "facing"), "east");
}

TEST_F(SimFixture, FallingBlocksSaveAndLoad) {
    level->setBlockAndUpdate({0, Y + 20, 0}, S("damaged_anvil[facing=west]"));
    for (int i = 0; i < 6; ++i) level->tick();
    ASSERT_EQ(all<FallingBlockEntity>().size(), 1u);
    const FallingBlockEntity& before = *all<FallingBlockEntity>()[0];
    auto copy = level->loadEntity(Level::saveEntity(before));
    auto* after = dynamic_cast<FallingBlockEntity*>(copy.get());
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->state, before.state);
    EXPECT_EQ(after->time, before.time);
    EXPECT_TRUE(after->hurtEntities);
    EXPECT_EQ(after->startPos, (BlockPos{0, Y + 20, 0}));
    EXPECT_DOUBLE_EQ(after->velocity.y, before.velocity.y);
}

TEST_F(SimFixture, CropsGrowUnderForcedRandomTicks) {
    // A wet 3x3 field of wheat (water in the middle).
    for (int z = -1; z <= 1; ++z)
        for (int x = -1; x <= 1; ++x) {
            if (x == 0 && z == 0) continue;
            level->setBlockAndUpdate({x, Y - 1, z}, S("farmland[moisture=7]"));
            level->setBlockAndUpdate({x, Y, z}, S("wheat"));
        }
    level->setBlockAndUpdate({0, Y - 1, 0}, S("water"));
    forceRandomTicks({-1, Y, -1}, {1, Y, 1}, 200);
    EXPECT_EQ(count({-1, Y, -1}, {1, Y, 1}, "wheat"), 8);
    for (int z = -1; z <= 1; ++z)
        for (int x = -1; x <= 1; ++x)
            if (x != 0 || z != 0) {
                EXPECT_EQ(registry->intValue(level->getBlock({x, Y, z}), "age"), 7) << x << "," << z;
            }
    // Shut in the dark, wheat doesn't grow.
    for (int z = 9; z <= 11; ++z)
        for (int x = 9; x <= 11; ++x) {
            level->setBlockAndUpdate({x, Y + 1, z}, S("stone"));
            if (x != 10 || z != 10) level->setBlockAndUpdate({x, Y, z}, S("stone"));
        }
    level->setBlockAndUpdate({10, Y - 1, 10}, S("farmland[moisture=7]"));
    level->setBlockAndUpdate({10, Y, 10}, S("wheat"));
    forceRandomTicks({10, Y, 10}, {10, Y, 10}, 200);
    EXPECT_EQ(registry->intValue(level->getBlock({10, Y, 10}), "age"), 0);
}

TEST_F(SimFixture, StemsGrowFruitBesideThem) {
    level->setBlockAndUpdate({0, Y - 1, 0}, S("farmland[moisture=7]"));
    level->setBlockAndUpdate({0, Y, 0}, S("pumpkin_stem[age=7]"));
    forceRandomTicks({0, Y, 0}, {0, Y, 0}, 300);
    EXPECT_EQ(id(0, Y, 0), "attached_pumpkin_stem");
    const Direction facing = *directionFromName(registry->value(level->getBlock({0, Y, 0}), "facing"));
    const BlockPos fruit = BlockPos(0, Y, 0).offset(facing);
    EXPECT_EQ(id(fruit.x, fruit.y, fruit.z), "pumpkin");
    // Taking the pumpkin leaves a ripe stem.
    level->setBlockAndUpdate(fruit, BlockRegistry::kAirState);
    EXPECT_EQ(id(0, Y, 0), "pumpkin_stem");
    EXPECT_EQ(registry->intValue(level->getBlock({0, Y, 0}), "age"), 7);
}

TEST_F(SimFixture, SugarCaneAndCactusStopAtThree) {
    level->setBlockAndUpdate({0, Y - 1, 0}, S("sand"));
    level->setBlockAndUpdate({1, Y - 1, 0}, S("water"));
    level->setBlockAndUpdate({0, Y, 0}, S("sugar_cane"));
    level->setBlockAndUpdate({5, Y - 1, 5}, S("sand"));
    level->setBlockAndUpdate({5, Y, 5}, S("cactus"));
    forceRandomTicks({0, Y, 0}, {5, Y + 4, 5}, 200);
    EXPECT_EQ(count({0, Y, 0}, {0, Y + 4, 0}, "sugar_cane"), 3);
    // Cactus grows three tall too, unless a flower (at age 8) tops it off sooner.
    const int cactus = count({5, Y, 5}, {5, Y + 4, 5}, "cactus");
    EXPECT_GE(cactus, 1);
    EXPECT_LE(cactus, 3);
    const std::string top = id(5, Y + cactus, 5);
    EXPECT_TRUE(cactus == 3 ? top == "air" || top == "cactus_flower" : top == "cactus_flower") << cactus << " " << top;
}

TEST_F(SimFixture, KelpGrowsUpToTheSurface) {
    // A 6-deep shaft of water sources in a stone tower.
    for (int y = Y; y <= Y + 5; ++y)
        for (int z = -1; z <= 1; ++z)
            for (int x = -1; x <= 1; ++x) level->setBlock({x, y, z}, (x == 0 && z == 0) ? S("water") : S("stone"));
    level->setBlockAndUpdate({0, Y, 0}, S("kelp[age=0]"));
    forceRandomTicks({0, Y, 0}, {0, Y + 6, 0}, 400);
    // Up to the top water block, never out of it: body pieces under a single head.
    EXPECT_EQ(id(0, Y + 5, 0), "kelp");
    EXPECT_EQ(count({0, Y, 0}, {0, Y + 4, 0}, "kelp_plant"), 5);
    EXPECT_EQ(id(0, Y + 6, 0), "air");
    // Cutting it leaves a new head on top of what's left; the part above floats away.
    level->setBlockAndUpdate({0, Y + 2, 0}, S("water"));
    EXPECT_EQ(id(0, Y + 1, 0), "kelp");
    EXPECT_EQ(id(0, Y + 3, 0), "water");
    EXPECT_EQ(id(0, Y + 5, 0), "water");
}

TEST_F(SimFixture, SaplingsGrowIntoTrees) {
    level->setBlockAndUpdate({0, Y, 0}, S("oak_sapling"));
    for (int i = 0; i < 50 && id(0, Y, 0) == "oak_sapling"; ++i) sim::applyBoneMeal(*level, {0, Y, 0}, Direction::Up);
    settle(*level);
    EXPECT_EQ(id(0, Y, 0), "oak_log");
    EXPECT_EQ(id(0, Y - 1, 0), "dirt"); // trees turn the grass under them to dirt
    EXPECT_GT(count({-3, Y + 2, -3}, {3, Y + 8, 3}, "oak_leaves"), 20);
    // A dark oak needs a 2x2 of saplings: one alone never grows.
    level->setBlockAndUpdate({20, Y, 0}, S("dark_oak_sapling"));
    for (int i = 0; i < 40; ++i) sim::applyBoneMeal(*level, {20, Y, 0}, Direction::Up);
    EXPECT_EQ(id(20, Y, 0), "dark_oak_sapling");
    for (int x = 20; x <= 21; ++x)
        for (int z = 0; z <= 1; ++z) level->setBlockAndUpdate({x, Y, z}, S("dark_oak_sapling"));
    for (int i = 0; i < 50 && id(21, Y, 1) == "dark_oak_sapling"; ++i) sim::applyBoneMeal(*level, {21, Y, 1}, Direction::Up);
    EXPECT_EQ(count({20, Y, 0}, {21, Y, 1}, "dark_oak_log"), 4);
    // No room: a sapling under a low ceiling stays a sapling.
    level->setBlockAndUpdate({0, Y, 20}, S("birch_sapling"));
    level->setBlockAndUpdate({0, Y + 2, 20}, S("stone"));
    for (int i = 0; i < 40; ++i) sim::applyBoneMeal(*level, {0, Y, 20}, Direction::Up);
    EXPECT_EQ(id(0, Y, 20), "birch_sapling");
}

TEST_F(SimFixture, LeavesDecayWithoutALog) {
    level->setBlockAndUpdate({0, Y, 0}, S("oak_sapling"));
    for (int i = 0; i < 50 && id(0, Y, 0) == "oak_sapling"; ++i) sim::applyBoneMeal(*level, {0, Y, 0}, Direction::Up);
    settle(*level);
    const BlockPos a{-4, Y, -4}, b{4, Y + 10, 4};
    const int leaves = count(a, b, "oak_leaves");
    ASSERT_GT(leaves, 20);
    // While the trunk stands, nothing decays.
    forceRandomTicks(a, b, 20);
    EXPECT_EQ(count(a, b, "oak_leaves"), leaves);
    // Without it the distances run out to 7 a tick at a time, then the leaves go.
    for (int y = Y; y <= Y + 10; ++y)
        if (id(0, y, 0) == "oak_log") level->setBlockAndUpdate({0, y, 0}, BlockRegistry::kAirState);
    settle(*level);
    forceRandomTicks(a, b, 5);
    EXPECT_EQ(count(a, b, "oak_leaves"), 0);
    // Placed leaves (persistent) never decay.
    level->setBlockAndUpdate({10, Y + 3, 10}, S("birch_leaves[persistent=true]"));
    settle(*level);
    forceRandomTicks({10, Y + 3, 10}, {10, Y + 3, 10}, 20);
    EXPECT_EQ(id(10, Y + 3, 10), "birch_leaves");
}

TEST_F(SimFixture, GrassSpreadsInTheLightAndDiesUnderBlocks) {
    // A dirt patch around one grass block (the flat world's top is grass: make it dirt).
    for (int z = -2; z <= 2; ++z)
        for (int x = -2; x <= 2; ++x) level->setBlockAndUpdate({x, Y - 1, z}, S("dirt"));
    level->setBlockAndUpdate({0, Y - 1, 0}, S("grass_block"));
    forceRandomTicks({-2, Y - 1, -2}, {2, Y - 1, 2}, 60);
    EXPECT_EQ(count({-2, Y - 1, -2}, {2, Y - 1, 2}, "grass_block"), 25);
    // Covered by stone, grass turns to dirt; under a top slab it lives.
    level->setBlockAndUpdate({0, Y, 0}, S("stone"));
    level->setBlockAndUpdate({1, Y, 0}, S("oak_slab[type=top]"));
    level->setBlockAndUpdate({2, Y, 0}, S("oak_slab[type=bottom]"));
    forceRandomTicks({0, Y - 1, 0}, {2, Y - 1, 0}, 1);
    EXPECT_EQ(id(0, Y - 1, 0), "dirt");
    EXPECT_EQ(id(1, Y - 1, 0), "grass_block");
    EXPECT_EQ(id(2, Y - 1, 0), "dirt");
}

TEST_F(SimFixture, BoneMealGrowsCropsAndGrass) {
    level->setBlockAndUpdate({0, Y - 1, 0}, S("farmland"));
    level->setBlockAndUpdate({0, Y, 0}, S("wheat"));
    EXPECT_TRUE(sim::applyBoneMeal(*level, {0, Y, 0}, Direction::Up));
    const int age = registry->intValue(level->getBlock({0, Y, 0}), "age");
    EXPECT_GE(age, 2);
    EXPECT_LE(age, 5);
    for (int i = 0; i < 4; ++i) sim::applyBoneMeal(*level, {0, Y, 0}, Direction::Up);
    EXPECT_EQ(registry->intValue(level->getBlock({0, Y, 0}), "age"), 7);
    EXPECT_FALSE(sim::applyBoneMeal(*level, {0, Y, 0}, Direction::Up)); // ripe: nothing to do
    // On grass: short grass and the plains' flowers around.
    EXPECT_TRUE(sim::applyBoneMeal(*level, {10, Y - 1, 10}, Direction::Up));
    const int grass = count({3, Y, 3}, {17, Y, 17}, "short_grass");
    int flowers = 0;
    for (const char* f : {"dandelion", "poppy", "oxeye_daisy", "cornflower", "azure_bluet", "orange_tulip", "red_tulip",
                          "white_tulip", "pink_tulip"})
        flowers += count({3, Y, 3}, {17, Y, 17}, f);
    EXPECT_GT(grass, 10);
    EXPECT_GT(grass + flowers, 15);
    EXPECT_EQ(level->takeEvents().back().type, LevelEventType::BoneMeal);
    // Not on stone.
    level->setBlockAndUpdate({-10, Y - 1, -10}, S("stone"));
    EXPECT_FALSE(sim::applyBoneMeal(*level, {-10, Y - 1, -10}, Direction::Up));
}

TEST_F(SimFixture, VinesHangDownAndBambooShootsUp) {
    // A vine on a wall grows down a random part of the way each time.
    for (int y = Y; y <= Y + 6; ++y) level->setBlockAndUpdate({0, y, 0}, S("stone"));
    level->setBlockAndUpdate({0, Y + 6, 1}, S("vine[north=true]"));
    forceRandomTicks({0, Y, 1}, {0, Y + 6, 1}, 300);
    EXPECT_GE(count({0, Y, 1}, {0, Y + 5, 1}, "vine"), 3);
    // Bamboo from a shoot up to 12-16 tall, leaves at the top.
    level->setBlockAndUpdate({10, Y, 10}, S("bamboo_sapling"));
    forceRandomTicks({10, Y, 10}, {10, Y + 20, 10}, 400);
    const int tall = count({10, Y, 10}, {10, Y + 20, 10}, "bamboo");
    EXPECT_GE(tall, 12);
    EXPECT_LE(tall, 16);
    EXPECT_EQ(registry->value(level->getBlock({10, Y + tall - 1, 10}), "leaves"), "large");
    EXPECT_EQ(registry->value(level->getBlock({10, Y, 10}), "leaves"), "none");
}

// Fills a box with one block (plain setBlock, no updates).
void fill(Level& l, const BlockRegistry& reg, BlockPos a, BlockPos b, const char* block) {
    const StateId s = *reg.parse(block);
    for (int y = a.y; y <= b.y; ++y)
        for (int z = a.z; z <= b.z; ++z)
            for (int x = a.x; x <= b.x; ++x) l.setBlock({x, y, z}, s);
}

TEST_F(SimFixture, TntCratersStayWithinVanillaBands) {
    // Power 4 in the middle of dirt and of stone: dirt gives way much farther.
    fill(*level, *registry, {-8, Y, -8}, {8, Y + 16, 8}, "dirt");
    fill(*level, *registry, {24, Y, -8}, {40, Y + 16, 8}, "stone");
    const BlockPos a{-8, Y, -8}, b{8, Y + 16, 8}, c{24, Y, -8}, d{40, Y + 16, 8};
    level->setBlock({0, Y + 8, 0}, BlockRegistry::kAirState); // where the TNT was
    level->setBlock({32, Y + 8, 0}, BlockRegistry::kAirState);
    sim::explode(*level, nullptr, {0.5, Y + 8.0625, 0.5}, 4.0f);
    sim::explode(*level, nullptr, {32.5, Y + 8.0625, 0.5}, 4.0f);
    const int dirt = count(a, b, "air"), stone = count(c, d, "air");
    EXPECT_GE(dirt, 150) << dirt;
    EXPECT_LE(dirt, 280) << dirt;
    EXPECT_GE(stone, 5) << stone;
    EXPECT_LE(stone, 40) << stone;
    // Nothing reaches past 4 blocks in dirt or 2 in stone.
    for (int y = a.y; y <= b.y; ++y)
        for (int z = a.z; z <= b.z; ++z)
            for (int x = a.x; x <= b.x; ++x)
                if (id(x, y, z) == "air") {
                    EXPECT_LE(glm::length(glm::vec3(x + 0.5f, y + 0.5f - (Y + 8.0f), z + 0.5f)), 5.5f);
                }
    // Obsidian doesn't break; water absorbs the blast.
    fill(*level, *registry, {-30, Y, -2}, {-26, Y + 4, 2}, "obsidian");
    level->setBlock({-28, Y + 2, 0}, BlockRegistry::kAirState);
    sim::explode(*level, nullptr, {-27.5, Y + 2.0625, 0.5}, 4.0f);
    EXPECT_EQ(count({-30, Y, -2}, {-26, Y + 4, 2}, "obsidian"), 124);
    fill(*level, *registry, {-30, Y, 20}, {-26, Y + 4, 24}, "dirt");
    level->setBlock({-28, Y + 2, 22}, S("water"));
    sim::explode(*level, nullptr, {-27.5, Y + 2.0625, 22.5}, 4.0f);
    EXPECT_EQ(count({-30, Y, 20}, {-26, Y + 4, 24}, "dirt"), 124);
}

TEST_F(SimFixture, TntLightsTntBesideIt) {
    level->setBlockAndUpdate({0, Y, 0}, S("tnt"));
    level->setBlockAndUpdate({2, Y, 0}, S("tnt"));
    level->setBlockAndUpdate({0, Y, 0}, BlockRegistry::kAirState);
    PrimedTnt::prime(*level, {0, Y, 0});
    for (int i = 0; i < 80; ++i) level->tick();
    // The first went off; the second is lit on a short fuse (10-29 ticks).
    EXPECT_EQ(id(2, Y, 0), "air");
    const auto lit = all<PrimedTnt>();
    ASSERT_EQ(lit.size(), 1u);
    EXPECT_GE(lit[0]->fuse, 9);
    EXPECT_LE(lit[0]->fuse, 29);
    EXPECT_EQ(level->takeEvents().back().type, LevelEventType::Explosion);
    for (int i = 0; i < 30; ++i) level->tick();
    EXPECT_TRUE(all<PrimedTnt>().empty());
}

TEST_F(SimFixture, ExplosionsHurtByDistanceAndCover) {
    // Vanilla getEntityDamageAmount: power 4, 6 blocks off in the open: (e^2+e)/2 x 56 + 1
    // with e = 1 - 6/8.
    Player player;
    player.pos = player.oldPos = {6.5, double(Y), 0.5};
    level->setPlayer(&player);
    sim::explode(*level, nullptr, {0.5, Y + 0.0625, 0.5}, 4.0f);
    EXPECT_NEAR(player.health, 20.0f - 9.75f, 0.02f);
    EXPECT_GT(player.velocity.x, 0.0); // thrown away from it
    // Behind a wall it sees nothing: no damage at all... except the vanilla minimum of 1.
    Player hidden;
    hidden.pos = hidden.oldPos = {40.5, double(Y), 0.5};
    fill(*level, *registry, {37, Y, -3}, {37, Y + 4, 3}, "obsidian");
    level->setPlayer(&hidden);
    sim::explode(*level, nullptr, {35.5, Y + 0.0625, 0.5}, 4.0f);
    EXPECT_NEAR(hidden.health, 19.0f, 0.01f);
    level->setPlayer(nullptr);
    // Dropped items close by are destroyed.
    ItemEntity::spawn(*level, {60.5, double(Y), 0.5}, ItemStack{items->find("stick"), 1});
    sim::explode(*level, nullptr, {61.0, double(Y), 0.5}, 4.0f);
    EXPECT_TRUE(all<ItemEntity>().empty() || all<ItemEntity>()[0]->stack.item != items->find("stick"));
}

TEST_F(SimFixture, FireSpreadsThroughWoodAndDiesOnStone) {
    level->doFireTick = true;
    // Eight wool floors, each lit in the middle: fire eats some of every floor and spreads
    // (wool: ignite odds 30, burn odds 60; one sample is too noisy to judge).
    int burned = 0, spread = 0;
    for (int f = 0; f < 8; ++f) {
        const int x0 = (f % 4) * 12 - 24, z0 = (f / 4) * 12 - 12;
        fill(*level, *registry, {x0, Y, z0}, {x0 + 4, Y, z0 + 4}, "white_wool");
        level->setBlockAndUpdate({x0 + 2, Y + 1, z0 + 2}, S("fire"));
    }
    for (int i = 0; i < 2400; ++i) level->tick();
    for (int f = 0; f < 8; ++f) {
        const int x0 = (f % 4) * 12 - 24, z0 = (f / 4) * 12 - 12;
        const int left = count({x0, Y, z0}, {x0 + 4, Y, z0 + 4}, "white_wool");
        burned += 25 - left;
        spread += left < 24;
    }
    EXPECT_GE(burned, 60) << burned;
    EXPECT_GE(spread, 6) << spread;
    // Fire on bare stone goes out by itself; on netherrack it never does.
    level->setBlockAndUpdate({20, Y - 1, 20}, S("stone"));
    level->setBlockAndUpdate({20, Y, 20}, S("fire"));
    level->setBlockAndUpdate({24, Y - 1, 20}, S("netherrack"));
    level->setBlockAndUpdate({24, Y, 20}, S("fire"));
    for (int i = 0; i < 1200; ++i) level->tick();
    EXPECT_EQ(id(20, Y, 20), "air");
    EXPECT_EQ(id(24, Y, 20), "fire");
    // Fire burning out a TNT block lights it.
    level->setBlockAndUpdate({30, Y, 30}, S("tnt"));
    level->setBlockAndUpdate({30, Y + 1, 30}, S("fire"));
    for (int i = 0; i < 2000 && id(30, Y, 30) == "tnt"; ++i) level->tick();
    EXPECT_FALSE(all<PrimedTnt>().empty());
}

TEST_F(SimFixture, FarmlandDriesOutAndIsTrampled) {
    level->setBlockAndUpdate({0, Y - 1, 0}, S("farmland[moisture=2]"));
    level->setBlockAndUpdate({4, Y - 1, 0}, S("farmland[moisture=0]"));
    level->setBlockAndUpdate({4, Y, 0}, S("wheat"));
    forceRandomTicks({0, Y - 1, 0}, {4, Y - 1, 0}, 3);
    EXPECT_EQ(id(0, Y - 1, 0), "dirt"); // dry and bare
    EXPECT_EQ(id(4, Y - 1, 0), "farmland"); // the wheat keeps it
    // Water within 4 wets it.
    level->setBlockAndUpdate({10, Y - 1, 0}, S("farmland"));
    level->setBlockAndUpdate({14, Y - 1, 0}, S("water"));
    forceRandomTicks({10, Y - 1, 0}, {10, Y - 1, 0}, 1);
    EXPECT_EQ(registry->intValue(level->getBlock({10, Y - 1, 0}), "moisture"), 7);
    // A solid block on it, or a fall onto it, makes dirt.
    level->setBlockAndUpdate({10, Y, 0}, S("stone"));
    level->tick();
    level->tick();
    EXPECT_EQ(id(10, Y - 1, 0), "dirt");
    sim::trampleFarmland(*level, {4, Y - 1, 0}, 3.0);
    EXPECT_EQ(id(4, Y - 1, 0), "dirt");
    EXPECT_EQ(id(4, Y, 0), "air"); // the wheat popped off
}

TEST_F(SimFixture, IceMeltsCopperAgesSpongesSoak) {
    level->setBlockAndUpdate({0, Y, 0}, S("ice"));
    level->setBlockAndUpdate({1, Y, 0}, S("glowstone"));
    level->setBlockAndUpdate({6, Y, 0}, S("ice"));
    forceRandomTicks({0, Y, 0}, {6, Y, 0}, 1);
    EXPECT_EQ(id(0, Y, 0), "water");
    EXPECT_EQ(id(6, Y, 0), "ice"); // only in block light above 10 (here 10)
    // Copper ages one stage at a time, keeping its shape.
    level->setBlockAndUpdate({10, Y, 0}, S("cut_copper_stairs[facing=east]"));
    forceRandomTicks({10, Y, 0}, {10, Y, 0}, 3000);
    EXPECT_EQ(id(10, Y, 0), "oxidized_cut_copper_stairs");
    EXPECT_EQ(registry->value(level->getBlock({10, Y, 0}), "facing"), "east");
    level->setBlockAndUpdate({12, Y, 0}, S("waxed_copper_block"));
    forceRandomTicks({12, Y, 0}, {12, Y, 0}, 500);
    EXPECT_EQ(id(12, Y, 0), "waxed_copper_block");
    // A sponge dropped into a pool takes the water around it.
    // A sponge dropped into a pool takes the water within 6 blocks, 64 at most.
    fill(*level, *registry, {21, Y - 2, 21}, {25, Y - 1, 25}, "water");
    level->setBlockAndUpdate({23, Y - 1, 23}, S("sponge"));
    EXPECT_EQ(id(23, Y - 1, 23), "wet_sponge");
    EXPECT_EQ(count({21, Y - 2, 21}, {25, Y - 1, 25}, "water"), 0);
    fill(*level, *registry, {40, Y - 2, 40}, {50, Y - 1, 50}, "water"); // 242 blocks
    level->setBlockAndUpdate({45, Y - 1, 45}, S("sponge"));
    EXPECT_EQ(count({40, Y - 2, 40}, {50, Y - 1, 50}, "water"), 242 - 65);
}

TEST_F(SimFixture, WeatherCyclesAndLightningStrikes) {
    level->setWeather(0, 60, true, true);
    for (int i = 0; i < 30; ++i) level->tick();
    EXPECT_NEAR(level->rainLevel(1.0f), 0.3f, 1e-4f);
    EXPECT_TRUE(level->isRaining());
    EXPECT_FALSE(level->isThundering());
    EXPECT_TRUE(level->isRainingAt({0, Y, 0}));
    EXPECT_FALSE(level->isRainingAt({0, Y - 2, 0})); // underground
    EXPECT_GT(level->skyDarken(), 0); // rain darkens the day
    for (int i = 0; i < 40; ++i) level->tick();
    EXPECT_FALSE(level->raining); // its 60 ticks ran out
    EXPECT_GT(level->rainTime, 0);
    // A bolt on Normal: it strikes the player in the open beside the column (vanilla
    // findLightningTargetAround), hurting them, setting them alight and lighting fire there.
    level->setWeather(0, 100000, true, true);
    for (int i = 0; i < 120; ++i) level->tick();
    ASSERT_TRUE(level->isThundering());
    Player player;
    player.pos = player.oldPos = {2.5, double(Y), 0.5};
    level->setPlayer(&player);
    level->doFireTick = true;
    level->spawnLightning({0, 0, 0}, false);
    ASSERT_EQ(all<LightningBolt>().size(), 1u);
    level->tick();
    level->setPlayer(nullptr);
    EXPECT_EQ(id(2, Y, 0), "fire");
    EXPECT_FLOAT_EQ(player.health, 15.0f);
    EXPECT_GT(player.remainingFireTicks, 0);
    for (int i = 0; i < 40; ++i) level->tick();
    EXPECT_TRUE(all<LightningBolt>().empty());
}

TEST_F(SimFixture, CommandsSummonWeatherAndGameRules) {
    Commands commands;
    CommandSource src;
    src.level = level.get();
    std::string summoned;
    src.summon = [&](std::string_view type, const glm::dvec3&, const Json&, std::string&) {
        summoned = type;
        return true;
    };
    std::string err;
    // TNT and lightning are the level's; items and orbs go to the client's handler.
    ASSERT_TRUE(commands.execute(src, "/summon tnt 0.5 -60 0.5 {fuse:30}", &err)) << err;
    ASSERT_EQ(all<PrimedTnt>().size(), 1u);
    EXPECT_EQ(all<PrimedTnt>()[0]->fuse, 30);
    ASSERT_TRUE(commands.execute(src, "/summon minecraft:lightning_bolt 9 -60 9", &err)) << err;
    EXPECT_EQ(all<LightningBolt>().size(), 1u);
    ASSERT_TRUE(commands.execute(src, "/summon item 0 -60 0 {Item:{id:\"minecraft:stick\"}}", &err)) << err;
    EXPECT_EQ(summoned, "item");
    // Weather with vanilla's durations in ticks, seconds or days.
    ASSERT_TRUE(commands.execute(src, "/weather thunder 30s", &err)) << err;
    EXPECT_TRUE(level->raining);
    EXPECT_TRUE(level->thundering);
    EXPECT_EQ(level->rainTime, 600);
    ASSERT_TRUE(commands.execute(src, "/weather clear 1d", &err)) << err;
    EXPECT_FALSE(level->raining);
    EXPECT_EQ(level->clearWeatherTime, 24000);
    EXPECT_FALSE(commands.execute(src, "/weather snow", &err));
    // The new rules.
    ASSERT_TRUE(commands.execute(src, "/gamerule doFireTick false", &err)) << err;
    EXPECT_FALSE(level->doFireTick);
    ASSERT_TRUE(commands.execute(src, "/gamerule snowAccumulationHeight 5", &err)) << err;
    EXPECT_EQ(level->snowAccumulationHeight, 5);
    ASSERT_TRUE(commands.execute(src, "/gamerule doWeatherCycle false", &err)) << err;
    EXPECT_FALSE(level->doWeatherCycle);
    // Saved in level.json; a world saved in the rain loads raining at full strength.
    ASSERT_TRUE(commands.execute(src, "/weather rain 500", &err)) << err;
    auto loaded = makeLevel();
    loaded->loadData(level->saveData());
    EXPECT_TRUE(loaded->raining);
    EXPECT_EQ(loaded->rainTime, 500);
    EXPECT_FLOAT_EQ(loaded->rainLevel(0.0f), 1.0f);
    EXPECT_FALSE(loaded->doFireTick);
    EXPECT_FALSE(loaded->doWeatherCycle);
    EXPECT_EQ(loaded->snowAccumulationHeight, 5);
}

} // namespace
