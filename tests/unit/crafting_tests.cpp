// Recipes, crafting menus, tools and fuel against the real item and recipe data.
#include <cmath>
#include <algorithm>
#include <map>
#include <set>
#include <memory>

#include <gtest/gtest.h>

#include "entity/Player.h"
#include "game/ContainerMenus.h"
#include "game/Loot.h"
#include "game/Recipes.h"
#include "game/Tools.h"
#include "resources/Atlas.h"
#include "resources/BlockModels.h"
#include "resources/ModelBaker.h"
#include "resources/ResourcePack.h"
#include "world/block/BlockRegistry.h"

using namespace mc;

namespace {

struct CraftingFixture : ::testing::Test {
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
        itemsError = new std::string;
        items->load(*pack, *registry, MC_DEFAULT_DATA_DIR, itemsError);
        recipes = std::make_unique<RecipeBook>();
        recipesError = new std::string;
        recipes->load(MC_DEFAULT_DATA_DIR, *items, recipesError);
    }
    static void TearDownTestSuite() {
        recipes.reset();
        items.reset();
        registry.reset();
        models.reset();
        baker.reset();
        store.reset();
        atlas.reset();
        pack.reset();
        delete itemsError;
        delete recipesError;
    }

    static ItemStack S(const char* id, int n = 1) {
        const Item* item = items->find(id);
        EXPECT_NE(item, nullptr) << id;
        return item ? ItemStack{item, n} : ItemStack{};
    }
    static std::string idOf(const ItemStack& s) { return s.empty() ? "" : std::string(shortId(s.item->id)); }

    static std::unique_ptr<ResourcePack> pack;
    static std::unique_ptr<Atlas> atlas;
    static std::unique_ptr<ModelStore> store;
    static std::unique_ptr<ModelBaker> baker;
    static std::unique_ptr<BlockModels> models;
    static std::unique_ptr<BlockRegistry> registry;
    static std::unique_ptr<ItemRegistry> items;
    static std::unique_ptr<RecipeBook> recipes;
    static std::string* itemsError;
    static std::string* recipesError;
};

std::unique_ptr<ResourcePack> CraftingFixture::pack;
std::unique_ptr<Atlas> CraftingFixture::atlas;
std::unique_ptr<ModelStore> CraftingFixture::store;
std::unique_ptr<ModelBaker> CraftingFixture::baker;
std::unique_ptr<BlockModels> CraftingFixture::models;
std::unique_ptr<BlockRegistry> CraftingFixture::registry;
std::unique_ptr<ItemRegistry> CraftingFixture::items;
std::unique_ptr<RecipeBook> CraftingFixture::recipes;
std::string* CraftingFixture::itemsError;
std::string* CraftingFixture::recipesError;

} // namespace

TEST_F(CraftingFixture, RecipeDataLoadsWithoutConflicts) {
    EXPECT_EQ(*itemsError, "");
    EXPECT_EQ(*recipesError, "");
    size_t crafting = 0, cooking = 0;
    for (const Recipe& r : recipes->recipes()) (r.crafting() ? crafting : cooking) += 1;
    EXPECT_GE(crafting + cooking, 1000u);
    EXPECT_GE(crafting, 800u);
    EXPECT_GE(cooking, 100u);
    const auto conflicts = recipes->conflicts();
    for (const auto& c : conflicts) ADD_FAILURE() << "conflict: " << c;
}

