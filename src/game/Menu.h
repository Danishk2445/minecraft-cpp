// Menu.h -- container menus with vanilla click semantics (AbstractContainerMenu):
// slots over item storage and the carried stack; pickup and split clicks,
// shift-click moves, drag splitting, double-click collecting, number-key and
// off-hand swaps, throwing, and the creative middle-click clone.
#pragma once

#include <array>
#include <span>
#include <vector>

#include "game/Inventory.h"

namespace mc {

class Player;
class RecipeBook;

enum class ClickType : uint8_t { Pickup, QuickMove, Swap, Clone, Throw, QuickCraft, PickupAll };

struct Slot {
    // Result: a crafting result (whole stacks, consumes the grid); Output: takes nothing
    // (a furnace's); Fuel: fuel or buckets.
    enum class Kind : uint8_t { Normal, Result, Armor, Creative, Output, Fuel };
    ItemStack* stack = nullptr; // the storage the slot shows
    int index = 0;              // in the menu
    int containerSlot = 0;      // in its container (the vanilla inventory number for player slots)
    int x = 0, y = 0;           // top-left of the 16x16 item area, GUI pixels from the screen's corner
    Kind kind = Kind::Normal;
    EquipmentSlot armor = EquipmentSlot::MainHand; // armor slots take only this equipment
    bool playerInventory = false;

    const ItemStack& item() const { return *stack; }
    bool hasItem() const { return !stack->empty(); }
    bool mayPlace(const ItemStack& s) const;
    bool mayPickup() const { return true; }
    // Containers hold up to 99 (vanilla Container.DEFAULT_MAX_STACK_SIZE); armor slots 1.
    int maxStackSize() const { return kind == Kind::Armor ? 1 : 99; }
    int maxStackSize(const ItemStack& s) const;
    // Vanilla safeInsert: moves up to `increment` of `s` in and returns what is left.
    ItemStack safeInsert(ItemStack s, int increment);
    // Vanilla tryRemove(count, decrement): up to min(count, decrement) items out.
    ItemStack tryRemove(int count, int decrement);
};

class Menu {
public:
    static constexpr int kClickedOutside = -999;
    // QUICK_CRAFT button encoding: header (start/continue/end) | type << 2
    // (0 charitable: split evenly, 1 greedy: one each, 2 clone: full stacks).
    static constexpr int quickcraftHeader(int button) { return button & 3; }
    static constexpr int quickcraftType(int button) { return (button >> 2) & 3; }
    static constexpr int quickcraftMask(int header, int type) { return (header & 3) | ((type & 3) << 2); }

    virtual ~Menu() = default;

    std::vector<Slot> slots;
    // The stack on the cursor (creative screens share the inventory menu's).
    ItemStack& carried() { return *carried_; }
    const ItemStack& carried() const { return *carried_; }

    // Vanilla AbstractContainerMenu.clicked; the menu then updates what depends on its slots.
    void clicked(int slotId, int button, ClickType type, Player& player);
    // Vanilla slotsChanged: a crafting grid recomputes its result.
    virtual void slotsChanged() {}
    // Vanilla quickMoveStack: shift-click moves; returns the moved stack (empty to stop).
    virtual ItemStack quickMoveStack(Player& player, int index) = 0;
    // Called when the screen closes: gives back what only lives in the menu (returnItems)
    // and marks it closed.
    virtual void removed(Player& player);
    // What only lives in the menu (the cursor stack, a crafting grid) goes into the
    // inventory, or is dropped; the menu stays open (a dying player's things drop with it).
    virtual void returnItems(Player& player);
    bool closed() const { return closed_; }

    // Drag state, for screens drawing the split preview.
    bool dragging() const { return quickcraftStatus_ == 1; }
    const std::vector<int>& dragSlots() const { return quickcraftSlots_; }
    int dragType() const { return quickcraftType_; }
    // Vanilla getQuickCraftPlaceCount.
    static int quickcraftPlaceCount(size_t slotCount, int type, const ItemStack& stack);
    // Vanilla canItemQuickReplace.
    static bool canItemQuickReplace(const Slot* slot, const ItemStack& stack, bool stackSizeMatters);

protected:
    // A result slot was taken from (vanilla ResultSlot.onTake): consume one craft.
    virtual void onResultTaken(Player& player, const ItemStack& taken) { (void)player, (void)taken; }
    Slot& addSlot(ItemStack* storage, int containerSlot, int x, int y, bool playerInventory = false);
    // The player's 27 inventory slots and 9 hotbar slots below a container, at vanilla's
    // offsets (the inventory at y `top`, the hotbar 58 lower).
    void addPlayerSlots(Inventory& inventory, int top);
    // Vanilla moveItemStackTo: top up matching stacks in [start, end), then the first empty slot.
    bool moveItemStackTo(ItemStack& stack, int start, int end, bool reverse);

