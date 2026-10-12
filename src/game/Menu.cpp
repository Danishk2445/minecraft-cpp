#include "game/Menu.h"

#include <algorithm>
#include <utility>

#include "entity/Player.h"
#include "game/Recipes.h"

namespace mc {

int Slot::maxStackSize(const ItemStack& s) const {
    // A bucket in a fuel slot sits alone (vanilla FurnaceFuelSlot).
    if (kind == Kind::Fuel && !s.empty() && s.item->fuelTicks == 0) return 1;
    return std::min(maxStackSize(), s.maxStack());
}

bool Slot::mayPlace(const ItemStack& s) const {
    switch (kind) {
    case Kind::Result:
    case Kind::Output: return false;
    case Kind::Fuel: return !s.empty() && (s.item->fuelTicks > 0 || s.item->id == "minecraft:bucket");
    case Kind::Armor: return !s.empty() && s.item->equipment == armor;
    case Kind::Creative: return false; // vanilla ItemPickerMenu.canDragTo: never into the picker
    default: return true;
    }
}

ItemStack Slot::safeInsert(ItemStack s, int increment) {
    if (s.empty() || !mayPlace(s)) return s;
    const int n = std::min(std::min(increment, s.count), maxStackSize(s) - (hasItem() ? stack->count : 0));
    if (n <= 0) return s;
    if (!hasItem()) *stack = s.split(n);
    else if (stack->sameItem(s)) {
        s.shrink(n);
        stack->grow(n);
    }
    return s;
}

ItemStack Slot::tryRemove(int count, int decrement) {
    if (!mayPickup() || !hasItem()) return {};
    // A result comes out whole, or not at all (vanilla ResultContainer.removeItem).
    if (kind == Kind::Result) return decrement < stack->count ? ItemStack{} : std::exchange(*stack, ItemStack{});
    return stack->split(std::min(count, decrement));
}

ItemStack Menu::take(Slot& slot, int count, int decrement, Player& player) {
    ItemStack taken = slot.tryRemove(count, decrement);
    if (!taken.empty() && (slot.kind == Slot::Kind::Result || slot.kind == Slot::Kind::Output))
        onResultTaken(player, taken);
    return taken;
}

ItemStack craftingResult(const RecipeBook* recipes, std::span<const ItemStack> grid, int width, int height) {
    return recipes ? recipes->craft(grid, width, height).result : ItemStack{};
}

void consumeCraft(const RecipeBook* recipes, std::span<ItemStack> grid, Player& player) {
    for (ItemStack& cell : grid) {
        if (cell.empty()) continue;
        ItemStack left = recipes ? recipes->remainder(cell) : ItemStack{};
        cell.shrink(1);
        if (left.empty()) continue;
        if (cell.empty()) cell = left;
        else if (cell.sameItem(left)) cell.grow(left.count);
        else if (!player.inventory.add(left)) player.drop(left);
    }
}

Slot& Menu::addSlot(ItemStack* storage, int containerSlot, int x, int y, bool playerInventory) {
    Slot& s = slots.emplace_back();
    s.stack = storage;
    s.index = int(slots.size()) - 1;
    s.containerSlot = containerSlot;
    s.x = x;
    s.y = y;
    s.playerInventory = playerInventory;
    return s;
}

void Menu::addPlayerSlots(Inventory& inventory, int top) {
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 9; ++j) {
            const int n = j + (i + 1) * 9;
            addSlot(&inventory.main[size_t(n)], n, 8 + j * 18, top + i * 18, true);
        }
    for (int j = 0; j < 9; ++j) addSlot(&inventory.main[size_t(j)], j, 8 + j * 18, top + 58, true);
}

void Menu::removed(Player& player) {
    closed_ = true;
    returnItems(player);
}

void Menu::returnItems(Player& player) {
    // Vanilla: whatever the cursor holds goes back to the inventory, or is dropped.
    if (!carried().empty()) {
        if (!player.inventory.add(carried())) player.drop(carried());
        carried() = {};
    }
}

int Menu::quickcraftPlaceCount(size_t slotCount, int type, const ItemStack& stack) {
    switch (type) {
    case 0: return int(float(stack.count) / float(std::max<size_t>(slotCount, 1)));
    case 1: return 1;
    case 2: return stack.maxStack();
    default: return stack.count;
    }
}

