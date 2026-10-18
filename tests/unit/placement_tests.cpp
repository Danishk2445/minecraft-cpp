#include <map>
#include <unistd.h>
#include <memory>
#include <set>

#include <gtest/gtest.h>

#include "core/JobSystem.h"
#include "entity/Player.h"
#include "game/CreativeTabs.h"
#include "game/Inventory.h"
#include "game/Item.h"
#include "game/Placement.h"
#include "game/Tools.h"
#include "resources/Language.h"
#include "resources/Atlas.h"
#include "resources/BlockModels.h"
#include "resources/ResourcePack.h"
#include "world/block/BlockBehavior.h"
#include "world/block/BlockRegistry.h"
#include "world/gen/ChunkGenerator.h"
#include "entity/ExperienceOrb.h"
#include "entity/ItemEntity.h"
#include "game/Damage.h"
#include "game/BlockEntities.h"
#include "game/Commands.h"
#include "game/FoodData.h"
#include "game/Recipes.h"
#include "world/storage/ChunkStorage.h"
#include "game/Loot.h"
#include "world/level/Level.h"

using namespace mc;

namespace {

// A flat world (grass at y = -61) with every chunk around the origin ready for edits.
struct PlacementFixture : ::testing::Test {
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
    }
    static void TearDownTestSuite() {
        registry.reset();
        models.reset();
        baker.reset();
        store.reset();
        atlas.reset();
        pack.reset();
    }
    void SetUp() override {
        jobs = std::make_unique<JobSystem>(0);
        level = std::make_unique<Level>(*registry, std::make_shared<FlatGenerator>(*registry), *jobs, 1);
        level->chunks().setView({0, 0}, 2);
        for (int i = 0; i < 1000 && !level->chunks().viewReady(); ++i) {
            level->chunks().update();
            jobs->waitIdle();
        }
        ASSERT_TRUE(level->chunks().viewReady());
    }
    void TearDown() override {
        level.reset();
        jobs.reset();
    }

    static StateId S(const char* text) { return *registry->parse(text); }
    std::string at(int x, int y, int z) const { return registry->describe(level->getBlock({x, y, z})); }
    std::string value(int x, int y, int z, const char* prop) const {
        return std::string(registry->value(level->getBlock({x, y, z}), prop));
    }

    // Places `block` as if clicking the top face of the block below `pos` (or `face` of
    // the block behind it), the player looking along `yaw`.
    bool place(const char* block, BlockPos pos, float yaw = 0.0f, Direction face = Direction::Up, double hitY = 0.0) {
        PlaceContext ctx;
        ctx.pos = pos;
        ctx.clickedFace = face;
        ctx.yaw = yaw;
        const BlockPos clicked = pos.offset(opposite(face));
        ctx.hitPoint = glm::dvec3(clicked.vec()) + glm::dvec3(0.5) + glm::dvec3(normal(face)) * 0.5;
        if (axisOf(face) != Axis::Y) ctx.hitPoint.y = clicked.y + hitY;
        const auto blocks = placementStates(*registry, *level, *registry->find(block), ctx);
        for (const auto& [p, s] : blocks) level->setBlockAndUpdate(p, s);
        return !blocks.empty();
    }

    static inline std::unique_ptr<ResourcePack> pack;
    static inline std::unique_ptr<Atlas> atlas;
    static inline std::unique_ptr<ModelStore> store;
    static inline std::unique_ptr<ModelBaker> baker;
    static inline std::unique_ptr<BlockModels> models;
    static inline std::unique_ptr<BlockRegistry> registry;
    std::unique_ptr<JobSystem> jobs;
    std::unique_ptr<Level> level;
};

constexpr int Y = -60; // first air layer above the flat world's grass

} // namespace

TEST_F(PlacementFixture, FacingAndAxis) {
    // Yaw 0 looks south: furnaces face the player (north), stairs climb away (south).
    ASSERT_TRUE(place("furnace", {0, Y, 0}, 0.0f));
    EXPECT_EQ(value(0, Y, 0, "facing"), "north");
    ASSERT_TRUE(place("oak_stairs", {2, Y, 0}, 90.0f)); // looking west
    EXPECT_EQ(value(2, Y, 0, "facing"), "west");
    EXPECT_EQ(value(2, Y, 0, "half"), "bottom");
    ASSERT_TRUE(place("oak_log", {4, Y, 0}, 0.0f, Direction::East));
    EXPECT_EQ(value(4, Y, 0, "axis"), "x");
    ASSERT_TRUE(place("oak_slab", {6, Y + 1, 0}, 0.0f, Direction::East, 0.75));
    EXPECT_EQ(value(6, Y + 1, 0, "type"), "top");
    // Torches stand on floors and hang on walls, never on ceilings.
    ASSERT_TRUE(place("stone", {8, Y, 0}));
    ASSERT_TRUE(place("torch", {9, Y, 0}, 0.0f, Direction::East));
    EXPECT_EQ(at(9, Y, 0), "minecraft:wall_torch[facing=east]");
    ASSERT_TRUE(place("torch", {8, Y + 1, 0}));
    EXPECT_EQ(at(8, Y + 1, 0), "minecraft:torch");
    // A sign rotates with the player: yaw 0 (looking south) gives rotation 8.
    ASSERT_TRUE(place("oak_sign", {10, Y, 0}, 0.0f));
    EXPECT_EQ(value(10, Y, 0, "rotation"), "8");
}

