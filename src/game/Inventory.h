// Inventory.h -- item stacks and the player inventory (vanilla Inventory slot
// layout: 0-8 hotbar, 9-35 main, armor, offhand).
#pragma once

#include <algorithm>
#include <array>

#include "game/Item.h"

namespace mc {

struct ItemStack {
    const Item* item = nullptr;
    int count = 0;

    bool empty() const { return !item || count <= 0; }
    // Same item (and, once items carry data, the same data: vanilla isSameItemSameComponents).
    bool sameItem(const ItemStack& o) const { return item == o.item; }
    int maxStack() const { return item ? item->maxStack : 64; }
    int spaceLeft() const { return empty() ? 0 : item->maxStack - count; }
    ItemStack copyWithCount(int n) const { return n > 0 && item ? ItemStack{item, n} : ItemStack{}; }
    // Takes up to n off this stack (vanilla split).
    ItemStack split(int n) {
        const int taken = std::min(std::max(n, 0), empty() ? 0 : count);
        const ItemStack out = copyWithCount(taken);
        shrink(taken);
        return out;
    }
    void grow(int n) { count += n; }
    void shrink(int n) {
        count -= n;
        if (count <= 0) *this = {};
    }
};

class Inventory {
public:
    static constexpr int kMainSize = 36;
    static constexpr int kHotbarSize = 9;

    std::array<ItemStack, kMainSize> main{}; // 0..8 are the hotbar
    std::array<ItemStack, 4> armor{};        // feet, legs, chest, head
    ItemStack offhand;
    int selected = 0;

    static constexpr int kOffhandSlot = 40;

    ItemStack& selectedStack() { return main[size_t(selected)]; }
    const ItemStack& selectedStack() const { return main[size_t(selected)]; }
    // Vanilla slot numbers: 0-35 main (0-8 hotbar), 36-39 armor (feet to head), 40 offhand.
    ItemStack& slot(int i) { return i < kMainSize ? main[size_t(i)] : i < kOffhandSlot ? armor[size_t(i - kMainSize)] : offhand; }

    // Vanilla add: top up matching stacks (selected slot, then 0..35), then fill empty
    // slots in order. Returns how many did not fit.
    int add(const Item& item, int count);
    // The same for a stack, which keeps what did not fit.
    bool add(ItemStack& stack) {
        if (stack.empty()) return false;
        stack.count = add(*stack.item, stack.count);
        if (stack.count <= 0) stack = {};
        return stack.empty();
    }
    // Creative pick-block: select the item if the hotbar has it, else put a stack in the
    // selected slot when empty, else the first empty hotbar slot, else the selected slot.
    void pick(const Item& item);
    // Removes up to `count` from the selected stack (consumed by placing).
    void consumeSelected(int count = 1);
};

} // namespace mc
