// Mobs (M12): path finding, the AI's goals, combat, breeding, spawning and despawning,
// on a flat plains world around the origin with a player standing in it.
#include <unistd.h>

#include <chrono>

#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <queue>

#include <gtest/gtest.h>

#include "core/JobSystem.h"
#include "entity/Arrow.h"
#include "entity/ExperienceOrb.h"
#include "entity/ItemEntity.h"
#include "entity/Player.h"
#include "entity/mob/Animals.h"
#include "entity/mob/Monsters.h"
#include "entity/path/PathFinder.h"
#include "game/Combat.h"
#include "game/Commands.h"
#include "game/Item.h"
#include "resources/Atlas.h"
#include "resources/BlockModels.h"
#include "resources/ModelBaker.h"
#include "resources/ResourcePack.h"
#include "world/biome/Biome.h"
#include "world/block/BlockRegistry.h"
#include "world/gen/ChunkGenerator.h"
#include "world/level/Level.h"
#include "world/level/NaturalSpawner.h"
#include "world/light/LightEngine.h"
#include "world/storage/ChunkStorage.h"

using namespace mc;

namespace {

constexpr int Y = -60; // the first air layer above the flat world's grass

struct MobFixture : ::testing::Test {
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
        types = std::make_unique<EntityTypes>();
        ASSERT_TRUE(types->load(std::string(MC_DEFAULT_DATA_DIR) + "/entities.json", *items, *biomes, &err)) << err;
    }
    static void TearDownTestSuite() {
        types.reset();
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
    std::unique_ptr<Level> makeLevel(std::shared_ptr<ChunkStorage> storage = nullptr, int radius = 3,
                                     bool spawning = false) {
        auto flat = std::make_shared<FlatGenerator>(*registry);
        flat->biome = biomes->require("plains");
        auto l = std::make_unique<Level>(*registry, flat, *jobs, 1);
        if (storage) l->attachStorage(storage);
        l->chunks().setLightEngine(std::make_shared<LightEngine>(*registry));
        l->setItems(items.get());
        l->setEntityTypes(types.get());
        l->doMobSpawning = spawning; // only what the tests spawn, unless asked
        l->chunks().setView({0, 0}, radius);
        for (int i = 0; i < 2000 && !l->chunks().viewReady(); ++i) {
            l->chunks().update();
            jobs->waitIdle();
            jobs->drainMain(1000.0);
        }
        EXPECT_TRUE(l->chunks().viewReady());
        l->randomTickSpeed = 0;
        l->tick(); // takes the newly ticking chunks (animals only with spawning on)
        l->doMobSpawning = false;
        player.pos = player.oldPos = {0.5, double(Y), 0.5};
        l->setPlayer(&player);
        return l;
    }
    void TearDown() override {
        level.reset();
        jobs.reset();
    }

    static StateId S(const char* text) { return *registry->parse(text); }
    std::string id(int x, int y, int z) const { return std::string(shortId(registry->blockOf(level->getBlock({x, y, z})).id)); }
    template <class T>
    T& spawn(const char* type, glm::dvec3 at, float yaw = 0.0f) {
        auto mob = createMob(*types, type);
        EXPECT_TRUE(mob) << type;
        mob->random = Xoroshiro(int64_t(level->random().nextLong()));
        mob->pos = mob->oldPos = at;
        mob->setRotation(yaw, 0.0f);
        SpawnGroup group;
        group.babies = false;
        group.zombieBaby = false;
        mob->finalizeSpawn(*level, SpawnReason::Command, group);
        Entity& e = level->addEntity(std::move(mob));
        return dynamic_cast<T&>(e);
    }
    template <class T>
    std::vector<T*> all() const {
        std::vector<T*> out;
        for (const auto& e : level->entities())
            if (auto* t = dynamic_cast<T*>(e.get()); t && !e->removed) out.push_back(t);
        return out;
    }
    // The world at night, frozen (monsters hunt and don't burn).
    void night() {
        level->daylightCycle = false;
        level->setDayTime(18000);
    }
    // A tick of the world with the player standing still in it.
    void tick() {
        player.tick(*level, PlayerInput{});
        player.survivalTick(*level, level->difficulty, true);
        level->tick();
    }
    void fill(BlockPos a, BlockPos b, const char* block) {
        for (int y = a.y; y <= b.y; ++y)
            for (int z = a.z; z <= b.z; ++z)
                for (int x = a.x; x <= b.x; ++x) level->setBlock({x, y, z}, S(block));
    }

    static inline std::unique_ptr<ResourcePack> pack;
    static inline std::unique_ptr<Atlas> atlas;
    static inline std::unique_ptr<ModelStore> store;
    static inline std::unique_ptr<ModelBaker> baker;
    static inline std::unique_ptr<BlockModels> models;
    static inline std::unique_ptr<BlockRegistry> registry;
    static inline std::unique_ptr<ItemRegistry> items;
    static inline std::unique_ptr<BiomeRegistry> biomes;
    static inline std::unique_ptr<EntityTypes> types;
    std::unique_ptr<JobSystem> jobs;
    std::unique_ptr<Level> level;
    Player player;
};

// The shortest 4-connected route through a maze by breadth-first search (the path finder
// may cut diagonals, so its node count is at most this).
int bfsSteps(const std::vector<std::string>& maze, glm::ivec2 from, glm::ivec2 to) {
    std::map<std::pair<int, int>, int> dist;
    std::queue<glm::ivec2> q;
    q.push(from);
    dist[{from.x, from.y}] = 0;
    while (!q.empty()) {
        const glm::ivec2 c = q.front();
        q.pop();
        if (c == to) return dist[{c.x, c.y}];
        for (const glm::ivec2 d : {glm::ivec2(1, 0), glm::ivec2(-1, 0), glm::ivec2(0, 1), glm::ivec2(0, -1)}) {
            const glm::ivec2 n = c + d;
            if (n.y < 0 || n.y >= int(maze.size()) || n.x < 0 || n.x >= int(maze[0].size()) || maze[size_t(n.y)][size_t(n.x)] == '#')
                continue;
            if (dist.contains({n.x, n.y})) continue;
            dist[{n.x, n.y}] = dist[{c.x, c.y}] + 1;
            q.push(n);
        }
    }
    return -1;
}

} // namespace