TEST_F(PlacementFixture, StairCorners) {
    // An L of stairs: the outer corner turns its shape.
    ASSERT_TRUE(place("oak_stairs", {0, Y, 0}, 180.0f)); // facing north
    ASSERT_TRUE(place("oak_stairs", {0, Y, -1}, 90.0f)); // facing west, in front of the first
    EXPECT_EQ(value(0, Y, 0, "shape"), "outer_left");
    // Behind a stair facing another way: inner corner.
    ASSERT_TRUE(place("oak_stairs", {5, Y, 0}, 180.0f)); // facing north
    ASSERT_TRUE(place("oak_stairs", {5, Y, 1}, -90.0f)); // facing east, behind it
    EXPECT_EQ(value(5, Y, 0, "shape"), "inner_right");
}

TEST_F(PlacementFixture, FencesPanesAndWallsConnect) {
    ASSERT_TRUE(place("oak_fence", {0, Y, 0}));
    ASSERT_TRUE(place("oak_fence", {1, Y, 0}));
    EXPECT_EQ(value(0, Y, 0, "east"), "true");
    EXPECT_EQ(value(1, Y, 0, "west"), "true");
    ASSERT_TRUE(place("stone", {0, Y, 1}));
    EXPECT_EQ(value(0, Y, 0, "south"), "true"); // solid blocks join too
    ASSERT_TRUE(place("oak_leaves", {0, Y, -1}));
    EXPECT_EQ(value(0, Y, 0, "north"), "false"); // but not leaves
    EXPECT_EQ(value(0, Y, -1, "persistent"), "true"); // placed leaves never decay
    EXPECT_EQ(value(0, Y, -1, "distance"), "7");
    ASSERT_TRUE(place("nether_brick_fence", {-1, Y, 0}));
    EXPECT_EQ(value(0, Y, 0, "west"), "false"); // wooden and nether brick fences stay apart
    // Breaking a neighbor disconnects.
    level->setBlockAndUpdate({1, Y, 0}, BlockRegistry::kAirState);
    EXPECT_EQ(value(0, Y, 0, "east"), "false");
    // Panes.
    ASSERT_TRUE(place("glass_pane", {4, Y, 0}));
    ASSERT_TRUE(place("glass_pane", {4, Y, 1}));
    EXPECT_EQ(value(4, Y, 0, "south"), "true");
    // Walls: a straight run hides the post, a corner keeps it.
    ASSERT_TRUE(place("cobblestone_wall", {8, Y, 0}));
    ASSERT_TRUE(place("cobblestone_wall", {7, Y, 0}));
    ASSERT_TRUE(place("cobblestone_wall", {9, Y, 0}));
    EXPECT_EQ(value(8, Y, 0, "up"), "false");
    EXPECT_EQ(value(8, Y, 0, "east"), "low");
    ASSERT_TRUE(place("cobblestone_wall", {9, Y, 1}));
    EXPECT_EQ(value(9, Y, 0, "up"), "true");
}

TEST_F(PlacementFixture, TwoBlockBlocks) {
    ASSERT_TRUE(place("oak_door", {0, Y, 0}, 0.0f));
    EXPECT_EQ(value(0, Y, 0, "half"), "lower");
    EXPECT_EQ(value(0, Y + 1, 0, "half"), "upper");
    EXPECT_EQ(value(0, Y + 1, 0, "facing"), "south");
    // Removing one half removes the other.
    level->setBlockAndUpdate({0, Y + 1, 0}, BlockRegistry::kAirState);
    EXPECT_EQ(at(0, Y, 0), "minecraft:air");
    // No room above: no door.
    ASSERT_TRUE(place("stone", {2, Y + 1, 0}, 0.0f, Direction::Up));
    EXPECT_FALSE(place("oak_door", {2, Y, 0}));
    // Beds take the block they face; breaking the head removes the foot.
    ASSERT_TRUE(place("red_bed", {4, Y, 0}, 0.0f));
    EXPECT_EQ(value(4, Y, 0, "part"), "foot");
    EXPECT_EQ(value(4, Y, 1, "part"), "head");
    level->setBlockAndUpdate({4, Y, 1}, BlockRegistry::kAirState);
    EXPECT_EQ(at(4, Y, 0), "minecraft:air");
    // Tall grass needs both blocks.
    ASSERT_TRUE(place("tall_grass", {6, Y, 0}));
    EXPECT_EQ(value(6, Y + 1, 0, "half"), "upper");
    // Chests side by side pair up.
    ASSERT_TRUE(place("chest", {8, Y, 0}, 0.0f));
    ASSERT_TRUE(place("chest", {9, Y, 0}, 0.0f));
    EXPECT_NE(value(8, Y, 0, "type"), "single");
    EXPECT_NE(value(9, Y, 0, "type"), "single");
    EXPECT_NE(value(8, Y, 0, "type"), value(9, Y, 0, "type"));
    level->setBlockAndUpdate({9, Y, 0}, BlockRegistry::kAirState);
    EXPECT_EQ(value(8, Y, 0, "type"), "single");
}