bool Menu::canItemQuickReplace(const Slot* slot, const ItemStack& stack, bool stackSizeMatters) {
    const bool empty = !slot || !slot->hasItem();
    if (!empty && stack.sameItem(slot->item()))
        return slot->item().count + (stackSizeMatters ? 0 : stack.count) <= stack.maxStack();
    return empty;
}

void Menu::resetQuickCraft() {
    quickcraftStatus_ = 0;
    quickcraftSlots_.clear();
}

void Menu::clicked(int slotId, int button, ClickType type, Player& player) {
    if (slotId != kClickedOutside && (slotId < -1 || slotId >= int(slots.size()))) return;
    doClick(slotId, button, type, player);
    slotsChanged();
}

void Menu::doClick(int slotId, int button, ClickType type, Player& player) {
    Inventory& inventory = player.inventory;
    if (type == ClickType::QuickCraft) {
        // Dragging: start (with the type), each slot entered, end.
        const int previous = quickcraftStatus_;
        quickcraftStatus_ = quickcraftHeader(button);
        if ((previous != 1 || quickcraftStatus_ != 2) && previous != quickcraftStatus_) {
            resetQuickCraft();
        } else if (carried().empty()) {
            resetQuickCraft();
        } else if (quickcraftStatus_ == 0) {
            quickcraftType_ = quickcraftType(button);
            // Clone drags need infinite materials (creative).
            const bool valid = quickcraftType_ == 0 || quickcraftType_ == 1 ||
                               (quickcraftType_ == 2 && player.mode() == GameMode::Creative);
            if (valid) {
                quickcraftStatus_ = 1;
                quickcraftSlots_.clear();
            } else {
                resetQuickCraft();
            }
        } else if (quickcraftStatus_ == 1) {
            if (slotId < 0) return;
            const Slot& slot = slots[size_t(slotId)];
            if (canItemQuickReplace(&slot, carried(), true) && slot.mayPlace(carried()) &&
                (quickcraftType_ == 2 || carried().count > int(quickcraftSlots_.size())) &&
                std::find(quickcraftSlots_.begin(), quickcraftSlots_.end(), slotId) == quickcraftSlots_.end())
                quickcraftSlots_.push_back(slotId);
        } else if (quickcraftStatus_ == 2) {
            if (!quickcraftSlots_.empty()) {
                if (quickcraftSlots_.size() == 1) {
                    // One slot: an ordinary click with the drag's button.
                    const int only = quickcraftSlots_[0];
                    const int dragType = quickcraftType_;
                    resetQuickCraft();
                    doClick(only, dragType, ClickType::Pickup, player);
                    return;
                }
                ItemStack source = carried();
                if (source.empty()) {
                    resetQuickCraft();
                    return;
                }
                int remaining = carried().count;
                for (int id : quickcraftSlots_) {
                    Slot& slot = slots[size_t(id)];
                    if (canItemQuickReplace(&slot, carried(), true) && slot.mayPlace(carried()) &&
                        (quickcraftType_ == 2 || carried().count >= int(quickcraftSlots_.size()))) {
                        const int already = slot.hasItem() ? slot.item().count : 0;
                        const int limit = std::min(source.maxStack(), slot.maxStackSize(source));
                        const int n =
                            std::min(quickcraftPlaceCount(quickcraftSlots_.size(), quickcraftType_, source) + already, limit);
                        remaining -= n - already;
                        *slot.stack = source.copyWithCount(n);
                    }
                }
                source.count = remaining;
                carried() = source.count > 0 ? source : ItemStack{};
            }
            resetQuickCraft();
        } else {
            resetQuickCraft();
        }
        return;
    }
    if (quickcraftStatus_ != 0) {
        resetQuickCraft();
        return;
    }

    if ((type == ClickType::Pickup || type == ClickType::QuickMove) && (button == 0 || button == 1)) {
        const bool primary = button == 0;
        if (slotId == kClickedOutside) {
            // Outside the window: drop the cursor stack (right click: one item).
            if (!carried().empty()) {
                if (primary) {
                    player.drop(carried());
                    carried() = {};
                } else {
                    player.drop(carried().split(1));
                }
            }
        } else if (type == ClickType::QuickMove) {
            if (slotId < 0) return;
            Slot& slot = slots[size_t(slotId)];
            if (!slot.mayPickup()) return;
            ItemStack moved = quickMoveStack(player, slotId);
            while (!moved.empty() && slot.item().sameItem(moved)) moved = quickMoveStack(player, slotId);
        } else {
            if (slotId < 0) return;
            Slot& slot = slots[size_t(slotId)];
            const ItemStack inSlot = slot.item();
            if (inSlot.empty()) {
                if (!carried().empty()) carried() = slot.safeInsert(carried(), primary ? carried().count : 1);
            } else if (slot.mayPickup()) {
                if (carried().empty()) {
                    // Take all, or half rounded up.
                    carried() = take(slot, primary ? inSlot.count : (inSlot.count + 1) / 2, 0x7FFFFFFF, player);
                } else if (slot.mayPlace(carried())) {
                    if (inSlot.sameItem(carried())) {
                        carried() = slot.safeInsert(carried(), primary ? carried().count : 1);
                    } else if (carried().count <= slot.maxStackSize(carried())) {
                        // Swap the cursor with the slot.
                        *slot.stack = carried();
                        carried() = inSlot;
                    }
                } else if (inSlot.sameItem(carried())) {
                    // A slot that takes nothing (a result) tops up the cursor.
                    const ItemStack taken = take(slot, inSlot.count, carried().maxStack() - carried().count, player);
                    carried().grow(taken.count);
                }
            }
        }
    } else if (type == ClickType::Swap && ((button >= 0 && button < 9) || button == Inventory::kOffhandSlot)) {
        if (slotId < 0) return;
        // Number keys swap the slot with that hotbar slot; F with the off hand.
        ItemStack& other = inventory.slot(button);
        Slot& slot = slots[size_t(slotId)];
        const ItemStack inSlot = slot.item();
        if (other.empty() && inSlot.empty()) return;
        if (other.empty()) {
            if (slot.mayPickup()) {
                other = inSlot;
                *slot.stack = {};
                if (slot.kind == Slot::Kind::Result || slot.kind == Slot::Kind::Output) onResultTaken(player, inSlot);
            }
        } else if (inSlot.empty()) {
            if (slot.mayPlace(other)) {
                const int limit = slot.maxStackSize(other);
                if (other.count > limit) *slot.stack = other.split(limit);
                else *slot.stack = std::exchange(other, ItemStack{});
            }
        } else if (slot.mayPickup() && slot.mayPlace(other)) {
            const int limit = slot.maxStackSize(other);
            if (other.count > limit) {
                *slot.stack = other.split(limit);
                ItemStack back = inSlot;
                if (!inventory.add(back)) player.drop(back);
            } else {
                *slot.stack = other;
                other = inSlot;
            }
        }
    } else if (type == ClickType::Clone && player.mode() == GameMode::Creative && carried().empty() && slotId >= 0) {
        // Middle click in creative: a full stack of the slot's item.
        const Slot& slot = slots[size_t(slotId)];
        if (slot.hasItem()) carried() = slot.item().copyWithCount(slot.item().maxStack());
    } else if (type == ClickType::Throw && carried().empty() && slotId >= 0) {
        // Q throws one item; Ctrl+Q the stack (and keeps going while the slot refills).
        Slot& slot = slots[size_t(slotId)];
        const int n = button == 0 ? 1 : slot.item().count;
        ItemStack thrown = take(slot, n, 0x7FFFFFFF, player);
        player.drop(thrown);
        if (button == 1)
            while (!thrown.empty() && slot.item().sameItem(thrown)) {
                thrown = take(slot, n, 0x7FFFFFFF, player);
                player.drop(thrown);
            }
    } else if (type == ClickType::PickupAll && slotId >= 0) {
        // Double click: gather matching items, partial stacks first.
        const Slot& clickedSlot = slots[size_t(slotId)];
        if (!carried().empty() && (!clickedSlot.hasItem() || !clickedSlot.mayPickup())) {
            const int start = button == 0 ? 0 : int(slots.size()) - 1, step = button == 0 ? 1 : -1;
            for (int pass = 0; pass < 2; ++pass)
                for (int i = start; i >= 0 && i < int(slots.size()) && carried().count < carried().maxStack(); i += step) {
                    Slot& s = slots[size_t(i)];
                    if (s.hasItem() && canItemQuickReplace(&s, carried(), true) && s.mayPickup() &&
                        s.kind != Slot::Kind::Result) {
                        const ItemStack& there = s.item();
                        if (pass != 0 || there.count != there.maxStack())
                            carried().grow(s.tryRemove(there.count, carried().maxStack() - carried().count).count);
                    }
                }
        }
    }
}