TEST_F(MobFixture, PathFinderSolvesAMaze) {
    // Walls two blocks high; the route winds back and forth.
    const std::vector<std::string> maze = {
        "###########",
        "#S..#.....#",
        "###.#.###.#",
        "#...#...#.#",
        "#.#####.#.#",
        "#.....#.#.#",
        "#####.#.#.#",
        "#.....#...#",
        "#.#######.#",
        "#........G#",
        "###########",
    };
    glm::ivec2 start{}, goal{};
    for (int z = 0; z < int(maze.size()); ++z)
        for (int x = 0; x < int(maze[0].size()); ++x) {
            if (maze[size_t(z)][size_t(x)] == '#') fill({x, Y, z}, {x, Y + 1, z}, "stone");
            if (maze[size_t(z)][size_t(x)] == 'S') start = {x, z};
            if (maze[size_t(z)][size_t(x)] == 'G') goal = {x, z};
        }
    PathMob mob;
    mob.pos = {start.x + 0.5, double(Y), start.y + 0.5};
    mob.width = 0.6f;
    mob.height = 1.95f;
    const BlockPos target{goal.x, Y, goal.y};
    const auto path = findPath(*level, mob, std::span(&target, 1), 64.0f, 0, 1024);
    ASSERT_TRUE(path);
    EXPECT_TRUE(path->reached);
    EXPECT_EQ(path->endNode().pos(), target);
    const int steps = bfsSteps(maze, start, goal);
    ASSERT_GT(steps, 0);
    EXPECT_LE(int(path->size()) - 1, steps);
    // Every node is open floor, never inside a wall, each a step from the last.
    for (size_t i = 0; i < path->size(); ++i) {
        const PathPoint& n = path->nodes[i];
        EXPECT_EQ(n.y, Y);
        EXPECT_NE(maze[size_t(n.z)][size_t(n.x)], '#') << n.x << "," << n.z;
        if (i > 0) {
            const PathPoint& p = path->nodes[i - 1];
            EXPECT_LE(std::abs(n.x - p.x) + std::abs(n.z - p.z), 2);
        }
    }
    // Walled in, there is no way: the path ends as close as it gets, not reached.
    fill({goal.x - 1, Y, goal.y - 1}, {goal.x - 1, Y + 1, goal.y - 1}, "stone");
    fill({goal.x, Y, goal.y - 1}, {goal.x, Y + 1, goal.y - 1}, "stone");
    fill({goal.x - 1, Y, goal.y}, {goal.x - 1, Y + 1, goal.y}, "stone");
    const auto blocked = findPath(*level, mob, std::span(&target, 1), 64.0f, 0, 1024);
    ASSERT_TRUE(blocked);
    EXPECT_FALSE(blocked->reached);
}

