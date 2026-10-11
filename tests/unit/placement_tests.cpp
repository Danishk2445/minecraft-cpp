#include <memory>
#include <set>

#include <gtest/gtest.h>

#include "core/JobSystem.h"
#include "entity/Player.h"
#include "game/CreativeTabs.h"
#include "game/Inventory.h"
#include "game/Item.h"
#include "game/Placement.h"
#include "resources/Language.h"
#include "resources/Atlas.h"
#include "resources/BlockModels.h"
#include "resources/ResourcePack.h"
#include "world/block/BlockBehavior.h"
#include "world/block/BlockRegistry.h"
#include "world/gen/ChunkGenerator.h"
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
        const float per = destroyProgressPerTick(*registry->find(b), p);
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
