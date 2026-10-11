#include <gtest/gtest.h>

#include "entity/Player.h"
#include "game/Menu.h"

using namespace mc;

namespace {

struct MenuFixture : ::testing::Test {
    Item stone{"minecraft:stone", 0, 64};
    Item dirt{"minecraft:dirt", 1, 64};
    Item pearl{"minecraft:ender_pearl", 2, 16};
    Item helmet{"minecraft:iron_helmet", 3, 1, nullptr, "", EquipmentSlot::Head};
    Item shield{"minecraft:shield", 4, 1, nullptr, "", EquipmentSlot::OffHand};
    Player player;
    InventoryMenu menu{player.inventory};

    // Menu slot of inventory main slot n (9..35) and hotbar slot h (0..8).
    static int mainSlot(int n) { return n; }
    static int hotbarSlot(int h) { return InventoryMenu::kHotbarStart + h; }
    ItemStack& at(int slot) { return *menu.slots[size_t(slot)].stack; }
    void click(int slot, int button, ClickType type = ClickType::Pickup) { menu.clicked(slot, button, type, player); }
    // A drag over `slots` with type 0 (split evenly), 1 (one each) or 2 (clone).
    void drag(int type, std::initializer_list<int> slots) {
        click(-999, Menu::quickcraftMask(0, type), ClickType::QuickCraft);
        for (int s : slots) click(s, Menu::quickcraftMask(1, type), ClickType::QuickCraft);
        click(-999, Menu::quickcraftMask(2, type), ClickType::QuickCraft);
    }
};

} // namespace

TEST_F(MenuFixture, PickupPlaceAndSplit) {
    at(mainSlot(9)) = {&stone, 10};
    click(9, 0); // take all
    EXPECT_EQ(menu.carried().count, 10);
    EXPECT_TRUE(at(9).empty());
    click(10, 1); // right click on an empty slot places one
    EXPECT_EQ(at(10).count, 1);
    EXPECT_EQ(menu.carried().count, 9);
    click(11, 0); // left click places the rest
    EXPECT_EQ(at(11).count, 9);
    EXPECT_TRUE(menu.carried().empty());
    at(12) = {&stone, 7};
    click(12, 1); // right click takes half, rounded up
    EXPECT_EQ(menu.carried().count, 4);
    EXPECT_EQ(at(12).count, 3);
}

TEST_F(MenuFixture, MergeAndSwap) {
    at(9) = {&stone, 60};
    menu.carried() = {&stone, 10};
    click(9, 0); // merge up to the stack limit
    EXPECT_EQ(at(9).count, 64);
    EXPECT_EQ(menu.carried().count, 6);
    at(10) = {&dirt, 5};
    click(10, 0); // different items swap
    EXPECT_EQ(at(10).item, &stone);
    EXPECT_EQ(at(10).count, 6);
    EXPECT_EQ(menu.carried().item, &dirt);
    EXPECT_EQ(menu.carried().count, 5);
    // Pearls stack to 16.
    at(11) = {&pearl, 10};
    menu.carried() = {&pearl, 10};
    click(11, 0);
    EXPECT_EQ(at(11).count, 16);
    EXPECT_EQ(menu.carried().count, 4);
}

TEST_F(MenuFixture, QuickMoveRoutes) {
    at(mainSlot(9)) = {&stone, 30};
    click(9, 0, ClickType::QuickMove); // main -> hotbar
    EXPECT_TRUE(at(9).empty());
    EXPECT_EQ(at(hotbarSlot(0)).count, 30);
    click(hotbarSlot(0), 0, ClickType::QuickMove); // hotbar -> main
    EXPECT_EQ(at(9).count, 30);
    // Partial stacks fill first.
    at(hotbarSlot(4)) = {&stone, 50};
    click(9, 0, ClickType::QuickMove);
    EXPECT_EQ(at(hotbarSlot(4)).count, 64);
    EXPECT_EQ(at(hotbarSlot(0)).count, 16);
    // Armor goes to its slot, shields to the off hand.
    at(20) = {&helmet, 1};
    click(20, 0, ClickType::QuickMove);
    EXPECT_EQ(player.inventory.armor[3].item, &helmet);
    at(21) = {&shield, 1};
    click(21, 0, ClickType::QuickMove);
    EXPECT_EQ(player.inventory.offhand.item, &shield);
    // The armor slot refuses other items.
    menu.carried() = {&dirt, 3};
    player.inventory.armor[3] = {};
    click(InventoryMenu::kArmorStart, 0);
    EXPECT_TRUE(player.inventory.armor[3].empty());
    EXPECT_EQ(menu.carried().count, 3);
}