TEST_F(MobFixture, PathsStepUpOneBlockAndAvoidFencesAndDrops) {
    PathMob mob;
    mob.pos = {0.5, double(Y), 0.5};
    // A one-block step is climbed; a two-block wall is walked around.
    fill({2, Y, -3}, {2, Y, 3}, "stone");
    BlockPos target{4, Y, 0};
    auto path = findPath(*level, mob, std::span(&target, 1), 32.0f, 0, 512);
    ASSERT_TRUE(path && path->reached);
    bool stepped = false;
    for (const PathPoint& n : path->nodes) stepped = stepped || (n.x == 2 && n.y == Y + 1);
    EXPECT_TRUE(stepped);
    // Fences (1.5 high) are never jumped: the way round.
    fill({2, Y, -3}, {2, Y, 3}, "air");
    fill({2, Y, -6}, {2, Y, 6}, "oak_fence");
    path = findPath(*level, mob, std::span(&target, 1), 32.0f, 0, 512);
    ASSERT_TRUE(path && path->reached);
    for (const PathPoint& n : path->nodes) EXPECT_FALSE(n.x == 2 && std::abs(n.z) <= 6) << n.z;
    // A pit deeper than 3 is not dropped into (without a target worth the fall).
    fill({2, Y, -6}, {2, Y, 6}, "air");
    fill({-8, Y - 4, -8}, {-6, Y - 1, -6}, "air");
    target = {-7, Y - 4, -7};
    path = findPath(*level, mob, std::span(&target, 1), 32.0f, 0, 512);
    if (path) {
        EXPECT_FALSE(path->reached);
    }
    EXPECT_EQ(mobPathType(*level, mob, {0, Y, 0}), PathType::Walkable);
    EXPECT_EQ(mobPathType(*level, mob, {0, Y - 1, 0}), PathType::Blocked);
}

TEST_F(MobFixture, ZombieClosesTheGapAndHits) {
    night();
    Zombie& z = spawn<Zombie>("zombie", {10.5, double(Y), 0.5});
    int ticks = 0;
    while (player.health == 20.0f && ticks < 600) {
        tick();
        ++ticks;
    }
    EXPECT_LT(ticks, 600) << "zombie at " << z.pos.x << "," << z.pos.z;
    EXPECT_LT(player.health, 20.0f);
    // Normal difficulty: 3 damage per hit (no armor), from the zombie.
    EXPECT_FLOAT_EQ(20.0f - player.health, 3.0f);
    EXPECT_EQ(player.lastDamage, DamageType::MobAttack);
    EXPECT_EQ(player.lastAttackerType, "minecraft:zombie");
    EXPECT_EQ(z.targetId, player.id);
    // The hit knocked the player back, away from the zombie.
    EXPECT_GT(std::abs(player.velocity.x) + std::abs(player.velocity.z), 0.0);
    // It walked about 9 blocks at the zombie's 2.3 m/s, give or take path finding and the
    // every-other-tick goals.
    EXPECT_GT(ticks, 60);
}

TEST_F(MobFixture, CreeperSwellsAndExplodes) {
    night();
    Creeper& c = spawn<Creeper>("creeper", {4.5, double(Y), 0.5});
    const uint32_t creeperId = c.id;
    int ticks = 0;
    bool swelled = false;
    while (level->entity(creeperId) && ticks < 400) {
        if (auto* live = dynamic_cast<Creeper*>(level->entity(creeperId))) swelled = swelled || live->swell > 0;
        tick();
        ++ticks;
    }
    EXPECT_FALSE(level->entity(creeperId));
    EXPECT_TRUE(swelled);
    EXPECT_LT(player.health, 20.0f);
    EXPECT_EQ(player.lastDamage, DamageType::PlayerExplosion);
    EXPECT_EQ(player.deathMessageKey(), "death.attack.explosion.player");
    // A crater in the grass around where it stood (power 3).
    int removed = 0;
    for (int x = -4; x <= 8; ++x)
        for (int z = -6; z <= 6; ++z) removed += id(x, Y - 1, z) == "air";
    EXPECT_GT(removed, 5);
    // Without mob griefing the blast hurts but breaks nothing.
    level->mobGriefing = false;
    player.health = 20.0f;
    player.invulnerableTime = 0;
    Creeper& d = spawn<Creeper>("creeper", {-30.5, double(Y), 0.5});
    d.ignited = true;
    for (int i = 0; i < 40; ++i) tick();
    for (int x = -34; x <= -27; ++x)
        for (int z = -3; z <= 3; ++z) EXPECT_EQ(id(x, Y - 1, z), "grass_block") << x << "," << z;
}

TEST_F(MobFixture, SkeletonShootsAndMostlyHits) {
    night();
    Skeleton& s = spawn<Skeleton>("skeleton", {10.5, double(Y), 0.5});
    ASSERT_TRUE(s.holdingBow());
    int hits = 0, shots = 0;
    std::vector<uint32_t> seen;
    for (int t = 0; t < 1200; ++t) {
        const float before = player.health;
        tick();
        if (player.health < before) ++hits;
        for (Arrow* a : all<Arrow>())
            if (std::find(seen.begin(), seen.end(), a->id) == seen.end()) {
                seen.push_back(a->id);
                ++shots;
            }
        player.health = 20.0f; // kept alive
        if (player.invulnerableTime > 0) player.invulnerableTime = 0;
    }
    // Every 2 seconds (40 ticks between, 20 drawing) on Normal, after it has looked a while.
    EXPECT_GE(shots, 10);
    EXPECT_LE(shots, 25);
    // Normal's spread (inaccuracy 6) at 10 blocks lands about half of them.
    EXPECT_GE(double(hits) / double(shots), 0.35) << hits << "/" << shots;
    EXPECT_EQ(player.lastDamage, DamageType::Arrow);
    // Its arrows can't be picked up.
    for (Arrow* a : all<Arrow>()) EXPECT_EQ(a->pickup, Arrow::Pickup::Disallowed);
}