TEST_F(PlacementFixture, SlabsDouble) {
    ASSERT_TRUE(place("oak_slab", {0, Y, 0}));
    const StateId bottom = level->getBlock({0, Y, 0});
    EXPECT_EQ(registry->value(bottom, "type"), "bottom");
    const auto merged = mergeIntoClicked(*registry, bottom, *registry->find("oak_slab"), Direction::Up, 0.5);
    ASSERT_TRUE(merged.has_value());
    EXPECT_EQ(registry->value(*merged, "type"), "double");
    EXPECT_FALSE(mergeIntoClicked(*registry, bottom, *registry->find("stone_slab"), Direction::Up, 0.5).has_value());
    EXPECT_FALSE(mergeIntoClicked(*registry, bottom, *registry->find("oak_slab"), Direction::Down, 0.0).has_value());
}

TEST_F(PlacementFixture, BlocksPopWithoutSupport) {
    ASSERT_TRUE(place("poppy", {0, Y, 0}));
    EXPECT_FALSE(place("poppy", {0, Y + 2, 0})); // flowers need dirt below
    level->setBlockAndUpdate({0, Y - 1, 0}, BlockRegistry::kAirState); // the grass under it
    EXPECT_EQ(at(0, Y, 0), "minecraft:air");
    // A wall torch falls with its wall; a ladder too.
    ASSERT_TRUE(place("stone", {3, Y, 0}));
    ASSERT_TRUE(place("torch", {4, Y, 0}, 0.0f, Direction::East));
    ASSERT_TRUE(place("ladder", {3, Y, -1}, 0.0f, Direction::North));
    level->setBlockAndUpdate({3, Y, 0}, BlockRegistry::kAirState);
    EXPECT_EQ(at(4, Y, 0), "minecraft:air");
    EXPECT_EQ(at(3, Y, -1), "minecraft:air");
    // Snow on grass makes it snowy; removing the snow clears it.
    level->setBlockAndUpdate({6, Y, 0}, S("snow"));
    EXPECT_EQ(value(6, Y - 1, 0, "snowy"), "true");
    level->setBlockAndUpdate({6, Y, 0}, BlockRegistry::kAirState);
    EXPECT_EQ(value(6, Y - 1, 0, "snowy"), "false");
}

TEST_F(PlacementFixture, HandBreakingSpeed) {
    Player p;
    p.onGround = true;
    // Vanilla: stone by hand takes 150 ticks (needs a pickaxe); dirt 15; leaves 6; bedrock never.
    auto ticks = [&](const char* b) {
        const float per = destroyProgressPerTick(registry->defaultState(b), p, *registry);
        return per <= 0.0f ? -1 : int(std::ceil(1.0f / per - 1e-4f));
    };
    EXPECT_EQ(ticks("stone"), 150);
    EXPECT_EQ(ticks("dirt"), 15);
    EXPECT_EQ(ticks("oak_leaves"), 6);
    EXPECT_EQ(ticks("oak_log"), 60);
    EXPECT_EQ(ticks("short_grass"), 1);
    EXPECT_EQ(ticks("bedrock"), -1);
    p.onGround = false;
    EXPECT_EQ(ticks("dirt"), 75); // five times slower in the air
}

TEST_F(PlacementFixture, ItemsAndInventory) {
    ItemRegistry items;
    std::string err;
    ASSERT_TRUE(items.load(*pack, *registry, MC_DEFAULT_DATA_DIR, &err)) << err;
    EXPECT_EQ(items.size(), 1416u);
    Language lang;
    ASSERT_TRUE(lang.load(*pack));
    auto item = [&](const char* id) {
        const Item* i = items.find(id);
        EXPECT_NE(i, nullptr) << id;
        return i;
    };
    EXPECT_EQ(items.name(*item("oak_planks"), lang), "Oak Planks");
    EXPECT_EQ(items.name(*item("redstone"), lang), "Redstone Dust");
    EXPECT_EQ(items.name(*item("diamond"), lang), "Diamond");
    // Block items, including the ones named after something else.
    EXPECT_EQ(item("oak_planks")->block, registry->find("oak_planks"));
    EXPECT_EQ(item("redstone")->block, registry->find("redstone_wire"));
    EXPECT_EQ(item("wheat_seeds")->block, registry->find("wheat"));
    EXPECT_EQ(item("wheat")->block, nullptr);
    EXPECT_EQ(item("diamond")->block, nullptr);
    // Picking: wall variants and crops give their item.
    EXPECT_EQ(items.forBlock(*registry->find("wall_torch")), item("torch"));
    EXPECT_EQ(items.forBlock(*registry->find("oak_wall_sign")), item("oak_sign"));
    EXPECT_EQ(items.forBlock(*registry->find("wheat")), item("wheat_seeds"));
    EXPECT_EQ(items.forBlock(*registry->find("redstone_wire")), item("redstone"));
    // Stack sizes.
    EXPECT_EQ(item("stone")->maxStack, 64);
    EXPECT_EQ(item("ender_pearl")->maxStack, 16);
    EXPECT_EQ(item("oak_sign")->maxStack, 16);
    EXPECT_EQ(item("diamond_sword")->maxStack, 1);
    EXPECT_EQ(item("red_bed")->maxStack, 1);
    EXPECT_EQ(item("water_bucket")->maxStack, 1);
    EXPECT_EQ(item("bucket")->maxStack, 16);
    // Inventory: tops up existing stacks, then fills empty slots in order.
    Inventory inv;
    inv.selected = 3;
    EXPECT_EQ(inv.add(*item("stone"), 100), 0);
    EXPECT_EQ(inv.main[0].count, 64);
    EXPECT_EQ(inv.main[1].count, 36);
    EXPECT_EQ(inv.add(*item("stone"), 30), 0);
    EXPECT_EQ(inv.main[1].count, 64);
    EXPECT_EQ(inv.main[2].count, 2);
    inv.pick(*item("stone"));
    EXPECT_EQ(inv.selected, 0);
    inv.selected = 5;
    inv.pick(*item("dirt"));
    EXPECT_EQ(inv.selected, 5);
    EXPECT_EQ(inv.main[5].item, item("dirt"));
    inv.consumeSelected(64);
    EXPECT_TRUE(inv.main[5].empty());
}

