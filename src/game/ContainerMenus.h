// ContainerMenus.h -- the menus of blocks (vanilla CraftingMenu, ...): their slots over
// the block's storage and the player's inventory, and shift-click rules.
#pragma once

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "core/Math.h"
#include "game/BlockEntities.h"
#include "game/Menu.h"

namespace mc {

class Level;

// Where a block menu was opened, for closing it when the block goes or the player walks
// off (vanilla stillValid: within 8 blocks of the block's center).
struct MenuAccess {
    BlockPos pos{0, 0, 0};
    uint16_t block = 0; // the block that must still be there
};

// The crafting table (vanilla CraftingMenu): 0 the result, 1-9 the 3x3 grid, 10-36 the
// inventory, 37-45 the hotbar. The grid empties into the inventory when it closes.
class CraftingMenu : public Menu {
public:
    static constexpr int kResult = 0, kGridStart = 1, kGridEnd = 10, kInvStart = 10, kInvEnd = 37, kHotbarStart = 37,
                         kHotbarEnd = 46;

    CraftingMenu(Inventory& inventory, const RecipeBook* recipes);
    ItemStack quickMoveStack(Player& player, int index) override;
    void returnItems(Player& player) override;
    void slotsChanged() override;

    std::array<ItemStack, 9> grid;
    ItemStack result;
    std::optional<MenuAccess> access;

protected:
    void onResultTaken(Player& player, const ItemStack& taken) override;

private:
    const RecipeBook* recipes_;
};

// A chest, barrel or ender chest (vanilla ChestMenu): 9 x `rows` container slots (27 or,
// for a double chest, 54 over both halves), then the inventory and hotbar. Shift-click moves
// between the container and the player.
class ChestMenu : public Menu {
public:
    // `halves` hold the containers whose slots it shows, in order (the right half of a
    // double chest first). The ender chest passes the player's own 27 slots, and its block
    // entity (`lid`) to open while the menu is.
    ChestMenu(Inventory& inventory, std::vector<std::shared_ptr<ContainerBlockEntity>> halves,
              std::span<ItemStack> enderSlots = {}, std::shared_ptr<ContainerBlockEntity> lid = nullptr);
    ItemStack quickMoveStack(Player& player, int index) override;
    void removed(Player& player) override;
    int rows() const { return rows_; }
    std::optional<MenuAccess> access;
    std::string title; // "Chest", "Large Chest", "Barrel", "Ender Chest"

private:
    std::vector<std::shared_ptr<ContainerBlockEntity>> halves_;
    std::shared_ptr<ContainerBlockEntity> lid_;
    int rows_ = 3;
};

// Furnaces, smokers and blast furnaces (vanilla AbstractFurnaceMenu): 0 input, 1 fuel (fuel
// or buckets), 2 output, then the inventory. Taking the output gives the gathered experience.
class FurnaceMenu : public Menu {
public:
    static constexpr int kInput = 0, kFuel = 1, kOutput = 2, kInvStart = 3, kInvEnd = 30, kHotbarStart = 30,
                         kHotbarEnd = 39;
    FurnaceMenu(Inventory& inventory, std::shared_ptr<FurnaceBlockEntity> furnace, Level* level);
    ItemStack quickMoveStack(Player& player, int index) override;
    const FurnaceBlockEntity& furnace() const { return *furnace_; }
    std::optional<MenuAccess> access;

protected:
    void onResultTaken(Player& player, const ItemStack& taken) override;

private:
    std::shared_ptr<FurnaceBlockEntity> furnace_;
    Level* level_;
};

} // namespace mc