TEST_F(MobFixture, BabiesGrowUpInTwentyMinutes) {
    Pig& pig = spawn<Pig>("pig", {5.5, double(Y), 5.5});
    pig.setAge(Animal::kBabyAge);
    EXPECT_TRUE(pig.isBaby());
    EXPECT_FLOAT_EQ(pig.width, 0.45f);
    level->daylightCycle = false;
    for (int t = 0; t < 23999; ++t) level->tick();
    EXPECT_TRUE(pig.isBaby());
    level->tick();
    EXPECT_FALSE(pig.isBaby());
    EXPECT_EQ(pig.age, 0);
    EXPECT_FLOAT_EQ(pig.width, 0.9f);
    EXPECT_FLOAT_EQ(pig.height, 0.9f);
}

TEST_F(MobFixture, FedAnimalsBreed) {
    Cow& a = spawn<Cow>("cow", {5.5, double(Y), 5.5});
    Cow& b = spawn<Cow>("cow", {7.5, double(Y), 5.5});
    ItemStack wheat{items->find("wheat"), 3};
    EXPECT_TRUE(a.interact(*level, player, wheat));
    EXPECT_TRUE(b.interact(*level, player, wheat));
    EXPECT_EQ(wheat.count, 1);
    EXPECT_GT(a.inLove, 0);
    // A cow won't eat in love again, nor anything but wheat.
    EXPECT_FALSE(a.interact(*level, player, wheat));
    ItemStack seeds{items->find("wheat_seeds"), 1};
    EXPECT_FALSE(b.interact(*level, player, seeds));
    int ticks = 0;
    while (all<Cow>().size() < 3 && ticks < 400) {
        level->tick();
        ++ticks;
    }
    ASSERT_EQ(all<Cow>().size(), 3u);
    int babies = 0;
    for (Cow* c : all<Cow>()) babies += c->isBaby();
    EXPECT_EQ(babies, 1);
    EXPECT_GE(a.age, 5990); // 6000, counting down
    EXPECT_GE(b.age, 5990);
    EXPECT_FALSE(all<ExperienceOrb>().empty());
    // Feeding the calf speeds it up by a tenth of what is left.
    Cow* calf = nullptr;
    for (Cow* c : all<Cow>())
        if (c->isBaby()) calf = c;
    const int before = calf->age;
    ItemStack more{items->find("wheat"), 1};
    EXPECT_TRUE(calf->interact(*level, player, more));
    EXPECT_EQ(calf->age, before + (-before / 20) / 10 * 20);
}

TEST_F(MobFixture, SheepAreShearedDyedAndRegrowEatingGrass) {
    Sheep& s = spawn<Sheep>("sheep", {3.5, double(Y), 3.5});
    s.color = 0;
    ItemStack dye{items->find("red_dye"), 1};
    EXPECT_TRUE(s.interact(*level, player, dye));
    EXPECT_EQ(s.color, 14);
    EXPECT_TRUE(dye.empty());
    ItemStack shears{items->find("shears"), 1};
    EXPECT_TRUE(s.interact(*level, player, shears));
    EXPECT_TRUE(s.sheared);
    EXPECT_EQ(shears.damage, 1);
    int wool = 0;
    for (ItemEntity* e : all<ItemEntity>()) {
        EXPECT_EQ(e->stack.item->id, "minecraft:red_wool");
        wool += e->stack.count;
    }
    EXPECT_GE(wool, 1);
    EXPECT_LE(wool, 3);
    EXPECT_FALSE(s.interact(*level, player, shears)); // nothing left to shear
    // It grazes now and then (1 in 1000 checks, every other tick): the wool grows back and
    // the grass under it turns to dirt.
    int ticks = 0;
    while (s.sheared && ticks < 30000) {
        level->tick();
        ++ticks;
    }
    EXPECT_FALSE(s.sheared);
    int dirt = 0;
    for (int x = -20; x <= 20; ++x)
        for (int z = -20; z <= 20; ++z) dirt += id(x, Y - 1, z) == "dirt";
    EXPECT_EQ(dirt, 1);
}