TEST_F(MenuFixture, DragSplitting) {
    menu.carried() = {&stone, 64};
    drag(0, {9, 10, 11, 12}); // split evenly
    for (int s : {9, 10, 11, 12}) EXPECT_EQ(at(s).count, 16);
    EXPECT_TRUE(menu.carried().empty());
    menu.carried() = {&dirt, 64};
    drag(0, {13, 14, 15}); // 21 each, one left over
    for (int s : {13, 14, 15}) EXPECT_EQ(at(s).count, 21);
    EXPECT_EQ(menu.carried().count, 1);
    menu.carried() = {&dirt, 10};
    drag(1, {16, 17, 18}); // one each
    for (int s : {16, 17, 18}) EXPECT_EQ(at(s).count, 1);
    EXPECT_EQ(menu.carried().count, 7);
    // Clone drags need creative mode.
    menu.carried() = {&dirt, 2};
    drag(2, {19, 20});
    EXPECT_TRUE(at(19).empty());
    EXPECT_EQ(menu.carried().count, 2);
    player.setMode(GameMode::Creative);
    drag(2, {19, 20});
    EXPECT_EQ(at(19).count, 64);
    EXPECT_EQ(at(20).count, 64);
    EXPECT_TRUE(menu.carried().empty()); // vanilla subtracts the clones from the cursor
    // A drag over a single slot is an ordinary click.
    menu.carried() = {&stone, 5};
    drag(1, {21});
    EXPECT_EQ(at(21).count, 1);
    EXPECT_EQ(menu.carried().count, 4);
}

TEST_F(MenuFixture, DoubleClickCollects) {
    at(9) = {&stone, 20};
    at(10) = {&stone, 64};
    at(11) = {&stone, 5};
    at(12) = {&dirt, 5};
    menu.carried() = {&stone, 10};
    click(13, 0, ClickType::PickupAll); // partial stacks first, then full ones
    EXPECT_EQ(menu.carried().count, 64);
    EXPECT_TRUE(at(9).empty());
    EXPECT_TRUE(at(11).empty());
    EXPECT_EQ(at(10).count, 64 - 29);
    EXPECT_EQ(at(12).count, 5);
}

TEST_F(MenuFixture, NumberKeysThrowAndOutside) {
    at(9) = {&stone, 12};
    at(hotbarSlot(2)) = {&dirt, 3};
    click(9, 2, ClickType::Swap); // key 3
    EXPECT_EQ(at(9).item, &dirt);
    EXPECT_EQ(player.inventory.main[2].item, &stone);
    click(9, Inventory::kOffhandSlot, ClickType::Swap); // F
    EXPECT_EQ(player.inventory.offhand.item, &dirt);
    EXPECT_TRUE(at(9).empty());

    at(10) = {&stone, 10};
    click(10, 0, ClickType::Throw); // Q: one
    EXPECT_EQ(at(10).count, 9);
    click(10, 1, ClickType::Throw); // Ctrl+Q: the stack
    EXPECT_TRUE(at(10).empty());
    ASSERT_EQ(player.droppedItems.size(), 2u);
    EXPECT_EQ(player.droppedItems[0].count, 1);
    EXPECT_EQ(player.droppedItems[1].count, 9);

    menu.carried() = {&stone, 6};
    click(Menu::kClickedOutside, 1); // right click outside drops one
    EXPECT_EQ(menu.carried().count, 5);
    click(Menu::kClickedOutside, 0);
    EXPECT_TRUE(menu.carried().empty());
    EXPECT_EQ(player.droppedItems.size(), 4u);
}

TEST_F(MenuFixture, CloneAndClosing) {
    at(9) = {&stone, 3};
    click(9, 2, ClickType::Clone); // survival: nothing
    EXPECT_TRUE(menu.carried().empty());
    player.setMode(GameMode::Creative);
    click(9, 2, ClickType::Clone);
    EXPECT_EQ(menu.carried().count, 64);
    // Closing returns the cursor and the crafting grid to the inventory.
    menu.carried() = {&dirt, 4};
    menu.craftGrid[1] = {&stone, 2};
    menu.removed(player);
    EXPECT_TRUE(menu.carried().empty());
    EXPECT_TRUE(menu.craftGrid[1].empty());
    int dirtCount = 0;
    for (const ItemStack& s : player.inventory.main)
        if (s.item == &dirt) dirtCount += s.count;
    EXPECT_EQ(dirtCount, 4);
    EXPECT_TRUE(player.droppedItems.empty());
}