// Creative tabs: every item a player can get in vanilla's creative menu is in a tab
// (the rest is operator-only or not in the menu at all); search matches names and ids.
TEST_F(PlacementFixture, CreativeTabsAndSearch) {
    ItemRegistry items;
    std::string err;
    ASSERT_TRUE(items.load(*pack, *registry, MC_DEFAULT_DATA_DIR, &err)) << err;
    Language lang;
    ASSERT_TRUE(lang.load(*pack));
    CreativeTabs tabs;
    ASSERT_TRUE(tabs.load(MC_DEFAULT_DATA_DIR, items, &err)) << err;
    ASSERT_EQ(tabs.tabs().size(), 11u);
    EXPECT_EQ(tabs.tabs()[0].id, "building_blocks");
    EXPECT_EQ(shortId(tabs.tabs()[0].items[0]->id), "oak_log");
    EXPECT_EQ(shortId(tabs.tabs()[1].items[0]->id), "white_wool");
    std::set<const Item*> inTabs;
    for (const CreativeTab& t : tabs.tabs())
        for (const Item* i : t.items) inTabs.insert(i);
    std::vector<std::string> missing;
    for (size_t i = 0; i < items.size(); ++i)
        if (!inTabs.contains(&items.get(i))) missing.emplace_back(shortId(items.get(i).id));
    const std::vector<std::string> outsideCreative = {"air", "filled_map", "knowledge_book", "petrified_oak_slab",
                                                      "written_book"};
    EXPECT_EQ(missing, outsideCreative);
    // The search tab: visible tabs only (no operator blocks), each item once.
    const auto& all = tabs.searchable();
    EXPECT_EQ(std::set<const Item*>(all.begin(), all.end()).size(), all.size());
    EXPECT_EQ(std::count(all.begin(), all.end(), items.find("command_block")), 0);
    auto name = [&](const Item& i) { return items.name(i, lang); };
    EXPECT_EQ(tabs.search("", name).size(), all.size());
    auto hits = tabs.search("DIAMOND SW", name);
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0], items.find("diamond_sword"));
    hits = tabs.search("spruce_tr", name); // ids match too
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0], items.find("spruce_trapdoor"));
    EXPECT_GE(tabs.search("oak", name).size(), 20u);
    EXPECT_TRUE(tabs.search("no such thing", name).empty());
}

TEST_F(PlacementFixture, HandUseAndScheduledTicks) {
    ASSERT_TRUE(place("oak_door", {0, Y, 0}, 0.0f));
    const StateId lower = level->getBlock({0, Y, 0});
    auto use = useBlock(*registry, lower, Direction::South);
    ASSERT_TRUE(use.has_value());
    level->setBlockAndUpdate({0, Y, 0}, use->state);
    EXPECT_EQ(value(0, Y, 0, "open"), "true");
    EXPECT_EQ(value(0, Y + 1, 0, "open"), "true"); // the other half follows
    EXPECT_FALSE(useBlock(*registry, S("iron_door"), Direction::South).has_value()); // needs redstone
    // A wooden button stays pressed 30 ticks, then springs back.
    ASSERT_TRUE(place("stone", {3, Y, 0}));
    ASSERT_TRUE(place("oak_button", {3, Y + 1, 0}));
    const StateId button = level->getBlock({3, Y + 1, 0});
    auto press = useBlock(*registry, button, Direction::South);
    ASSERT_TRUE(press.has_value());
    EXPECT_EQ(press->tickDelay, 30);
    level->setBlockAndUpdate({3, Y + 1, 0}, press->state);
    level->scheduleTick({3, Y + 1, 0}, registry->info(press->state).block, press->tickDelay);
    for (int i = 0; i < 29; ++i) level->tick();
    EXPECT_EQ(value(3, Y + 1, 0, "powered"), "true");
    level->tick();
    EXPECT_EQ(value(3, Y + 1, 0, "powered"), "false");
    EXPECT_EQ(level->pendingTicks(), 0u);
    // Fence gates open away from the player.
    ASSERT_TRUE(place("oak_fence_gate", {6, Y, 0}, 0.0f)); // facing south
    auto gate = useBlock(*registry, level->getBlock({6, Y, 0}), Direction::North);
    ASSERT_TRUE(gate.has_value());
    EXPECT_EQ(registry->value(gate->state, "facing"), "north");
    EXPECT_EQ(registry->value(gate->state, "open"), "true");
}