TEST_F(MobFixture, ChickensLayEggs) {
    Chicken& c = spawn<Chicken>("chicken", {2.5, double(Y), 2.5});
    EXPECT_GE(c.eggTime, 6000);
    EXPECT_LT(c.eggTime, 12000);
    c.eggTime = 5;
    for (int t = 0; t < 10; ++t) level->tick();
    bool egg = false;
    for (ItemEntity* e : all<ItemEntity>()) egg = egg || e->stack.item->id == "minecraft:egg";
    EXPECT_TRUE(egg);
    EXPECT_GE(c.eggTime, 5990);
    // Chickens flutter down: no fall damage, falling slowed.
    Chicken& high = spawn<Chicken>("chicken", {-3.5, double(Y + 20), -3.5});
    double fastest = 0.0;
    for (int t = 0; t < 200 && !high.onGround; ++t) {
        level->tick();
        fastest = std::max(fastest, -high.velocity.y);
    }
    EXPECT_TRUE(high.onGround);
    EXPECT_FLOAT_EQ(high.health, 4.0f);
    EXPECT_LT(fastest, 0.35);
}

TEST_F(MobFixture, SpawnLightRules) {
    Xoroshiro r(42);
    const EntityTypeInfo& zombie = *types->find("zombie");
    const EntityTypeInfo& cow = *types->find("cow");
    // By day the open sky is too bright for monsters; animals spawn on lit grass.
    level->setDayTime(6000);
    const BlockPos open{20, Y, 20};
    int monsterOk = 0;
    for (int i = 0; i < 100; ++i) monsterOk += spawner::checkSpawnRules(*level, zombie, open, r);
    EXPECT_EQ(monsterOk, 0);
    EXPECT_TRUE(spawner::checkSpawnRules(*level, cow, open, r));
    EXPECT_TRUE(spawner::isSpawnPositionOk(*level, open));
    // At night: monsters, sometimes (the darkness test is random); never beside a torch.
    level->setDayTime(18000);
    int dark = 0;
    for (int i = 0; i < 200; ++i) dark += spawner::checkSpawnRules(*level, zombie, open, r);
    EXPECT_GT(dark, 20);
    level->setBlockAndUpdate({22, Y, 20}, S("torch"));
    for (int i = 0; i < 20; ++i) level->tick(); // light updates
    for (int i = 0; i < 100; ++i) EXPECT_FALSE(spawner::checkSpawnRules(*level, zombie, open, r));
    // Not in water, not on glass or leaves, not in a 1-high gap.
    level->setBlock({30, Y - 1, 30}, S("glass"));
    EXPECT_FALSE(spawner::isSpawnPositionOk(*level, {30, Y, 30}));
    level->setBlock({32, Y, 30}, S("water"));
    EXPECT_FALSE(spawner::isSpawnPositionOk(*level, {32, Y, 30}));
    level->setBlock({34, Y + 1, 30}, S("stone"));
    EXPECT_FALSE(spawner::isSpawnPositionOk(*level, {34, Y, 30}));
    // Animals need grass: not on stone.
    level->setBlock({36, Y - 1, 30}, S("stone"));
    level->setDayTime(6000);
    EXPECT_FALSE(spawner::checkSpawnRules(*level, cow, {36, Y, 30}, r));
}

TEST_F(MobFixture, NaturalSpawningKeepsToTheCapsAndDespawns) {
    level = makeLevel(nullptr, 5);
    level->doMobSpawning = true;
    night();
    const int cap = spawner::localCap(*level, MobCategory::Monster);
    ASSERT_GT(cap, 5);
    int most = 0;
    for (int t = 0; t < 3000; ++t) {
        level->tick();
        const int n = spawner::mobCount(*level, MobCategory::Monster);
        most = std::max(most, n);
        ASSERT_LE(n, cap);
    }
    EXPECT_GE(most, cap / 2);
    // Animals too, every 400 ticks, within their own small cap.
    EXPECT_LE(spawner::mobCount(*level, MobCategory::Creature), spawner::localCap(*level, MobCategory::Creature));
    // By day the zombies and skeletons burn; on Peaceful every monster is gone at once.
    level->difficulty = Difficulty::Peaceful;
    level->tick();
    EXPECT_EQ(spawner::mobCount(*level, MobCategory::Monster), 0);
    // Far from the player (over 128 blocks), monsters despawn; animals stay.
    level->difficulty = Difficulty::Normal;
    level->doMobSpawning = false;
    Zombie& z = spawn<Zombie>("zombie", {20.5, double(Y), 20.5});
    Cow& c = spawn<Cow>("cow", {22.5, double(Y), 20.5});
    const uint32_t zid = z.id, cid = c.id;
    player.pos = {200.5, double(Y), 0.5};
    level->tick();
    EXPECT_FALSE(level->entity(zid));
    EXPECT_TRUE(level->entity(cid));
}

