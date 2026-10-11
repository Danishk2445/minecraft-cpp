// Inventory.h -- item stacks and the player inventory (vanilla Inventory slot
// layout: 0-8 hotbar, 9-35 main, armor, offhand).
#pragma once

#include <array>

#include "game/Item.h"

namespace mc {

struct ItemStack {
    const Item* item = nullptr;
    int count = 0;

    bool empty() const { return !item || count <= 0; }
    bool sameItem(const ItemStack& o) const { return item == o.item; }
    int spaceLeft() const { return empty() ? 0 : item->maxStack - count; }
};

class Inventory {
public:
    static constexpr int kMainSize = 36;
    static constexpr int kHotbarSize = 9;

    std::array<ItemStack, kMainSize> main{}; // 0..8 are the hotbar
    std::array<ItemStack, 4> armor{};        // feet, legs, chest, head
    ItemStack offhand;
    int selected = 0;

    ItemStack& selectedStack() { return main[size_t(selected)]; }
    const ItemStack& selectedStack() const { return main[size_t(selected)]; }

    // Vanilla add: top up matching stacks (selected slot, then 0..35), then fill empty
    // slots in order. Returns how many did not fit.
    int add(const Item& item, int count);
    // Creative pick-block: select the item if the hotbar has it, else put a stack in the
    // selected slot when empty, else the first empty hotbar slot, else the selected slot.
    void pick(const Item& item);
    // Removes up to `count` from the selected stack (consumed by placing).
    void consumeSelected(int count = 1);
};

} // namespace mc