bool Menu::moveItemStackTo(ItemStack& stack, int start, int end, bool reverse) {
    bool moved = false;
    if (stack.maxStack() > 1) {
        for (int i = reverse ? end - 1 : start; !stack.empty() && (reverse ? i >= start : i < end); i += reverse ? -1 : 1) {
            Slot& slot = slots[size_t(i)];
            ItemStack& there = *slot.stack;
            if (!there.empty() && there.sameItem(stack)) {
                const int total = there.count + stack.count, limit = slot.maxStackSize(there);
                if (total <= limit) {
                    stack = {};
                    there.count = total;
                    moved = true;
                } else if (there.count < limit) {
                    stack.shrink(limit - there.count);
                    there.count = limit;
                    moved = true;
                }
            }
        }
    }
    if (!stack.empty()) {
        for (int i = reverse ? end - 1 : start; reverse ? i >= start : i < end; i += reverse ? -1 : 1) {
            Slot& slot = slots[size_t(i)];
            if (slot.item().empty() && slot.mayPlace(stack)) {
                *slot.stack = stack.split(std::min(stack.count, slot.maxStackSize(stack)));
                moved = true;
                break;
            }
        }
    }
    return moved;
}

InventoryMenu::InventoryMenu(Inventory& inventory, const RecipeBook* recipes)
    : inventory_(inventory), recipes_(recipes) {
    addSlot(&craftResult, 0, 154, 28).kind = Slot::Kind::Result;
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j) addSlot(&craftGrid[size_t(j + i * 2)], j + i * 2, 98 + j * 18, 18 + i * 18);
    static constexpr EquipmentSlot kArmor[4] = {EquipmentSlot::Head, EquipmentSlot::Chest, EquipmentSlot::Legs,
                                                EquipmentSlot::Feet};
    for (int i = 0; i < 4; ++i) {
        // Inventory armor runs feet (36) to head (39).
        Slot& s = addSlot(&inventory.armor[size_t(3 - i)], 39 - i, 8, 8 + i * 18, true);
        s.kind = Slot::Kind::Armor;
        s.armor = kArmor[i];
    }
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 9; ++j) {
            const int n = j + (i + 1) * 9;
            addSlot(&inventory.main[size_t(n)], n, 8 + j * 18, 84 + i * 18, true);
        }
    for (int j = 0; j < 9; ++j) addSlot(&inventory.main[size_t(j)], j, 8 + j * 18, 142, true);
    addSlot(&inventory.offhand, Inventory::kOffhandSlot, 77, 62, true);
}

