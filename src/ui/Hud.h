// Hud.h -- the in-game overlay (vanilla Gui): hotbar with items and the offhand
// slot, crosshair, hearts, armor, food, air, the experience bar and the
// fading name of the selected item.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "game/Inventory.h"

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
    void render(GuiGraphics& g, const Player& player, bool firstPerson, bool eyeInWater, const ItemDrawer& drawItem);

private:
    void renderHotbar(GuiGraphics& g, const Player& player, const ItemDrawer& drawItem);
    void renderStatus(GuiGraphics& g, const Player& player);
    void renderExperience(GuiGraphics& g, const Player& player);
    void renderSelectedName(GuiGraphics& g, const Player& player);

    int ticks_ = 0;
    int highlightTimer_ = 0; // vanilla toolHighlightTimer
    std::string highlightName_;
    const Item* highlightItem_ = nullptr;
    uint32_t random_ = 12345;
    bool eyeInWater_ = false;
};

} // namespace mc
