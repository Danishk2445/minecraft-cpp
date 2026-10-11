#include "ui/CreativeScreen.h"

#include <algorithm>

#include <GLFW/glfw3.h>

#include "entity/Player.h"
#include "game/Item.h"
#include "resources/FontSet.h"
#include "ui/GuiGraphics.h"

namespace mc {
namespace {

constexpr float kScrollbarX = 175, kScrollbarTop = 18, kScrollbarHeight = 112;

std::string tabBackground(bool search, bool inventory) {
    return std::string("minecraft:gui/container/creative_inventory/") +
           (search ? "tab_item_search" : inventory ? "tab_inventory" : "tab_items");
}

} // namespace

CreativeScreen::CreativeScreen(ScreenContext ctx, Player& player, InventoryMenu& inventoryMenu, const CreativeTabs& tabs)
    : CreativeMenuHolder{CreativeMenu(player.inventory, inventoryMenu)},
      ContainerScreen(ctx, creativeMenu, player, ""), inventoryMenu_(inventoryMenu), tabs_(tabs) {
    imageWidth_ = 195;
    imageHeight_ = 136;
    for (const CreativeTab& t : tabs.tabs())
        if (!t.hidden) tabList_.push_back({&t, Tab::Kind::Items, ctx.translate(t.nameKey), t.icon, t.top, t.column, false});
    // The search and survival inventory tabs sit at the right end of each row.
    const Item* compass = ctx.items ? ctx.items->find("compass") : nullptr;
    const Item* chest = ctx.items ? ctx.items->find("chest") : nullptr;
    tabList_.push_back({nullptr, Tab::Kind::Search, ctx.translate("itemGroup.search"), compass, true, 6, true});
    tabList_.push_back({nullptr, Tab::Kind::Inventory, ctx.translate("itemGroup.inventory"), chest, false, 6, true});
    // The menu keeps the inventory menu's cursor; ContainerScreen closes it through removed().
}

void CreativeScreen::init(glm::vec2 size) {
    ContainerScreen::init(size);
    selectTab(std::clamp(lastTab, 0, int(tabList_.size()) - 1));
}

void CreativeScreen::removed() {
    // Like vanilla, the cursor stack goes back to the inventory (the shared one).
    inventoryMenu_.removed(player_);
}

float CreativeScreen::tabX(const Tab& t) const {
    return t.alignedRight ? float(imageWidth_ - 27 * (7 - t.column) + 1) : float(27 * t.column);
}

float CreativeScreen::tabY(const Tab& t) const { return t.top ? -32.0f : float(imageHeight_); }

bool CreativeScreen::overTab(const Tab& t, glm::vec2 mouse) const {
    const glm::vec2 m = mouse - glm::vec2(leftPos_, topPos_);
    return m.x >= tabX(t) && m.x <= tabX(t) + 26 && m.y >= tabY(t) && m.y <= tabY(t) + 32;
}

bool CreativeScreen::insideScrollbar(glm::vec2 mouse) const {
    const glm::vec2 m = mouse - glm::vec2(leftPos_, topPos_);
    return m.x >= kScrollbarX && m.y >= kScrollbarTop && m.x < kScrollbarX + 14 && m.y < kScrollbarTop + kScrollbarHeight;
}

bool CreativeScreen::clickedOutside(glm::vec2 mouse) const {
    if (!ContainerScreen::clickedOutside(mouse)) return false;
    return !overTab(tabList_[size_t(selected_)], mouse);
}

void CreativeScreen::selectTab(int index) {
    selected_ = index;
    lastTab = index;
    const Tab& t = tabList_[size_t(index)];
    scrolling_ = false;
    if (t.kind == Tab::Kind::Inventory) {
        creativeMenu.showInventory();
    } else if (t.kind == Tab::Kind::Search) {
        refreshSearch();
    } else {
        creativeMenu.showItems(t.data->items);
    }
}

void CreativeScreen::refreshSearch() {
    const ScreenContext ctx = ctx_;
    creativeMenu.showItems(tabs_.search(encodeUtf8(search_), [&ctx](const Item& item) {
        return ctx.items && ctx.lang ? ctx.items->name(item, *ctx.lang) : item.id;
    }));
}

void CreativeScreen::renderTab(GuiGraphics& g, const Tab& t, bool selected) {
    // CreativeModeInventoryScreen.renderTabButton
    const float x = leftPos_ + tabX(t), y = topPos_ + (t.top ? -28.0f : float(imageHeight_ - 4));
    const std::string sprite = std::string("container/creative_inventory/tab_") + (t.top ? "top" : "bottom") +
                               (selected ? "_selected_" : "_unselected_") + std::to_string(std::clamp(t.column, 0, 6) + 1);
    g.sprite(sprite, x, y, 26, 32);
    if (t.icon) g.item(ItemStack{t.icon, 1}, x + 13 - 8, y + 16 - 8 + (t.top ? 1.0f : -1.0f));
}

void CreativeScreen::renderBg(GuiGraphics& g, glm::vec2 mouse, float) {
    const Tab& sel = tabList_[size_t(selected_)];
    for (size_t i = 0; i < tabList_.size(); ++i)
        if (int(i) != selected_) renderTab(g, tabList_[i], false);
    g.texture(tabBackground(sel.kind == Tab::Kind::Search, sel.kind == Tab::Kind::Inventory), leftPos_, topPos_, 0, 0,
              float(imageWidth_), float(imageHeight_));
    if (sel.kind == Tab::Kind::Search) {
        // The search box (an unbordered EditBox): white text and a blinking cursor.
        const std::string text = encodeUtf8(search_);
        const float x = leftPos_ + 82, y = topPos_ + 6;
        const float end = g.text(text, x, y, 0xFFFFFFFF);
        if ((ticks_ / 6) % 2 == 0) g.text("_", end, y, 0xFFFFFFFF);
    }
    if (sel.kind != Tab::Kind::Inventory) {
        const float x = leftPos_ + kScrollbarX, top = topPos_ + kScrollbarTop;
        g.sprite(creativeMenu.canScroll() ? "container/creative_inventory/scroller"
                                          : "container/creative_inventory/scroller_disabled",
                 x, top + float(int((kScrollbarHeight - 17) * creativeMenu.scroll())), 12, 15);
    }
    renderTab(g, sel, true);
    if (sel.kind == Tab::Kind::Inventory)
        g.playerFollowingMouse(player_, leftPos_ + 73, topPos_ + 6, leftPos_ + 105, topPos_ + 49, 20.0f, 0.0625f, mouse);
}

void CreativeScreen::renderLabels(GuiGraphics& g, glm::vec2) {
    const Tab& sel = tabList_[size_t(selected_)];
    if (sel.kind != Tab::Kind::Inventory) g.text(sel.name, leftPos_ + 8, topPos_ + 6, 0xFF404040, false);
}

void CreativeScreen::render(GuiGraphics& g, glm::vec2 mouse, float partialTick) {
    ContainerScreen::render(g, mouse, partialTick);
    // Tab names on hover.
    for (const Tab& t : tabList_)
        if (overTab(t, mouse)) {
            g.tooltip({t.name}, mouse.x, mouse.y);
            break;
        }
}

bool CreativeScreen::mouseClicked(glm::vec2 mouse, int button, int mods) {
    if (button == 0) {
        for (const Tab& t : tabList_)
            if (overTab(t, mouse)) return true; // tabs switch on release
        if (tabList_[size_t(selected_)].kind != Tab::Kind::Inventory && insideScrollbar(mouse)) {
            scrolling_ = creativeMenu.canScroll();
            return true;
        }
    }
    clickedOutside_ = clickedOutside(mouse);
    return ContainerScreen::mouseClicked(mouse, button, mods);
}

bool CreativeScreen::mouseReleased(glm::vec2 mouse, int button, int mods) {
    if (button == 0) {
        scrolling_ = false;
        for (size_t i = 0; i < tabList_.size(); ++i)
            if (overTab(tabList_[i], mouse)) {
                if (int(i) != selected_) selectTab(int(i));
                return true;
            }
    }
    return ContainerScreen::mouseReleased(mouse, button, mods);
}

bool CreativeScreen::mouseDragged(glm::vec2 mouse, int button, glm::vec2 delta) {
    if (scrolling_) {
        // Vanilla: the thumb's center follows the mouse.
        const float top = topPos_ + kScrollbarTop;
        creativeMenu.scrollTo((mouse.y - top - 7.5f) / (kScrollbarHeight - 15.0f));
        return true;
    }
    return ContainerScreen::mouseDragged(mouse, button, delta);
}

bool CreativeScreen::mouseScrolled(glm::vec2, double amount) {
    if (tabList_[size_t(selected_)].kind == Tab::Kind::Inventory || !creativeMenu.canScroll()) return false;
    creativeMenu.scrollTo(creativeMenu.scroll() - float(amount) / float(creativeMenu.rowsBeyondView()));
    return true;
}

bool CreativeScreen::keyPressed(int key, int scancode, int mods) {
    const Tab& sel = tabList_[size_t(selected_)];
    if (sel.kind == Tab::Kind::Search) {
        if (key == GLFW_KEY_BACKSPACE) {
            if (!search_.empty()) {
                search_.pop_back();
                refreshSearch();
            }
            return true;
        }
        if (key == GLFW_KEY_ESCAPE) return false;
        // Letters type into the box instead of acting as hotkeys (E, Q, numbers).
        if (key != GLFW_KEY_ENTER && key < GLFW_KEY_ESCAPE) return true;
    } else if (key == GLFW_KEY_T) {
        // The chat key jumps to search (vanilla).
        for (size_t i = 0; i < tabList_.size(); ++i)
            if (tabList_[i].kind == Tab::Kind::Search) selectTab(int(i));
        return true;
    }
    return ContainerScreen::keyPressed(key, scancode, mods);
}

bool CreativeScreen::charTyped(char32_t c) {
    if (tabList_[size_t(selected_)].kind != Tab::Kind::Search || c < 32 || c == 0x7F || c == U'§') return false;
    if (search_.size() >= 50) return true;
    search_.push_back(c);
    refreshSearch();
    return true;
}

void CreativeScreen::slotClicked(Slot* slot, int slotId, int button, ClickType type) {
    // CreativeModeInventoryScreen.slotClicked
    const bool quick = type == ClickType::QuickMove;
    if (slotId == Menu::kClickedOutside && type == ClickType::Pickup) type = ClickType::Throw;
    ItemStack& carried = creativeMenu.carried();
    const bool inventoryTab = tabList_[size_t(selected_)].kind == Tab::Kind::Inventory;
    if (!slot && !inventoryTab && type != ClickType::QuickCraft) {
        // Outside the window: throw the cursor stack (right click: one item).
        if (!carried.empty() && clickedOutside_) {
            if (button == 0) {
                player_.drop(carried);
                carried = {};
            } else if (button == 1) {
                player_.drop(carried.split(1));
            }
        }
        return;
    }
    if (slot && !slot->mayPickup()) return;
    const bool destroy = slot && slot->index == creativeMenu.destroySlot();
    if (destroy && quick) {
        // Shift-click on the destroy slot empties the whole inventory.
        for (Slot& s : inventoryMenu_.slots) *s.stack = {};
        return;
    }
    if (inventoryTab) {
        if (destroy) {
            carried = {};
        } else if (type == ClickType::Throw && slot && slot->hasItem()) {
            player_.drop(slot->stack->split(button == 0 ? 1 : slot->item().maxStack()));
        } else if (type == ClickType::Throw && slotId == Menu::kClickedOutside && !carried.empty()) {
            player_.drop(carried);
            carried = {};
        } else {
            inventoryMenu_.clicked(slot ? slot->index : slotId, button, type, player_);
        }
        return;
    }
    if (type != ClickType::QuickCraft && slot && slot->kind == Slot::Kind::Creative) {
        const ItemStack shown = slot->item();
        if (type == ClickType::Swap) {
            if (!shown.empty()) player_.inventory.slot(button) = shown.copyWithCount(shown.maxStack());
            return;
        }
        if (type == ClickType::Clone) {
            if (carried.empty() && !shown.empty()) carried = shown.copyWithCount(shown.maxStack());
            return;
        }
        if (type == ClickType::Throw) {
            if (!shown.empty()) player_.drop(shown.copyWithCount(button == 0 ? 1 : shown.maxStack()));
            return;
        }
        if (!carried.empty() && !shown.empty() && carried.sameItem(shown)) {
            // Clicking the same item adds one (shift: fills the stack); right click takes one away.
            if (button == 0) {
                if (quick) carried.count = carried.maxStack();
                else if (carried.count < carried.maxStack()) carried.grow(1);
            } else {
                carried.shrink(1);
            }
        } else if (!shown.empty() && carried.empty()) {
            carried = shown.copyWithCount(quick ? shown.maxStack() : shown.count);
        } else if (button == 0) {
            carried = {};
        } else if (!carried.empty()) {
            carried.shrink(1);
        }
        return;
    }
    creativeMenu.clicked(slot ? slot->index : slotId, button, type, player_);
}

} // namespace mc