ItemStack InventoryMenu::quickMoveStack(Player& player, int index) {
    Slot& slot = slots[size_t(index)];
    if (!slot.hasItem()) return {};
    ItemStack& stack = *slot.stack;
    const ItemStack original = stack;
    const EquipmentSlot equip = stack.item->equipment;
    const bool armor = equip == EquipmentSlot::Head || equip == EquipmentSlot::Chest || equip == EquipmentSlot::Legs ||
                       equip == EquipmentSlot::Feet;
    // Armor slot for each equipment piece: head 5, chest 6, legs 7, feet 8.
    const int armorSlot = equip == EquipmentSlot::Head ? 5 : equip == EquipmentSlot::Chest ? 6 : equip == EquipmentSlot::Legs ? 7 : 8;
    if (index == kResult) {
        // Shift-clicking the result crafts once per call (the click repeats it while the
        // result stays the same): it goes into the inventory from the hotbar's end.
        if (!moveItemStackTo(stack, kMainStart, kHotbarEnd, true)) return {};
        if (!stack.empty()) player.drop(std::exchange(stack, ItemStack{})); // what didn't fit falls out
        onResultTaken(player, original);
        return original;
    } else if (index >= kCraftStart && index < kCraftEnd) {
        if (!moveItemStackTo(stack, kMainStart, kHotbarEnd, false)) return {};
    } else if (index >= kArmorStart && index < kArmorEnd) {
        if (!moveItemStackTo(stack, kMainStart, kHotbarEnd, false)) return {};
    } else if (armor && !slots[size_t(armorSlot)].hasItem()) {
        if (!moveItemStackTo(stack, armorSlot, armorSlot + 1, false)) return {};
    } else if (equip == EquipmentSlot::OffHand && !slots[kOffhand].hasItem()) {
        if (!moveItemStackTo(stack, kOffhand, kOffhand + 1, false)) return {};
    } else if (index >= kMainStart && index < kMainEnd) {
        if (!moveItemStackTo(stack, kHotbarStart, kHotbarEnd, false)) return {};
    } else if (index >= kHotbarStart && index < kHotbarEnd) {
        if (!moveItemStackTo(stack, kMainStart, kMainEnd, false)) return {};
    } else if (!moveItemStackTo(stack, kMainStart, kHotbarEnd, false)) {
        return {};
    }
    if (!stack.empty() && stack.count == original.count) return {};
    (void)player;
    return original;
}