TEST_F(MobFixture, ADayOfSpawningAndDespawning) {
    // A full day from dusk: monsters build up to the cap at night (never past it), and by
    // noon the sun and the despawning have thinned them out.
    level = makeLevel(nullptr, 5);
    level->doMobSpawning = true;
    level->setDayTime(13000);
    const int cap = spawner::localCap(*level, MobCategory::Monster);
    int nightPeak = 0, noon = -1;
    for (int t = 0; t < 24000; ++t) {
        level->tick();
        const int n = spawner::mobCount(*level, MobCategory::Monster);
        ASSERT_LE(n, cap) << "tick " << t;
        const int64_t time = level->dayTime() % 24000;
        if (time >= 14000 && time < 22000) nightPeak = std::max(nightPeak, n);
        if (time == 6000) noon = n;
    }
    EXPECT_GE(nightPeak, cap * 3 / 4);
    EXPECT_GE(noon, 0);
    EXPECT_LT(noon, nightPeak);
    EXPECT_LE(spawner::mobCount(*level, MobCategory::Creature), 4 * spawner::localCap(*level, MobCategory::Creature));
}

TEST_F(MobFixture, NewChunksStartWithAnimals) {
    // Plains generate animals in about one chunk in ten, packs of 4 of one kind.
    level = makeLevel(nullptr, 8, true);
    std::map<std::string, int> kinds;
    for (Mob* m : all<Mob>()) {
        EXPECT_EQ(m->category(), MobCategory::Creature);
        ++kinds[std::string(m->type())];
    }
    int total = 0;
    for (const auto& [k, n] : kinds) total += n;
    EXPECT_GT(total, 8);
    EXPECT_LT(total, 300);
    EXPECT_GE(kinds.size(), 2u);
}

TEST_F(MobFixture, UndeadBurnInDaylight) {
    level->daylightCycle = false;
    level->setDayTime(6000);
    Zombie& bare = spawn<Zombie>("zombie", {6.5, double(Y), 6.5});
    Zombie& helmeted = spawn<Zombie>("zombie", {-6.5, double(Y), -6.5});
    helmeted.setItemSlot(EquipmentSlot::Head, ItemStack{items->find("iron_helmet"), 1});
    bool burned = false;
    for (int t = 0; t < 300 && !burned; ++t) {
        level->tick();
        burned = bare.onFire();
    }
    EXPECT_TRUE(burned);
    EXPECT_FALSE(helmeted.onFire());
    EXPECT_GT(helmeted.equipment[size_t(EquipmentSlot::Head)].damage, 0);
    // The fire takes it down (1 damage a second, 20 health), unless it finds water or shade.
    for (int t = 0; t < 900 && bare.alive(); ++t) level->tick();
    EXPECT_TRUE(bare.dead() || bare.health < 20.0f);
}

TEST_F(MobFixture, PlayerKillsDropLootAndExperience) {
    // Far from the player (who would pick the drops up).
    Cow& cow = spawn<Cow>("cow", {20.5, double(Y), 0.5});
    const uint32_t cid = cow.id;
    // A player's kill (iron sword, 6 damage, cow 10 health: two hits a second apart).
    cow.hurtServer(*level, DamageSource::playerAttack(player), 6.0f);
    for (int t = 0; t < 20; ++t) level->tick();
    cow.hurtServer(*level, DamageSource::playerAttack(player), 6.0f);
    EXPECT_TRUE(cow.dead());
    for (int t = 0; t < 21; ++t) level->tick();
    EXPECT_FALSE(level->entity(cid));
    bool beef = false;
    for (ItemEntity* e : all<ItemEntity>()) beef = beef || e->stack.item->id == "minecraft:beef";
    EXPECT_TRUE(beef);
    EXPECT_FALSE(all<ExperienceOrb>().empty());
    // A death in fire drops it cooked; one the player had nothing to do with gives no
    // experience.
    for (ItemEntity* e : all<ItemEntity>()) e->removed = true;
    for (ExperienceOrb* o : all<ExperienceOrb>()) o->removed = true;
    level->tick();
    Pig& pig = spawn<Pig>("pig", {-20.5, double(Y), -4.5});
    const uint32_t pid = pig.id;
    pig.remainingFireTicks = 200;
    pig.hurtServer(*level, DamageType::Lava, 20.0f);
    bool cooked = false;
    for (ItemEntity* e : all<ItemEntity>()) cooked = cooked || e->stack.item->id == "minecraft:cooked_porkchop";
    EXPECT_TRUE(cooked);
    EXPECT_TRUE(all<ExperienceOrb>().empty());
    // The dead fall over and vanish after a second.
    for (int t = 0; t < 25; ++t) level->tick();
    EXPECT_FALSE(level->entity(pid));
}