// --- Survival (M10) ---

TEST(Survival, FoodDataDrainRegenerationAndStarvation) {
    FoodData food;
    float health = 10.0f;
    auto heal = [&](float h) { health = std::min(20.0f, health + h); };
    auto starve = [&](float d) { health -= d; };
    // Exhaustion past 4 costs a point of saturation first.
    food.addExhaustion(4.5f);
    food.tick(20.0f, 20.0f, Difficulty::Normal, true, heal, starve);
    EXPECT_FLOAT_EQ(food.saturation, 4.0f);
    EXPECT_EQ(food.foodLevel, 20);
    // A full bar with saturation heals min(saturation, 6) / 6 every 10 ticks.
    for (int i = 0; i < 10; ++i) food.tick(health, 20.0f, Difficulty::Normal, true, heal, starve);
    EXPECT_NEAR(health, 10.0f + 4.0f / 6.0f, 1e-5f);
    // 18 food and no saturation: 1 health every 80 ticks, costing 6 exhaustion.
    food = FoodData{};
    food.foodLevel = 18;
    food.saturation = 0.0f;
    health = 10.0f;
    for (int i = 0; i < 79; ++i) food.tick(health, 20.0f, Difficulty::Normal, true, heal, starve);
    EXPECT_FLOAT_EQ(health, 10.0f);
    food.tick(health, 20.0f, Difficulty::Normal, true, heal, starve);
    EXPECT_FLOAT_EQ(health, 11.0f);
    EXPECT_FLOAT_EQ(food.exhaustion, 6.0f);
    // Starving: down to 10 on Easy, 1 on Normal, all the way on Hard.
    for (auto [difficulty, floor] : {std::pair{Difficulty::Easy, 10.0f}, {Difficulty::Normal, 1.0f}, {Difficulty::Hard, 0.0f}}) {
        food = FoodData{};
        food.foodLevel = 0;
        food.saturation = 0.0f;
        health = 20.0f;
        for (int i = 0; i < 80 * 25 && health > 0.0f; ++i) food.tick(health, 20.0f, difficulty, true, heal, starve);
        EXPECT_FLOAT_EQ(health, floor) << int(difficulty);
    }
    // Eating: saturation can't pass the food level.
    food = FoodData{};
    food.foodLevel = 2;
    food.saturation = 0.0f;
    food.eat(8, 12.8f); // cooked beef
    EXPECT_EQ(food.foodLevel, 10);
    EXPECT_FLOAT_EQ(food.saturation, 10.0f);
}

TEST(Survival, ArmorAndExperienceFormulas) {
    // Vanilla: full diamond (20 armor, 8 toughness) takes 3 of 10 damage; 4 armor only its
    // floor of armor / 5 (3.2%).
    EXPECT_NEAR(damageAfterArmor(10.0f, 20.0f, 8.0f), 3.0f, 1e-5f);
    EXPECT_NEAR(damageAfterArmor(10.0f, 4.0f, 0.0f), 9.68f, 1e-5f);
    EXPECT_NEAR(damageAfterArmor(100.0f, 20.0f, 0.0f), 84.0f, 1e-4f); // floored at armor / 5
    Player p;
    EXPECT_EQ(p.xpNeededForNextLevel(), 7);
    p.giveExperiencePoints(352); // levels 0..15 need 16^2 + 6 * 16 points
    EXPECT_EQ(p.experienceLevel, 16);
    EXPECT_NEAR(p.experienceProgress, 0.0f, 1e-4f);
    EXPECT_EQ(p.xpNeededForNextLevel(), 42);
    p.experienceLevel = 30;
    EXPECT_EQ(p.xpNeededForNextLevel(), 112);
}

