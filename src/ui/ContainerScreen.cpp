#include "ui/ContainerScreen.h"

#include <algorithm>
#include <cctype>
#include <chrono>

#include <GLFW/glfw3.h>

#include "entity/Player.h"
#include "game/Item.h"
#include "resources/Language.h"
#include "ui/GuiGraphics.h"

namespace mc {
namespace {

double nowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

} // namespace

std::string ScreenContext::translate(const std::string& key) const { return lang ? lang->get(key) : key; }

std::string ScreenContext::translate(const std::string& key, const std::vector<std::string>& args) const {
    const std::string pattern = translate(key);
    std::string out;
    size_t next = 0;
    for (size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] != '%' || i + 1 >= pattern.size()) {
            out += pattern[i];
            continue;
        }
        if (pattern[i + 1] == '%') {
            out += '%';
            ++i;
        } else if (pattern[i + 1] == 's') {
            if (next < args.size()) out += args[next++];
            ++i;
        } else if (std::isdigit(static_cast<unsigned char>(pattern[i + 1])) && i + 3 < pattern.size() &&
                   pattern[i + 2] == '$' && pattern[i + 3] == 's') {
            const size_t n = size_t(pattern[i + 1] - '1');
            if (n < args.size()) out += args[n];
            i += 3;
        } else {
            out += pattern[i];
        }
    }
    return out;
}

std::string ScreenContext::itemName(const ItemStack& stack) const {
    if (stack.empty() || !items || !lang) return "";
    const std::string name = items->name(*stack.item, *lang);
    if (stack.item->rarity == Rarity::Common) return name;
    return std::string("§") + rarityColorCode(stack.item->rarity) + name;
}

ContainerScreen::ContainerScreen(ScreenContext ctx, Menu& menu, Player& player, std::string title)
    : Screen(ctx), menu_(menu), player_(player), title_(std::move(title)) {}

void ContainerScreen::init(glm::vec2 size) {
    Screen::init(size);
    leftPos_ = float((int(size.x) - imageWidth_) / 2);
    topPos_ = float((int(size.y) - imageHeight_) / 2);
    inventoryLabelY_ = float(imageHeight_ - 94);
}

void ContainerScreen::removed() { menu_.removed(player_); }

Slot* ContainerScreen::hoveredSlot(glm::vec2 mouse) {
    const glm::vec2 m = mouse - glm::vec2(leftPos_, topPos_);
    for (Slot& s : menu_.slots)
        if (m.x >= float(s.x - 1) && m.x < float(s.x + 17) && m.y >= float(s.y - 1) && m.y < float(s.y + 17)) return &s;
    return nullptr;
}

bool ContainerScreen::clickedOutside(glm::vec2 mouse) const {
    return mouse.x < leftPos_ || mouse.y < topPos_ || mouse.x >= leftPos_ + float(imageWidth_) ||
           mouse.y >= topPos_ + float(imageHeight_);
}

const char* ContainerScreen::emptySlotIcon(const Slot& slot) const {
    if (slot.kind == Slot::Kind::Armor) {
        switch (slot.armor) {
        case EquipmentSlot::Head: return "container/slot/helmet";
        case EquipmentSlot::Chest: return "container/slot/chestplate";
        case EquipmentSlot::Legs: return "container/slot/leggings";
        case EquipmentSlot::Feet: return "container/slot/boots";
        default: return nullptr;
        }
    }
    if (slot.playerInventory && slot.containerSlot == Inventory::kOffhandSlot) return "container/slot/shield";
    return nullptr;
}

void ContainerScreen::slotClicked(Slot* slot, int slotId, int button, ClickType type) {
    menu_.clicked(slot ? slot->index : slotId, button, type, player_);
}

void ContainerScreen::recalculateQuickCraftRemaining() {
    const ItemStack& carried = menu_.carried();
    if (carried.empty() || !quickCrafting_) return;
    if (quickCraftType_ == 2) {
        quickCraftRemaining_ = carried.maxStack();
        return;
    }
    quickCraftRemaining_ = carried.count;
    for (Slot* s : quickCraftSlots_) {
        const int already = s->hasItem() ? s->item().count : 0;
        const int limit = std::min(carried.maxStack(), s->maxStackSize(carried));
        const int n = std::min(Menu::quickcraftPlaceCount(quickCraftSlots_.size(), quickCraftType_, carried) + already, limit);
        quickCraftRemaining_ -= n - already;
    }
}