TEST_F(MobFixture, MobsSurviveSaveAndReload) {
    auto dir = std::filesystem::temp_directory_path() / ("mc_mobs_" + std::to_string(::getpid()));
    std::filesystem::remove_all(dir);
    auto storage = std::make_shared<ChunkStorage>(dir, *registry, nullptr);
    level = makeLevel(storage);
    Sheep& s = spawn<Sheep>("sheep", {3.5, double(Y), 3.5});
    s.color = 11;
    s.sheared = true;
    s.setAge(-1000);
    s.health = 5.0f;
    Zombie& z = spawn<Zombie>("zombie", {-3.5, double(Y), 3.5});
    z.setItemSlot(EquipmentSlot::MainHand, ItemStack{items->find("iron_sword"), 1});
    z.setBaby(true);
    Creeper& c = spawn<Creeper>("creeper", {3.5, double(Y), -3.5});
    c.powered = true;
    level->chunks().saveAll();
    while (level->chunks().savesPending()) {
        jobs->waitIdle();
        jobs->drainMain(1000.0);
    }
    level.reset();
    level = makeLevel(storage);
    std::map<std::string, Mob*> byType;
    for (Mob* m : all<Mob>()) byType[std::string(m->type())] = m;
    ASSERT_EQ(byType.size(), 3u);
    auto* s2 = dynamic_cast<Sheep*>(byType["minecraft:sheep"]);
    auto* z2 = dynamic_cast<Zombie*>(byType["minecraft:zombie"]);
    auto* c2 = dynamic_cast<Creeper*>(byType["minecraft:creeper"]);
    ASSERT_TRUE(s2 && z2 && c2);
    EXPECT_EQ(s2->color, 11);
    EXPECT_TRUE(s2->sheared);
    EXPECT_NEAR(s2->age, -1000, 2); // a tick or two of growing up on either side
    EXPECT_FLOAT_EQ(s2->health, 5.0f);
    EXPECT_NEAR(s2->pos.x, 3.5, 0.01);
    EXPECT_TRUE(z2->isBaby());
    EXPECT_EQ(z2->mainHand().item->id, "minecraft:iron_sword");
    EXPECT_TRUE(c2->powered);
    level.reset();
    std::filesystem::remove_all(dir);
}

TEST_F(MobFixture, PlayerArrowsFlyFallAndArePickedUp) {
    auto arrow = std::make_unique<Arrow>();
    arrow->pos = arrow->oldPos = {0.5, double(Y) + 1.5, 0.5};
    arrow->owner = player.id;
    arrow->pickup = Arrow::Pickup::Allowed;
    arrow->pickupItem = ItemStack{items->find("arrow"), 1};
    Xoroshiro r(3);
    arrow->shoot({1.0, 0.0, 0.0}, 3.0f, 0.0f, r);
    Arrow& a = static_cast<Arrow&>(level->addEntity(std::move(arrow)));
    for (int t = 0; t < 100 && !a.inGround; ++t) level->tick();
    EXPECT_TRUE(a.inGround);
    EXPECT_GT(a.pos.x, 10.0); // a full-power arrow flies far before it falls
    EXPECT_NEAR(a.pos.y, double(Y), 0.1);
    EXPECT_LT(a.pitch, 0.0f); // nose down: vanilla's arrow pitch is atan2(vy, horizontal)
    // Walking over it picks it up once it stops shaking.
    player.pos = a.pos;
    for (int t = 0; t < 10; ++t) tick();
    EXPECT_TRUE(all<Arrow>().empty());
    int arrows = 0;
    for (const ItemStack& st : player.inventory.main)
        if (!st.empty() && st.item->id == "minecraft:arrow") arrows += st.count;
    EXPECT_EQ(arrows, 1);
}

TEST_F(MobFixture, PlayerAttacksScaleWithTheCooldownAndCrit) {
    player.inventory.selectedStack() = ItemStack{items->find("iron_sword"), 1};
    Cow& a = spawn<Cow>("cow", {2.0, double(Y), 0.5});
    // Full strength (an iron sword recovers in 12.5 ticks): 6 damage, the cow pushed away.
    player.attackStrengthTicker = 100;
    player.onGround = true;
    player.oldPos = player.pos;
    AttackResult r = playerAttack(*level, player, a);
    EXPECT_TRUE(r.hurt);
    EXPECT_FALSE(r.critical);
    EXPECT_FLOAT_EQ(a.health, 4.0f);
    EXPECT_GT(a.velocity.x, 0.0);
    EXPECT_EQ(player.inventory.selectedStack().damage, 1); // swords wear by 1
    EXPECT_EQ(player.attackStrengthTicker, 0);
    // Right after, the cooldown leaves 0.2 + 0.04^2 x 0.8 of it (and the invulnerability
    // window swallows it anyway).
    Cow& b = spawn<Cow>("cow", {-2.0, double(Y), 0.5});
    r = playerAttack(*level, player, b);
    EXPECT_NEAR(r.damage, 6.0f * (0.2f + 0.04f * 0.04f * 0.8f), 1e-4f);
    // Falling: a critical hit, half again.
    Cow& c = spawn<Cow>("cow", {0.5, double(Y), 2.0});
    player.attackStrengthTicker = 100;
    player.onGround = false;
    player.fallDistance = 1.0;
    r = playerAttack(*level, player, c);
    EXPECT_TRUE(r.critical);
    EXPECT_FLOAT_EQ(r.damage, 9.0f);
    EXPECT_FLOAT_EQ(c.health, 1.0f);
}