TEST_F(PlacementFixture, DamageInvulnerabilityAndArmorWear) {
    ItemRegistry items;
    std::string err;
    ASSERT_TRUE(items.load(*pack, *registry, MC_DEFAULT_DATA_DIR, &err)) << err;
    Player p;
    EXPECT_TRUE(p.hurt(DamageType::Fall, 5.0f));
    EXPECT_FLOAT_EQ(p.health, 15.0f);
    // A weaker hit inside the invulnerability window does nothing; a harder one adds its excess.
    EXPECT_FALSE(p.hurt(DamageType::Fall, 3.0f));
    EXPECT_TRUE(p.hurt(DamageType::Fall, 8.0f));
    EXPECT_FLOAT_EQ(p.health, 12.0f);
    // Armor: an iron chestplate (6) takes 4 cactus damage down to 4 x (1 - 4/25) and wears by 1.
    Player q;
    q.inventory.armor[2] = {items.find("iron_chestplate"), 1};
    EXPECT_TRUE(q.hurt(DamageType::Cactus, 4.0f));
    EXPECT_NEAR(q.health, 20.0f - 3.36f, 1e-4f);
    EXPECT_EQ(q.inventory.armor[2].damage, 1);
    // Creative players ignore everything but the void and /kill.
    Player c;
    c.setMode(GameMode::Creative);
    EXPECT_FALSE(c.hurt(DamageType::Lava, 4.0f));
    EXPECT_TRUE(c.hurt(DamageType::OutOfWorld, 4.0f));
    EXPECT_FLOAT_EQ(c.health, 16.0f);
}

TEST_F(PlacementFixture, FallingAndDrowning) {
    // A survival player dropped 13 blocks above the grass lands with 10 damage.
    Player p;
    p.pos = p.oldPos = {0.5, -60.0 + 13.0, 0.5};
    for (int i = 0; i < 100 && p.health == 20.0f; ++i) {
        p.tick(*level, PlayerInput{});
        p.survivalTick(*level, Difficulty::Normal, true);
    }
    EXPECT_FLOAT_EQ(p.health, 10.0f);
    EXPECT_EQ(p.deathMessageKey(), "death.fell.accident.generic");
    // What it lands on from the same height (vanilla fallOn): hay takes 80% off; a bed halves
    // the distance (13.44 from its top: 3 damage, where halving the damage would give 5); a
    // stalagmite's tip (11/16 high) adds 2.5 to the distance and doubles the damage.
    auto fallOnto = [&](int x, const char* block) {
        level->setBlock({x, -61, 0}, S(block));
        Player f;
        f.pos = f.oldPos = {x + 0.5, -60.0 + 13.0, 0.5};
        for (int i = 0; i < 100 && f.health == 20.0f && !f.onGround; ++i) {
            f.tick(*level, PlayerInput{});
            f.survivalTick(*level, Difficulty::Normal, true);
        }
        return 20.0f - f.health;
    };
    EXPECT_FLOAT_EQ(fallOnto(2, "hay_block"), 2.0f);           // floor((13 - 3) * 0.2)
    EXPECT_FLOAT_EQ(fallOnto(4, "red_bed[part=head]"), 3.0f);  // floor(13.4375 / 2 - 3)
    EXPECT_FLOAT_EQ(fallOnto(6, "pointed_dripstone[vertical_direction=up,thickness=tip]"), 20.0f); // 25: dead
    // Under water the air runs out after 15 seconds, then drowning takes 2 every second.
    for (int y = -60; y <= -56; ++y) level->setBlock({0, y, 0}, registry->defaultState("water"));
    Player d;
    d.pos = d.oldPos = {0.5, -60.0, 0.5};
    int ticks = 0;
    while (d.health == 20.0f && ticks < 400) {
        d.pos = d.oldPos = {0.5, -60.0, 0.5}; // held down
        d.velocity = glm::dvec3(0.0);
        d.updateFluidState(*level);
        d.survivalTick(*level, Difficulty::Normal, true);
        ++ticks;
    }
    EXPECT_EQ(ticks, 320); // 300 of air, then down to -20
    EXPECT_FLOAT_EQ(d.health, 18.0f);
}

TEST_F(PlacementFixture, ItemEntitiesFallMergeAndArePickedUp) {
    ItemRegistry items;
    std::string err;
    ASSERT_TRUE(items.load(*pack, *registry, MC_DEFAULT_DATA_DIR, &err)) << err;
    level->setItems(&items);
    const Item* diamond = items.find("diamond");
    auto drop = [&](glm::dvec3 at, int n) -> ItemEntity& {
        ItemEntity& e = ItemEntity::spawn(*level, at, ItemStack{diamond, n});
        e.velocity = glm::dvec3(0.0);
        return e;
    };
    drop({0.5, -58.0, 0.5}, 10);
    drop({0.7, -58.0, 0.6}, 20);
    drop({8.5, -58.0, 8.5}, 5);
    for (int i = 0; i < 60; ++i) level->tick();
    // The two close stacks became one; everything rests on the grass (y -60).
    ASSERT_EQ(level->entities().size(), 2u);
    int total = 0;
    for (const auto& e : level->entities()) {
        const auto& item = dynamic_cast<const ItemEntity&>(*e);
        total += item.stack.count;
        EXPECT_NEAR(item.pos.y, -60.0, 1e-6);
    }
    EXPECT_EQ(total, 35);
    // A player standing next to one picks it up; the far one stays.
    Player p;
    p.pos = p.oldPos = {0.5, -60.0, 0.5};
    level->setPlayer(&p);
    level->tick();
    EXPECT_EQ(p.inventory.main[0].count, 30);
    EXPECT_EQ(level->entities().size(), 1u);
    EXPECT_EQ(level->takePickups().size(), 1u);
    // Experience orbs drift to the player and give their points.
    ExperienceOrb::award(*level, {2.5, -59.0, 2.5}, 10); // a 7 and a 3
    for (int i = 0; i < 200; ++i) {
        p.tick(*level, PlayerInput{}); // counts down the 2 ticks between orbs
        level->tick();
    }
    EXPECT_EQ(p.totalExperience, 10);
    EXPECT_EQ(p.experienceLevel, 1);
    level->setPlayer(nullptr);
}