void ContainerScreen::renderSlot(GuiGraphics& g, Slot& slot) {
    const float x = leftPos_ + float(slot.x), y = topPos_ + float(slot.y);
    ItemStack shown = slot.item();
    std::string countText;
    bool dragTarget = false;
    const ItemStack& carried = menu_.carried();
    if (quickCrafting_ && !carried.empty() &&
        std::find(quickCraftSlots_.begin(), quickCraftSlots_.end(), &slot) != quickCraftSlots_.end()) {
        if (quickCraftSlots_.size() == 1) return;
        // Preview of what the drag will leave here (capped counts in yellow).
        dragTarget = true;
        const int limit = std::min(carried.maxStack(), slot.maxStackSize(carried));
        int n = Menu::quickcraftPlaceCount(quickCraftSlots_.size(), quickCraftType_, carried) +
                (slot.hasItem() ? slot.item().count : 0);
        if (n > limit) {
            n = limit;
            countText = "§e" + std::to_string(limit);
        }
        shown = carried.copyWithCount(n);
    }
    if (shown.empty()) {
        if (const char* icon = emptySlotIcon(slot)) {
            g.sprite(icon, x, y, 16, 16);
            return;
        }
    }
    if (dragTarget) g.fill(x, y, x + 16, y + 16, 0x80FFFFFF);
    g.item(shown, x, y);
    g.itemDecorations(shown, x, y, countText.empty() ? nullptr : &countText);
}

void ContainerScreen::renderLabels(GuiGraphics& g, glm::vec2) {
    g.text(title_, leftPos_ + titleX_, topPos_ + titleY_, 0xFF404040, false);
    if (showInventoryLabel_)
        g.text(ctx_.translate("container.inventory"), leftPos_ + inventoryLabelX_, topPos_ + inventoryLabelY_, 0xFF404040,
               false);
}

void ContainerScreen::render(GuiGraphics& g, glm::vec2 mouse, float partialTick) {
    // In a world, screens dim it (vanilla renderTransparentBackground).
    g.fillGradient(0, 0, size_.x, size_.y, 0xC0101010, 0xD0101010);
    renderBg(g, mouse, partialTick);
    renderLabels(g, mouse);
    hovered_ = hoveredSlot(mouse);
    if (hovered_)
        g.sprite("container/slot_highlight_back", leftPos_ + float(hovered_->x) - 4, topPos_ + float(hovered_->y) - 4, 24, 24);
    for (Slot& s : menu_.slots) renderSlot(g, s);
    if (hovered_)
        g.sprite("container/slot_highlight_front", leftPos_ + float(hovered_->x) - 4, topPos_ + float(hovered_->y) - 4, 24, 24);

    // The item on the cursor; while dragging, what will be left of it.
    ItemStack carried = menu_.carried();
    if (!carried.empty()) {
        std::string countText;
        if (quickCrafting_ && quickCraftSlots_.size() > 1) {
            carried = carried.copyWithCount(quickCraftRemaining_);
            if (carried.empty()) countText = "§e0";
        }
        g.item(carried, mouse.x - 8, mouse.y - 8);
        g.itemDecorations(carried.empty() ? menu_.carried() : carried, mouse.x - 8, mouse.y - 8,
                          countText.empty() ? nullptr : &countText);
    } else if (hovered_ && hovered_->hasItem()) {
        g.tooltip({ctx_.itemName(hovered_->item())}, mouse.x, mouse.y);
    }
}

bool ContainerScreen::mouseClicked(glm::vec2 mouse, int button, int mods) {
    const bool pickBlock = button == GLFW_MOUSE_BUTTON_MIDDLE && player_.mode() == GameMode::Creative;
    Slot* slot = hoveredSlot(mouse);
    const double now = nowSeconds();
    doubleClick_ = lastClickSlot_ == slot && now - lastClickTime_ < 0.25 && lastClickButton_ == button;
    skipNextRelease_ = false;
    if (button == 0 || button == 1 || pickBlock) {
        int slotId = slot ? slot->index : -1;
        if (clickedOutside(mouse)) slotId = Menu::kClickedOutside;
        if (!quickCrafting_) {
            if (menu_.carried().empty()) {
                if (pickBlock) {
                    slotClicked(slot, slotId, button, ClickType::Clone);
                } else {
                    const bool shift = slotId != Menu::kClickedOutside && (mods & kShift);
                    ClickType type = ClickType::Pickup;
                    if (shift) {
                        lastQuickMoved_ = slot && slot->hasItem() ? slot->item() : ItemStack{};
                        type = ClickType::QuickMove;
                    } else if (slotId == Menu::kClickedOutside) {
                        type = ClickType::Throw;
                    }
                    slotClicked(slot, slotId, button, type);
                }
                skipNextRelease_ = true;
            } else {
                // With an item on the cursor, the press starts a possible drag.
                quickCrafting_ = true;
                quickCraftButton_ = button;
                quickCraftSlots_.clear();
                quickCraftType_ = button == 0 ? 0 : button == 1 ? 1 : 2;
            }
        }
    }
    lastClickSlot_ = slot;
    lastClickTime_ = now;
    lastClickButton_ = button;
    return true;
}