TEST_F(MobFixture, SwordSweepsHitNeighbours) {
    player.inventory.selectedStack() = ItemStack{items->find("diamond_sword"), 1};
    player.onGround = true;
    player.oldPos = player.pos;
    player.attackStrengthTicker = 100;
    Cow& target = spawn<Cow>("cow", {0.5, double(Y), 2.0});
    Cow& beside = spawn<Cow>("cow", {1.5, double(Y), 2.2});
    Cow& far = spawn<Cow>("cow", {0.5, double(Y), 6.0});
    const AttackResult r = playerAttack(*level, player, target);
    EXPECT_TRUE(r.sweep);
    EXPECT_FLOAT_EQ(target.health, 3.0f); // 7
    EXPECT_FLOAT_EQ(beside.health, 9.0f); // the sweep's 1
    EXPECT_FLOAT_EQ(far.health, 10.0f);
    // An axe doesn't sweep (and wears by 2).
    player.inventory.selectedStack() = ItemStack{items->find("iron_axe"), 1};
    player.attackStrengthTicker = 100;
    Cow& d = spawn<Cow>("cow", {-4.0, double(Y), 0.5});
    EXPECT_FALSE(playerAttack(*level, player, d).sweep);
    EXPECT_EQ(player.inventory.selectedStack().damage, 2);
}

TEST_F(MobFixture, SpawnEggsSpawnMobsAndBabies) {
    const Item& egg = *items->find("pig_spawn_egg");
    // On the top of the grass: a pig standing on it.
    Mob* pig = spawnFromEgg(*level, egg, {3, Y - 1, 3}, Direction::Up);
    ASSERT_TRUE(pig);
    EXPECT_EQ(pig->type(), "minecraft:pig");
    EXPECT_NEAR(pig->pos.y, double(Y), 1e-6);
    EXPECT_NEAR(pig->pos.x, 3.5, 1e-6);
    // On a pig: a piglet.
    ItemStack held{&egg, 2};
    EXPECT_TRUE(pig->interact(*level, player, held));
    EXPECT_EQ(held.count, 1);
    int babies = 0;
    for (Pig* p : all<Pig>()) babies += p->isBaby();
    EXPECT_EQ(babies, 1);
    // A cow egg does nothing on a pig.
    ItemStack cowEgg{items->find("cow_spawn_egg"), 1};
    EXPECT_FALSE(pig->interact(*level, player, cowEgg));
}

TEST_F(MobFixture, SummonAndKillCommands) {
    Commands commands;
    CommandSource src;
    src.level = level.get();
    std::string err;
    ASSERT_TRUE(commands.execute(src, "/summon zombie 3 -60 3 {NoAI:1b,equipment:{mainhand:{id:\"minecraft:iron_sword\"}}}", &err))
        << err;
    ASSERT_TRUE(commands.execute(src, "/summon sheep 5 -60 3 {Color:14,Sheared:1b}", &err)) << err;
    ASSERT_TRUE(commands.execute(src, "/summon cow -3 -60 3", &err)) << err;
    ASSERT_EQ(all<Mob>().size(), 3u);
    Zombie* z = all<Zombie>().front();
    EXPECT_TRUE(z->noAi);
    EXPECT_EQ(z->mainHand().item->id, "minecraft:iron_sword");
    EXPECT_FLOAT_EQ(z->attackDamageWithWeapon(), 3.0f + 5.0f);
    EXPECT_EQ(all<Sheep>().front()->color, 14);
    EXPECT_TRUE(all<Sheep>().front()->sheared);
    // Killing by type: the zombie dies (and later vanishes); the rest live.
    ASSERT_TRUE(commands.execute(src, "/kill @e[type=zombie]", &err)) << err;
    EXPECT_TRUE(z->dead());
    EXPECT_FALSE(all<Sheep>().front()->dead());
    ASSERT_TRUE(commands.execute(src, "/kill @e[type=!cow]", &err)) << err; // the sheep (no player here)
    EXPECT_TRUE(all<Sheep>().front()->dead());
    EXPECT_FALSE(all<Cow>().front()->dead());
    EXPECT_FALSE(commands.execute(src, "/kill @e[type=creeper]", &err));
    ASSERT_TRUE(commands.execute(src, "/gamerule mobGriefing false", &err)) << err;
    EXPECT_FALSE(level->mobGriefing);
}

TEST_F(MobFixture, DISABLED_BenchSeventyZombies) {
    night();
    Xoroshiro r(9);
    for (int i = 0; i < 70; ++i)
        spawn<Zombie>("zombie", {r.nextInt(80) - 40 + 0.5, double(Y), r.nextInt(80) - 40 + 0.5});
    const auto t0 = std::chrono::steady_clock::now();
    for (int t = 0; t < 400; ++t) {
        tick();
        player.health = 20.0f;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    printf("70 zombies: %.3f ms per tick\n", ms / 400.0);
}
