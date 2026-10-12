#include "game/Inventory.h"

#include <algorithm>

namespace mc {

int Inventory::add(const Item& item, int count) {
    auto topUp = [&](ItemStack& s) {
        if (count <= 0 || s.item != &item || s.count >= item.maxStack) return;
        const int n = std::min(count, item.maxStack - s.count);
        s.count += n;
        count -= n;
    };
    // Vanilla getSlotWithRemainingSpace: the selected slot, the off hand, then 0-35.
    topUp(selectedStack());
    topUp(offhand);
    for (ItemStack& s : main) topUp(s);
    for (ItemStack& s : main) {
        if (count <= 0) break;
        if (!s.empty()) continue;
        const int n = std::min(count, item.maxStack);
        s = {&item, n};
        count -= n;
    }
    return count;
}

void Inventory::pick(const Item& item) {
    for (int i = 0; i < kHotbarSize; ++i)
        if (main[size_t(i)].item == &item) {
            selected = i;
            return;
        }
    int slot = selected;
    if (!main[size_t(slot)].empty())
        for (int i = 0; i < kHotbarSize; ++i)
            if (main[size_t(i)].empty()) {
                slot = i;
                break;
            }
    main[size_t(slot)] = {&item, item.maxStack};
    selected = slot;
}

void Inventory::consumeSelected(int count) {
    ItemStack& s = selectedStack();
    s.count -= count;
    if (s.count <= 0) s = {};
}

Json stackToJson(const ItemStack& stack) {
    if (stack.empty()) return nullptr;
    Json j = {{"id", stack.item->id}, {"count", stack.count}};
    if (stack.damage > 0) j["damage"] = stack.damage;
    return j;
}

ItemStack stackFromJson(const Json& j, const ItemRegistry& items) {
    if (!j.is_object()) return {};
    const Item* item = items.find(j.value("id", std::string()));
    if (!item) return {};
    ItemStack stack{item, std::clamp(j.value("count", 1), 1, item->maxStack)};
    if (item->maxDamage > 0) stack.damage = std::clamp(j.value("damage", 0), 0, item->maxDamage - 1);
    return stack;
}

} // namespace mc