TEST_F(PlacementFixture, BlocksRemovedByUpdatesDropTheirLoot) {
    ItemRegistry items;
    BlockLoot loot;
    std::string err;
    ASSERT_TRUE(items.load(*pack, *registry, MC_DEFAULT_DATA_DIR, &err)) << err;
    ASSERT_TRUE(loot.load(std::filesystem::path(MC_DEFAULT_DATA_DIR) / "loot" / "blocks.json", *registry, items, &err))
        << err;
    level->setItems(&items);
    level->setLoot(&loot);
    auto dropped = [&] {
        std::map<std::string, int> out;
        for (const auto& e : level->entities())
            if (const auto* item = dynamic_cast<const ItemEntity*>(e.get()); item && !item->removed)
                out[std::string(shortId(item->stack.item->id))] += item->stack.count;
        return out;
    };
    // A wall torch whose wall is mined pops off as a torch.
    const int Y = -60;
    ASSERT_TRUE(place("stone", {2, Y, 0}));
    ASSERT_TRUE(place("torch", {3, Y, 0}, 0.0f, Direction::East));
    level->setBlockAndUpdate({2, Y, 0}, BlockRegistry::kAirState);
    EXPECT_EQ(at(3, Y, 0), "minecraft:air");
    EXPECT_EQ(dropped(), (std::map<std::string, int>{{"torch", 1}}));
    // A door's lower half drops the door when its upper half goes; the upper one drops nothing.
    ASSERT_TRUE(place("oak_door", {6, Y, 0}));
    level->setBlockAndUpdate({6, Y + 1, 0}, BlockRegistry::kAirState);
    EXPECT_EQ(at(6, Y, 0), "minecraft:air");
    EXPECT_EQ(dropped(), (std::map<std::string, int>{{"torch", 1}, {"oak_door", 1}}));
    level->setLoot(nullptr);
}

TEST_F(PlacementFixture, FurnaceSmeltsAndChestsSurviveReload) {
    ItemRegistry items;
    RecipeBook recipes;
    std::string err;
    ASSERT_TRUE(items.load(*pack, *registry, MC_DEFAULT_DATA_DIR, &err)) << err;
    ASSERT_TRUE(recipes.load(MC_DEFAULT_DATA_DIR, items, &err)) << err;
    level->setItems(&items);
    level->setRecipes(&recipes);
    const int Y = -60;
    // A furnace with 2 raw iron and a coal: lit at once, an ingot every 200 ticks, the
    // experience (0.7 each) kept until taken.
    level->setBlockAndUpdate({0, Y, 3}, S("furnace"));
    auto furnace = std::dynamic_pointer_cast<FurnaceBlockEntity>(level->blockEntity({0, Y, 3}));
    ASSERT_TRUE(furnace);
    furnace->items[FurnaceBlockEntity::kInput] = {items.find("raw_iron"), 2};
    furnace->items[FurnaceBlockEntity::kFuel] = {items.find("coal"), 1};
    level->tick();
    EXPECT_EQ(value(0, Y, 3, "lit"), "true");
    EXPECT_TRUE(furnace->items[FurnaceBlockEntity::kFuel].empty());
    for (int i = 0; i < 198; ++i) level->tick();
    EXPECT_TRUE(furnace->items[FurnaceBlockEntity::kOutput].empty()); // 199 ticks of cooking
    level->tick();
    EXPECT_EQ(furnace->items[FurnaceBlockEntity::kOutput].count, 1);
    for (int i = 0; i < 200; ++i) level->tick();
    EXPECT_EQ(furnace->items[FurnaceBlockEntity::kOutput].count, 2);
    EXPECT_TRUE(furnace->items[FurnaceBlockEntity::kInput].empty());
    EXPECT_NEAR(furnace->storedExperience, 1.4f, 1e-5f);
    EXPECT_EQ(furnace->takeExperience(0.5f), 1); // 1.4: the 0.4 rounds up when random < 0.4
    // The coal (1600 ticks) burns out; the flame goes out with it.
    for (int i = 0; i < 1300; ++i) level->tick();
    EXPECT_EQ(value(0, Y, 3, "lit"), "false");
    // A lava bucket leaves its bucket in the fuel slot (with room for the stone).
    furnace->items[FurnaceBlockEntity::kOutput] = {};
    furnace->items[FurnaceBlockEntity::kInput] = {items.find("cobblestone"), 1};
    furnace->items[FurnaceBlockEntity::kFuel] = {items.find("lava_bucket"), 1};
    level->tick();
    EXPECT_EQ(furnace->items[FurnaceBlockEntity::kFuel].item, items.find("bucket"));

    // A chest's contents go into the chunk's extras and come back on load.
    level->setBlockAndUpdate({2, Y, 3}, S("chest"));
    auto chest = std::dynamic_pointer_cast<ContainerBlockEntity>(level->blockEntity({2, Y, 3}));
    ASSERT_TRUE(chest);
    chest->items[4] = {items.find("diamond"), 7};
    const auto dir = std::filesystem::temp_directory_path() / ("mc_chest_test_" + std::to_string(::getpid()));
    std::filesystem::remove_all(dir);
    {
        auto storage = std::make_shared<ChunkStorage>(dir, *registry, nullptr);
        level->attachStorage(storage);
        EXPECT_GT(level->chunks().saveAll(), 0);
        while (level->chunks().savesPending()) {
            jobs->waitIdle();
            jobs->drainMain(1000.0);
        }
        Json extra;
        auto saved = storage->load({0, 0}, &extra);
        ASSERT_TRUE(saved);
        const Json bes = extra.value("block_entities", Json::array());
        bool found = false;
        for (const Json& be : bes)
            if (be.value("type", std::string()) == "minecraft:chest") {
                found = true;
                EXPECT_EQ(be["items"][0]["id"], "minecraft:diamond");
                EXPECT_EQ(be["items"][0]["count"], 7);
                EXPECT_EQ(be["items"][0]["slot"], 4);
            }
        EXPECT_TRUE(found);
    }
    std::filesystem::remove_all(dir);
    level->setRecipes(nullptr);
}