bool ContainerScreen::mouseDragged(glm::vec2 mouse, int, glm::vec2) {
    Slot* slot = hoveredSlot(mouse);
    const ItemStack& carried = menu_.carried();
    if (quickCrafting_ && slot && !carried.empty() &&
        (carried.count > int(quickCraftSlots_.size()) || quickCraftType_ == 2) &&
        Menu::canItemQuickReplace(slot, carried, true) && slot->mayPlace(carried) &&
        std::find(quickCraftSlots_.begin(), quickCraftSlots_.end(), slot) == quickCraftSlots_.end()) {
        quickCraftSlots_.push_back(slot);
        recalculateQuickCraftRemaining();
    }
    return true;
}

bool ContainerScreen::mouseReleased(glm::vec2 mouse, int button, int mods) {
    Slot* slot = hoveredSlot(mouse);
    int slotId = slot ? slot->index : -1;
    if (clickedOutside(mouse)) slotId = Menu::kClickedOutside;
    const bool canPickAll = slot && slot->kind != Slot::Kind::Result;
    if (doubleClick_ && slot && button == 0 && canPickAll) {
        if (mods & kShift) {
            // Shift double click: move every matching stack of that container.
            if (!lastQuickMoved_.empty())
                for (Slot& s : menu_.slots)
                    if (s.mayPickup() && s.hasItem() && s.playerInventory == slot->playerInventory &&
                        Menu::canItemQuickReplace(&s, lastQuickMoved_, true))
                        slotClicked(&s, s.index, button, ClickType::QuickMove);
        } else {
            slotClicked(slot, slotId, button, ClickType::PickupAll);
        }
        doubleClick_ = false;
        lastClickTime_ = -1.0;
    } else if (quickCrafting_ && quickCraftButton_ == button) {
        quickCrafting_ = false;
        if (!quickCraftSlots_.empty()) {
            slotClicked(nullptr, Menu::kClickedOutside, Menu::quickcraftMask(0, quickCraftType_), ClickType::QuickCraft);
            for (Slot* s : quickCraftSlots_)
                slotClicked(s, s->index, Menu::quickcraftMask(1, quickCraftType_), ClickType::QuickCraft);
            slotClicked(nullptr, Menu::kClickedOutside, Menu::quickcraftMask(2, quickCraftType_), ClickType::QuickCraft);
        } else if (!menu_.carried().empty()) {
            if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
                slotClicked(slot, slotId, button, ClickType::Clone);
            } else {
                const bool shift = slotId != Menu::kClickedOutside && (mods & kShift);
                if (shift) lastQuickMoved_ = slot && slot->hasItem() ? slot->item() : ItemStack{};
                slotClicked(slot, slotId, button, shift ? ClickType::QuickMove : ClickType::Pickup);
            }
        }
    }
    if (menu_.carried().empty()) lastClickTime_ = -1.0; // double clicks need the first click to pick something up
    quickCrafting_ = false;
    quickCraftSlots_.clear();
    return true;
}

bool ContainerScreen::keyPressed(int key, int, int mods) {
    if (key == GLFW_KEY_E) {
        close();
        return true;
    }
    if (menu_.carried().empty() && hovered_) {
        if (key == GLFW_KEY_F) {
            slotClicked(hovered_, hovered_->index, Inventory::kOffhandSlot, ClickType::Swap);
            return true;
        }
        if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9) {
            slotClicked(hovered_, hovered_->index, key - GLFW_KEY_1, ClickType::Swap);
            return true;
        }
    }
    if (hovered_ && hovered_->hasItem() && key == GLFW_KEY_Q) {
        slotClicked(hovered_, hovered_->index, (mods & kControl) ? 1 : 0, ClickType::Throw);
        return true;
    }
    return false;
}

} // namespace mc