void InventoryMenu::slotsChanged() { craftResult = craftingResult(recipes_, craftGrid, 2, 2); }

void InventoryMenu::onResultTaken(Player& player, const ItemStack&) {
    consumeCraft(recipes_, craftGrid, player);
    slotsChanged();
}

void InventoryMenu::returnItems(Player& player) {
    Menu::returnItems(player);
    craftResult = {};
    for (ItemStack& s : craftGrid) {
        if (!s.empty() && !inventory_.add(s)) player.drop(s);
        s = {};
    }
}

CreativeMenu::CreativeMenu(Inventory& inventory, InventoryMenu& inventoryMenu)
    : inventory_(inventory), inventoryMenu_(inventoryMenu) {
    shareCarried(inventoryMenu.carried());
}

void CreativeMenu::showItems(const std::vector<const Item*>& items) {
    items_ = items;
    inventoryLayout_ = false;
    slots.clear();
    for (int i = 0; i < kGridSlots; ++i)
        addSlot(&grid_[size_t(i)], i, 9 + (i % kColumns) * 18, 18 + (i / kColumns) * 18).kind = Slot::Kind::Creative;
    for (int j = 0; j < Inventory::kHotbarSize; ++j) addSlot(&inventory_.main[size_t(j)], j, 9 + j * 18, 112, true);
    scrollTo(0.0f);
}

void CreativeMenu::showInventory() {
    inventoryLayout_ = true;
    items_.clear();
    slots.clear();
    // CreativeModeInventoryScreen.selectTab(INVENTORY): the same slots, moved.
    for (const Slot& original : inventoryMenu_.slots) {
        Slot s = original;
        const int l = original.index;
        if (l >= InventoryMenu::kArmorStart && l < InventoryMenu::kArmorEnd) {
            const int o = l - InventoryMenu::kArmorStart;
            s.x = 54 + (o / 2) * 54;
            s.y = 6 + (o % 2) * 27;
        } else if (l < InventoryMenu::kCraftEnd) {
            s.x = s.y = -2000; // the crafting grid is hidden
        } else if (l == InventoryMenu::kOffhand) {
            s.x = 35;
            s.y = 20;
        } else {
            const int o = l - InventoryMenu::kMainStart;
            s.x = 9 + (o % 9) * 18;
            s.y = l >= InventoryMenu::kHotbarStart ? 112 : 54 + (o / 9) * 18;
        }
        slots.push_back(s);
    }
    addSlot(&destroy_, 0, 173, 112);
}

int CreativeMenu::rowsBeyondView() const {
    const int rows = (int(items_.size()) + kColumns - 1) / kColumns;
    return std::max(0, rows - kRows);
}

void CreativeMenu::scrollTo(float offset) {
    scroll_ = std::clamp(offset, 0.0f, 1.0f);
    fillGrid();
}

void CreativeMenu::fillGrid() {
    if (inventoryLayout_) return;
    const int firstRow = int(scroll_ * float(rowsBeyondView()) + 0.5f);
    for (int i = 0; i < kGridSlots; ++i) {
        const size_t n = size_t(firstRow * kColumns + i);
        grid_[size_t(i)] = n < items_.size() ? ItemStack{items_[n], 1} : ItemStack{};
    }
}

ItemStack CreativeMenu::quickMoveStack(Player&, int index) {
    if (!inventoryLayout_ && index >= kGridSlots && index < int(slots.size())) *slots[size_t(index)].stack = {};
    return {};
}

} // namespace mc