TEST_F(CraftingFixture, ShapedShapelessAndMirrored) {
    // A log anywhere in the 2x2 grid: 4 planks.
    std::array<ItemStack, 4> grid2{};
    grid2[3] = S("oak_log");
    const auto planks = recipes->craft(grid2, 2, 2).result;
    EXPECT_EQ(idOf(planks), "oak_planks");
    EXPECT_EQ(planks.count, 4);
    // The axe (##/#|/ |) also works mirrored (##/|#/| ), anywhere in the 3x3 grid.
    const ItemStack p = S("oak_planks"), stick = S("stick"), none;
    const std::array<ItemStack, 9> axe = {p, p, none, p, stick, none, none, stick, none};
    const std::array<ItemStack, 9> mirrored = {none, p, p, none, stick, p, none, stick, none};
    EXPECT_EQ(idOf(recipes->craft(axe, 3, 3).result), "wooden_axe");
    EXPECT_EQ(idOf(recipes->craft(mirrored, 3, 3).result), "wooden_axe");
    // A torch: coal over a stick, in any column; charcoal works too (#coals).
    const std::array<ItemStack, 9> torch = {none, none, S("charcoal"), none, none, stick, none, none, none};
    const auto t = recipes->craft(torch, 3, 3).result;
    EXPECT_EQ(idOf(t), "torch");
    EXPECT_EQ(t.count, 4);
    // Nothing for nonsense.
    const std::array<ItemStack, 9> junk = {S("dirt"), stick, none, none, none, none, none, none, S("dirt")};
    EXPECT_TRUE(recipes->craft(junk, 3, 3).result.empty());
}

TEST_F(CraftingFixture, InventoryCraftingTakeShiftClickAndRemainders) {
    Player player;
    InventoryMenu menu(player.inventory, recipes.get());
    menu.craftGrid[0] = S("oak_log", 2);
    menu.slotsChanged();
    EXPECT_EQ(idOf(menu.craftResult), "oak_planks");
    // Taking the result uses one log; the result comes back for the second.
    menu.clicked(InventoryMenu::kResult, 0, ClickType::Pickup, player);
    EXPECT_EQ(menu.carried().count, 4);
    EXPECT_EQ(menu.craftGrid[0].count, 1);
    EXPECT_EQ(idOf(menu.craftResult), "oak_planks");
    // Clicking again with planks on the cursor tops it up.
    menu.clicked(InventoryMenu::kResult, 0, ClickType::Pickup, player);
    EXPECT_EQ(menu.carried().count, 8);
    EXPECT_TRUE(menu.craftGrid[0].empty());
    EXPECT_TRUE(menu.craftResult.empty());
    menu.removed(player);
    // Shift-clicking crafts all it can: 3 logs make 12 planks in the inventory.
    Player p2;
    InventoryMenu m2(p2.inventory, recipes.get());
    m2.craftGrid[1] = S("birch_log", 3);
    m2.slotsChanged();
    m2.clicked(InventoryMenu::kResult, 0, ClickType::QuickMove, p2);
    int planks = 0;
    for (const ItemStack& s : p2.inventory.main)
        if (idOf(s) == "birch_planks") planks += s.count;
    EXPECT_EQ(planks, 12);
    EXPECT_TRUE(m2.craftGrid[1].empty());
    // Cake: the milk buckets come back empty.
    Player p3;
    CraftingMenu table(p3.inventory, recipes.get());
    const ItemStack milk = S("milk_bucket"), sugar = S("sugar"), egg = S("egg"), wheat = S("wheat");
    table.grid = {milk, milk, milk, sugar, egg, sugar, wheat, wheat, wheat};
    table.slotsChanged();
    ASSERT_EQ(idOf(table.result), "cake");
    table.clicked(CraftingMenu::kResult, 0, ClickType::Pickup, p3);
    EXPECT_EQ(idOf(table.carried()), "cake");
    for (int i = 0; i < 3; ++i) EXPECT_EQ(idOf(table.grid[size_t(i)]), "bucket");
    EXPECT_TRUE(table.grid[4].empty());
}

