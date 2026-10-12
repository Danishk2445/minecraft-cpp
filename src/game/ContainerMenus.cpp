#include "game/ContainerMenus.h"

#include <utility>

#include "entity/ExperienceOrb.h"
#include "entity/Player.h"
#include "resources/ResourcePack.h"
#include "world/level/Level.h"

namespace mc {

CraftingMenu::CraftingMenu(Inventory& inventory, const RecipeBook* recipes) : recipes_(recipes) {
    addSlot(&result, 0, 124, 35).kind = Slot::Kind::Result;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) addSlot(&grid[size_t(j + i * 3)], j + i * 3, 30 + j * 18, 17 + i * 18);
    addPlayerSlots(inventory, 84);
}

void CraftingMenu::slotsChanged() { result = craftingResult(recipes_, grid, 3, 3); }

void CraftingMenu::onResultTaken(Player& player, const ItemStack&) {
    consumeCraft(recipes_, grid, player);
    slotsChanged();
}

ItemStack CraftingMenu::quickMoveStack(Player& player, int index) {
    Slot& slot = slots[size_t(index)];
    if (!slot.hasItem()) return {};
    ItemStack& stack = *slot.stack;
    const ItemStack original = stack;
    if (index == kResult) {
        if (!moveItemStackTo(stack, kInvStart, kHotbarEnd, true)) return {};
        if (!stack.empty()) player.drop(std::exchange(stack, ItemStack{})); // what didn't fit falls out
        onResultTaken(player, original);
        return original;
    }
    if (index >= kInvStart && index < kHotbarEnd) {
        // From the inventory: into the grid first, then between inventory and hotbar.
        if (!moveItemStackTo(stack, kGridStart, kGridEnd, false)) {
            if (index < kHotbarStart) {
                if (!moveItemStackTo(stack, kHotbarStart, kHotbarEnd, false)) return {};
            } else if (!moveItemStackTo(stack, kInvStart, kInvEnd, false)) {
                return {};
            }
        }
    } else if (!moveItemStackTo(stack, kInvStart, kHotbarEnd, false)) {
        return {};
    }
    if (!stack.empty() && stack.count == original.count) return {};
    return original;
}

void CraftingMenu::returnItems(Player& player) {
    Menu::returnItems(player);
    result = {};
    for (ItemStack& s : grid) {
        if (!s.empty() && !player.inventory.add(s)) player.drop(s);
        s = {};
    }
}

ChestMenu::ChestMenu(Inventory& inventory, std::vector<std::shared_ptr<ContainerBlockEntity>> halves,
                     std::span<ItemStack> enderSlots, std::shared_ptr<ContainerBlockEntity> lid)
    : halves_(std::move(halves)), lid_(std::move(lid)) {
    std::vector<ItemStack*> storage;
    for (const auto& half : halves_)
        for (ItemStack& s : half->items) storage.push_back(&s);
    for (ItemStack& s : enderSlots) storage.push_back(&s);
    rows_ = int(storage.size()) / 9;
    for (const auto& half : halves_) ++half->viewers;
    if (lid_) ++lid_->viewers;
    for (int r = 0; r < rows_; ++r)
        for (int c = 0; c < 9; ++c) addSlot(storage[size_t(r * 9 + c)], r * 9 + c, 8 + c * 18, 18 + r * 18);
    // The inventory sits 103 + (rows - 4) * 18 down.
    addPlayerSlots(inventory, 103 + (rows_ - 4) * 18);
}

ItemStack ChestMenu::quickMoveStack(Player&, int index) {
    Slot& slot = slots[size_t(index)];
    if (!slot.hasItem()) return {};
    ItemStack& stack = *slot.stack;
    const ItemStack original = stack;
    const int container = rows_ * 9;
    if (index < container) {
        if (!moveItemStackTo(stack, container, int(slots.size()), true)) return {};
    } else if (!moveItemStackTo(stack, 0, container, false)) {
        return {};
    }
    return original;
}

void ChestMenu::removed(Player& player) {
    Menu::removed(player);
    for (const auto& half : halves_) half->viewers = std::max(0, half->viewers - 1);
    if (lid_) lid_->viewers = std::max(0, lid_->viewers - 1);
}

namespace {

// Vanilla FurnaceFuelSlot: fuel, or an empty bucket (one at a time).
struct FuelRules {
    static bool mayPlace(const ItemStack& s) {
        return FurnaceBlockEntity::isFuel(s) || (!s.empty() && shortId(s.item->id) == "bucket");
    }
};

} // namespace

FurnaceMenu::FurnaceMenu(Inventory& inventory, std::shared_ptr<FurnaceBlockEntity> furnace, Level* level)
    : furnace_(std::move(furnace)), level_(level) {
    addSlot(&furnace_->items[FurnaceBlockEntity::kInput], 0, 56, 17);
    Slot& fuel = addSlot(&furnace_->items[FurnaceBlockEntity::kFuel], 1, 56, 53);
    fuel.kind = Slot::Kind::Fuel;
    addSlot(&furnace_->items[FurnaceBlockEntity::kOutput], 2, 116, 35).kind = Slot::Kind::Output;
    addPlayerSlots(inventory, 84);
}

void FurnaceMenu::onResultTaken(Player& player, const ItemStack&) {
    // Vanilla FurnaceResultSlot: the experience gathered so far, as orbs at the player.
    if (!level_) return;
    if (const int xp = furnace_->takeExperience(level_->random().nextFloat()); xp > 0)
        ExperienceOrb::award(*level_, player.pos, xp);
}

ItemStack FurnaceMenu::quickMoveStack(Player& player, int index) {
    Slot& slot = slots[size_t(index)];
    if (!slot.hasItem()) return {};
    ItemStack& stack = *slot.stack;
    const ItemStack original = stack;
    if (index == kOutput) {
        if (!moveItemStackTo(stack, kInvStart, kHotbarEnd, true)) return {};
        onResultTaken(player, original);
    } else if (index != kFuel && index != kInput) {
        // From the inventory: cookable things to the input, fuel to the fuel slot.
        if (furnace_->recipeFor(stack)) {
            if (!moveItemStackTo(stack, kInput, kInput + 1, false)) return {};
        } else if (FuelRules::mayPlace(stack)) {
            if (!moveItemStackTo(stack, kFuel, kFuel + 1, false)) return {};
        } else if (index >= kInvStart && index < kInvEnd) {
            if (!moveItemStackTo(stack, kHotbarStart, kHotbarEnd, false)) return {};
        } else if (index >= kHotbarStart && index < kHotbarEnd && !moveItemStackTo(stack, kInvStart, kInvEnd, false)) {
            return {};
        }
    } else if (!moveItemStackTo(stack, kInvStart, kHotbarEnd, false)) {
        return {};
    }
    if (!stack.empty() && stack.count == original.count) return {};
    return original;
}

} // namespace mc