TEST_F(PlacementFixture, SignsTakeVanillaDataAndGoWithTheirBlock) {
    ItemRegistry items;
    std::string err;
    ASSERT_TRUE(items.load(*pack, *registry, MC_DEFAULT_DATA_DIR, &err)) << err;
    level->setItems(&items);
    // /setblock takes the block entity's data in vanilla's SNBT (1b for true).
    Commands commands;
    CommandSource src;
    src.level = level.get();
    ASSERT_TRUE(commands.execute(
        src,
        R"(/setblock 0 -60 2 oak_sign[rotation=4]{front_text:{messages:["Hi","there you","",""],color:"red",has_glowing_text:1b},is_waxed:1b})",
        &err))
        << err;
    EXPECT_EQ(value(0, Y, 2, "rotation"), "4");
    auto sign = std::dynamic_pointer_cast<SignBlockEntity>(level->blockEntity({0, Y, 2}));
    ASSERT_TRUE(sign);
    EXPECT_EQ(sign->front.lines[0], "Hi");
    EXPECT_EQ(sign->front.lines[1], "there you");
    EXPECT_EQ(sign->front.color, "red");
    EXPECT_TRUE(sign->front.glowing);
    EXPECT_TRUE(sign->back.empty());
    EXPECT_EQ(sign->back.color, "black");
    EXPECT_TRUE(sign->waxed);
    EXPECT_FALSE(commands.execute(src, "/setblock 0 -60 3 oak_sign{front_text:", &err));

    // Saved under vanilla's names, and loaded back.
    Json saved;
    sign->save(saved);
    EXPECT_EQ(saved["front_text"]["messages"][1], "there you");
    EXPECT_EQ(saved["front_text"]["has_glowing_text"], true);
    EXPECT_EQ(saved["is_waxed"], true);
    SignBlockEntity copy({0, Y, 2}, false);
    copy.load(saved, items);
    EXPECT_EQ(copy.front.lines, sign->front.lines);
    EXPECT_EQ(copy.front.color, "red");
    EXPECT_TRUE(copy.front.glowing && copy.waxed);

    // Text colors (vanilla DyeColor.getTextColor); sign sizes.
    EXPECT_EQ(dyeTextColor("light_blue"), 0x9AC0CDu);
    EXPECT_EQ(dyeTextColor("black"), 0x000000u);
    EXPECT_EQ(sign->maxLineWidth(), 90);
    EXPECT_EQ(sign->lineHeight(), 10);
    level->setBlock({1, Y + 1, 2}, S("oak_hanging_sign"));
    auto hanging = std::dynamic_pointer_cast<SignBlockEntity>(level->blockEntity({1, Y + 1, 2}));
    ASSERT_TRUE(hanging);
    EXPECT_TRUE(hanging->hanging);
    EXPECT_EQ(hanging->type(), "minecraft:hanging_sign");
    EXPECT_EQ(hanging->maxLineWidth(), 60);
    EXPECT_EQ(hanging->lineHeight(), 9);

    // Breaking the block lets go of its entity (an open editor sees that and closes); a new
    // sign there starts blank.
    level->setBlockAndUpdate({0, Y, 2}, S("air"));
    EXPECT_TRUE(sign->removed);
    EXPECT_FALSE(level->blockEntities().contains({0, Y, 2}));
    level->setBlockAndUpdate({0, Y, 2}, S("oak_sign"));
    auto fresh = std::dynamic_pointer_cast<SignBlockEntity>(level->blockEntity({0, Y, 2}));
    ASSERT_TRUE(fresh);
    EXPECT_TRUE(fresh->front.empty());
    EXPECT_FALSE(fresh->waxed);
}
