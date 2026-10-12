// Hud.h -- the in-game overlay (vanilla Gui): hotbar with items and the offhand
// slot, crosshair, hearts, armor, food, air, the experience bar and the
// fading name of the selected item.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "game/Inventory.h"
#include "game/Options.h"

namespace mc {

class GuiGraphics;
class Player;

class Hud {
public:
    // Draws an item stack (icon, count, durability) with its top-left at (x, y).
    using ItemDrawer = std::function<void(const ItemStack&, float x, float y)>;
    // The display name of a stack, with its rarity color code.
    using ItemNamer = std::function<std::string(const ItemStack&)>;

    // 20 Hz: the selected item's name timer and the heart shake.
    void tick(const Player& player, const ItemNamer& name);
    // Vanilla setOverlayMessage: a line above the hotbar for 3 seconds, fading in the last.
    void setOverlayMessage(std::string text) {
        overlayMessage_ = std::move(text);
        overlayTime_ = 60;
    }
    void render(GuiGraphics& g, const Player& player, bool firstPerson, bool eyeInWater, const ItemDrawer& drawItem);

    // Where the attack strength shows while it recovers (the option), and hardcore hearts.
    AttackIndicator attackIndicator = AttackIndicator::Crosshair;
    bool hardcore = false;

private:
    void renderHotbar(GuiGraphics& g, const Player& player, const ItemDrawer& drawItem);
    void renderStatus(GuiGraphics& g, const Player& player);
    void renderExperience(GuiGraphics& g, const Player& player);
    void renderSelectedName(GuiGraphics& g, const Player& player);
    void renderOverlayMessage(GuiGraphics& g);
    // Vanilla renderSleepOverlay: the screen goes dark while sleeping.
    void renderSleepOverlay(GuiGraphics& g, const Player& player);

    int ticks_ = 0;
    int highlightTimer_ = 0; // vanilla toolHighlightTimer
    std::string highlightName_;
    const Item* highlightItem_ = nullptr;
    uint32_t random_ = 12345;
    std::string overlayMessage_;
    int overlayTime_ = 0;
    bool eyeInWater_ = false;
    // Vanilla's heart blink: health lost (or regained) while invulnerable flashes the hearts
    // for 20 (10) ticks, the lost part shown until the health has held for a second.
    int lastHealth_ = 0, displayHealth_ = 0;
    int healthBlinkTime_ = 0, lastHealthTime_ = -1000; // the first tick takes the health as shown
};

} // namespace mc