TEST_F(CraftingFixture, RepairCookingAndFuel) {
    // Two worn pickaxes: their durability left plus 5% of the maximum.
    ItemStack a = S("wooden_pickaxe"), b = S("wooden_pickaxe");
    a.damage = 50; // 9 left
    b.damage = 40; // 19 left
    std::array<ItemStack, 4> grid{a, {}, {}, b};
    const auto repaired = recipes->craft(grid, 2, 2).result;
    EXPECT_EQ(idOf(repaired), "wooden_pickaxe");
    EXPECT_EQ(repaired.damage, 59 - (9 + 19 + 2));
    // Cooking.
    const Recipe* iron = recipes->findCooking(RecipeType::Smelting, S("raw_iron"));
    ASSERT_NE(iron, nullptr);
    EXPECT_EQ(idOf(iron->result), "iron_ingot");
    EXPECT_EQ(iron->cookingTime, 200);
    EXPECT_FLOAT_EQ(iron->experience, 0.7f);
    const Recipe* blast = recipes->findCooking(RecipeType::Blasting, S("raw_iron"));
    ASSERT_NE(blast, nullptr);
    EXPECT_EQ(blast->cookingTime, 100);
    EXPECT_EQ(recipes->findCooking(RecipeType::Smoking, S("raw_iron")), nullptr);
    const Recipe* beef = recipes->findCooking(RecipeType::Smoking, S("beef"));
    ASSERT_NE(beef, nullptr);
    EXPECT_EQ(idOf(beef->result), "cooked_beef");
    // Fuel.
    EXPECT_EQ(items->find("coal")->fuelTicks, 1600);
    EXPECT_EQ(items->find("oak_planks")->fuelTicks, 300);
    EXPECT_EQ(items->find("lava_bucket")->fuelTicks, 20000);
    EXPECT_EQ(items->find("stick")->fuelTicks, 100);
    EXPECT_EQ(items->find("cobblestone")->fuelTicks, 0);
}

TEST_F(CraftingFixture, ToolBreakTimes) {
    Player p;
    p.onGround = true;
    auto ticks = [&](const char* block, const char* tool) {
        p.inventory.main[0] = tool ? S(tool) : ItemStack{};
        const float per = destroyProgressPerTick(registry->defaultState(block), p, *registry);
        return per <= 0.0f ? -1 : int(std::ceil(1.0f / per - 1e-4f));
    };
    // Vanilla: stone by hand 150 ticks (no drops), with a wooden pickaxe 23; obsidian with a
    // diamond pickaxe 188; logs by hand 60, with a stone axe 15; dirt with a wooden shovel 8.
    EXPECT_EQ(ticks("stone", nullptr), 150);
    EXPECT_EQ(ticks("stone", "wooden_pickaxe"), 23);
    EXPECT_EQ(ticks("obsidian", "diamond_pickaxe"), 188);
    EXPECT_EQ(ticks("obsidian", "iron_pickaxe"), 834); // too weak: 100 instead of 30
    EXPECT_EQ(ticks("oak_log", nullptr), 60);
    EXPECT_EQ(ticks("oak_log", "stone_axe"), 15);
    EXPECT_EQ(ticks("dirt", "wooden_shovel"), 8);
    EXPECT_EQ(ticks("dirt", "stone_shovel"), 4);
    EXPECT_EQ(ticks("cobweb", "iron_sword"), 8); // 4 hardness, speed 15, correct tool
    EXPECT_EQ(ticks("oak_leaves", "shears"), 1);
    // Drops need the right tier.
    EXPECT_FALSE(canHarvest({}, registry->defaultState("stone"), *registry));
    EXPECT_TRUE(canHarvest(S("wooden_pickaxe"), registry->defaultState("stone"), *registry));
    EXPECT_FALSE(canHarvest(S("stone_pickaxe"), registry->defaultState("diamond_ore"), *registry));
    EXPECT_TRUE(canHarvest(S("iron_pickaxe"), registry->defaultState("diamond_ore"), *registry));
    EXPECT_TRUE(canHarvest({}, registry->defaultState("dirt"), *registry));
    // Wear: tools 1 per block (swords 2), nothing for instantly broken blocks.
    EXPECT_EQ(miningWear(S("wooden_pickaxe"), registry->defaultState("stone"), *registry), 1);
    EXPECT_EQ(miningWear(S("iron_sword"), registry->defaultState("stone"), *registry), 2);
    EXPECT_EQ(miningWear(S("wooden_pickaxe"), registry->defaultState("short_grass"), *registry), 0);
    EXPECT_EQ(miningWear(S("shears"), registry->defaultState("short_grass"), *registry), 1);
}