    // Use another menu's cursor stack (vanilla ItemPickerMenu.getCarried).
    void shareCarried(ItemStack& stack) { carried_ = &stack; }

private:
    void doClick(int slotId, int button, ClickType type, Player& player);
    // tryRemove, then onTake for result slots.
    ItemStack take(Slot& slot, int count, int decrement, Player& player);
    ItemStack ownCarried_;
    ItemStack* carried_ = &ownCarried_;
    bool closed_ = false;
    void resetQuickCraft();
    int quickcraftType_ = -1;
    int quickcraftStatus_ = 0;
    std::vector<int> quickcraftSlots_;
};

// The player's own inventory screen (vanilla InventoryMenu): 0 crafting result,
// 1-4 the 2x2 grid, 5-8 armor (head to feet), 9-35 main, 36-44 hotbar, 45 off hand.
class InventoryMenu : public Menu {
public:
    static constexpr int kResult = 0, kCraftStart = 1, kCraftEnd = 5, kArmorStart = 5, kArmorEnd = 9;
    static constexpr int kMainStart = 9, kMainEnd = 36, kHotbarStart = 36, kHotbarEnd = 45, kOffhand = 45;

    InventoryMenu(Inventory& inventory, const RecipeBook* recipes = nullptr);
    ItemStack quickMoveStack(Player& player, int index) override;
    // The crafting grid empties into the inventory (vanilla clearContainer).
    void returnItems(Player& player) override;
    void slotsChanged() override;

    std::array<ItemStack, 4> craftGrid;
    ItemStack craftResult;

protected:
    void onResultTaken(Player& player, const ItemStack& taken) override;

private:
    Inventory& inventory_;
    const RecipeBook* recipes_;
};

// Crafting grids (vanilla CraftingMenu.slotChangedCraftingGrid and ResultSlot.onTake):
// the result for a grid, and one craft's worth of ingredients used up, each cell leaving
// its remainder (into the cell when it emptied, else the inventory, else the ground).
ItemStack craftingResult(const RecipeBook* recipes, std::span<const ItemStack> grid, int width, int height);
void consumeCraft(const RecipeBook* recipes, std::span<ItemStack> grid, Player& player);

// The creative inventory (vanilla ItemPickerMenu): a 9x5 window onto a tab's items
// above the hotbar, sharing the inventory menu's cursor stack. For the "Survival
// Inventory" tab its slots become the inventory menu's (same indices) at the creative
// layout, plus the destroy-item slot.
class CreativeMenu : public Menu {
public:
    static constexpr int kGridSlots = 45, kColumns = 9, kRows = 5;

    CreativeMenu(Inventory& inventory, InventoryMenu& inventoryMenu);
    // An item tab: the grid shows `items` (one of each) from the scroll position.
    void showItems(const std::vector<const Item*>& items);
    // The survival inventory tab.
    void showInventory();
    bool inventoryLayout() const { return inventoryLayout_; }
    int destroySlot() const { return inventoryLayout_ ? int(slots.size()) - 1 : -1; }

    // Scrolling the grid (vanilla scrollTo / subtractInputFromScroll).
    int rowsBeyondView() const;
    bool canScroll() const { return rowsBeyondView() > 0; }
    float scroll() const { return scroll_; }
    void scrollTo(float offset);

    // Shift-clicking a hotbar slot under an item tab deletes it (vanilla).
    ItemStack quickMoveStack(Player& player, int index) override;

private:
    void fillGrid();
    Inventory& inventory_;
    InventoryMenu& inventoryMenu_;
    std::array<ItemStack, kGridSlots> grid_;
    ItemStack destroy_;
    std::vector<const Item*> items_;
    float scroll_ = 0.0f;
    bool inventoryLayout_ = false;
};

} // namespace mc