TEST_F(CraftingFixture, BlockLoot) {
    BlockLoot loot;
    std::string err;
    ASSERT_TRUE(loot.load(std::filesystem::path(MC_DEFAULT_DATA_DIR) / "loot" / "blocks.json", *registry, *items, &err))
        << err;
    Xoroshiro random(7);
    auto dropsOf = [&](const char* state, bool shears = false) {
        std::map<std::string, int> out;
        for (const ItemStack& s : loot.drops(*registry->parse(state), shears, random)) out[idOf(s)] += s.count;
        return out;
    };
    using M = std::map<std::string, int>;
    EXPECT_EQ(dropsOf("stone"), (M{{"cobblestone", 1}}));
    EXPECT_EQ(dropsOf("grass_block"), (M{{"dirt", 1}}));
    EXPECT_EQ(dropsOf("oak_planks"), (M{{"oak_planks", 1}})); // no rule: its own item
    EXPECT_EQ(dropsOf("glass"), M{});                          // only with Silk Touch
    EXPECT_EQ(dropsOf("oak_slab[type=double]"), (M{{"oak_slab", 2}}));
    EXPECT_EQ(dropsOf("wall_torch"), (M{{"torch", 1}}));
    EXPECT_EQ(dropsOf("oak_leaves", true), (M{{"oak_leaves", 1}}));
    EXPECT_EQ(dropsOf("oak_door[half=upper]"), M{});
    EXPECT_EQ(dropsOf("oak_door[half=lower]"), (M{{"oak_door", 1}}));
    EXPECT_EQ(dropsOf("candle[candles=3]"), (M{{"candle", 3}}));
    // Ores: their item, and experience in vanilla's ranges.
    EXPECT_EQ(dropsOf("diamond_ore"), (M{{"diamond", 1}}));
    EXPECT_EQ(dropsOf("iron_ore"), (M{{"raw_iron", 1}}));
    int lo = 100, hi = 0, lapisLo = 100, lapisHi = 0;
    for (int i = 0; i < 500; ++i) {
        const int xp = loot.experience(registry->defaultState("diamond_ore"), random);
        lo = std::min(lo, xp), hi = std::max(hi, xp);
        const int n = dropsOf("lapis_ore")["lapis_lazuli"];
        lapisLo = std::min(lapisLo, n), lapisHi = std::max(lapisHi, n);
    }
    EXPECT_EQ(lo, 3);
    EXPECT_EQ(hi, 7);
    EXPECT_EQ(lapisLo, 4);
    EXPECT_EQ(lapisHi, 9);
    EXPECT_EQ(loot.experience(registry->defaultState("iron_ore"), random), 0); // raw ores give none
    // Leaves without shears: saplings 1 in 20, sticks 1 in 50, apples 1 in 200 (oak).
    int saplings = 0, apples = 0;
    for (int i = 0; i < 20000; ++i) {
        const auto d = dropsOf("oak_leaves");
        saplings += d.contains("oak_sapling");
        apples += d.contains("apple");
    }
    EXPECT_NEAR(saplings / 20000.0, 0.05, 0.01);
    EXPECT_NEAR(apples / 20000.0, 0.005, 0.0025);
    // Wheat: seeds always; the grain only when ripe.
    EXPECT_FALSE(dropsOf("wheat[age=3]").contains("wheat"));
    EXPECT_EQ(dropsOf("wheat[age=7]")["wheat"], 1);
}

// The plan's M10 check: every tool tier the overworld allows (wood to diamond, and gold) can
// be reached in survival from nothing. Blocks the overworld generates are harvested with
// whatever is at hand (their loot, rolled many times for the chancy drops); what is held is
// crafted (3x3 recipes once there is a crafting table) and smelted (once there is a furnace
// and fuel), until nothing new turns up. Netherite needs the Nether, which is out of scope.
TEST_F(CraftingFixture, EveryTierIsReachableFromNaturalDrops) {
    BlockLoot loot;
    std::string err;
    ASSERT_TRUE(loot.load(std::filesystem::path(MC_DEFAULT_DATA_DIR) / "loot" / "blocks.json", *registry, *items, &err))
        << err;
    const char* natural[] = {"oak_log",      "birch_log",    "oak_leaves",    "grass_block",   "dirt",
                             "sand",         "gravel",       "clay",          "stone",         "deepslate",
                             "coal_ore",     "iron_ore",     "copper_ore",    "gold_ore",      "redstone_ore",
                             "lapis_ore",    "diamond_ore",  "deepslate_iron_ore", "deepslate_diamond_ore",
                             "emerald_ore",  "sugar_cane",   "short_grass"};
    // The gates the chain has to pass: stone needs a pickaxe, iron a stone one, diamonds iron.
    const StateId stone = registry->find("stone")->defaultState, ironOre = registry->find("iron_ore")->defaultState,
                  diamondOre = registry->find("diamond_ore")->defaultState;
    EXPECT_FALSE(canHarvest(ItemStack{}, stone, *registry));
    EXPECT_TRUE(canHarvest(S("wooden_pickaxe"), stone, *registry));
    EXPECT_FALSE(canHarvest(S("wooden_pickaxe"), ironOre, *registry));
    EXPECT_TRUE(canHarvest(S("stone_pickaxe"), ironOre, *registry));
    EXPECT_FALSE(canHarvest(S("stone_pickaxe"), diamondOre, *registry));
    EXPECT_TRUE(canHarvest(S("iron_pickaxe"), diamondOre, *registry));
    std::set<const Item*> have;
    Xoroshiro random(11);
    auto harvestable = [&](StateId state) {
        if (canHarvest(ItemStack{}, state, *registry)) return true;
        for (const Item* item : have)
            if (item->tool != ToolKind::None && canHarvest(ItemStack{item, 1}, state, *registry)) return true;
        return false;
    };
    auto available = [&](const Ingredient& ing) {
        return std::any_of(ing.items.begin(), ing.items.end(), [&](const Item* i) { return have.contains(i); });
    };
    for (size_t before = SIZE_MAX; before != have.size();) {
        before = have.size();
        for (const char* id : natural) {
            const Block* block = registry->find(id);
            ASSERT_NE(block, nullptr) << id;
            if (!harvestable(block->defaultState)) continue;
            for (int roll = 0; roll < 64; ++roll)
                for (const ItemStack& s : loot.drops(block->defaultState, false, random)) have.insert(s.item);
        }
        const bool table = have.contains(items->find("crafting_table"));
        const bool furnace = have.contains(items->find("furnace"));
        const bool fuel = std::any_of(have.begin(), have.end(), [](const Item* i) { return i->fuelTicks > 0; });
        for (const Recipe& r : recipes->recipes()) {
            if (have.contains(r.result.item)) continue;
            bool ok = false;
            if (r.crafting()) {
                const size_t filled = size_t(std::count_if(r.ingredients.begin(), r.ingredients.end(),
                                                           [](const Ingredient& i) { return !i.empty(); }));
                const bool fitsInventory = r.type == RecipeType::Shaped ? r.width <= 2 && r.height <= 2 : filled <= 4;
                ok = (table || fitsInventory) && std::all_of(r.ingredients.begin(), r.ingredients.end(), [&](const Ingredient& i) {
                         return i.empty() || available(i);
                     });
            } else if (r.type == RecipeType::Smelting) {
                ok = furnace && fuel && available(r.ingredients.front());
            }
            if (ok) have.insert(r.result.item);
        }
    }
    for (const char* tier : {"wooden", "stone", "iron", "golden", "diamond"})
        for (const char* kind : {"pickaxe", "axe", "shovel", "hoe", "sword"}) {
            const std::string id = std::string(tier) + "_" + kind;
            EXPECT_TRUE(have.contains(items->find(id))) << id << " is out of reach";
        }
    for (const char* armor : {"iron_chestplate", "golden_helmet", "diamond_boots"})
        EXPECT_TRUE(have.contains(items->find(armor))) << armor;
    EXPECT_FALSE(have.contains(items->find("netherite_pickaxe")));
}
