// ContainerScreens.h -- the windows of block menus: the crafting table (vanilla
// CraftingScreen), ...
#pragma once

#include "game/ContainerMenus.h"
#include "ui/ContainerScreen.h"

namespace mc {

class CraftingScreen : public ContainerScreen {
public:
    CraftingScreen(ScreenContext ctx, CraftingMenu& menu, Player& player);

protected:
    void renderBg(GuiGraphics& g, glm::vec2 mouse, float partialTick) override;
};

// Chests, barrels and ender chests (vanilla ContainerScreen over generic_54.png): the
// top `rows` rows of slots, then the inventory part of the texture.
class ChestScreen : public ContainerScreen {
public:
    ChestScreen(ScreenContext ctx, ChestMenu& menu, Player& player, std::string title);

protected:
    void renderBg(GuiGraphics& g, glm::vec2 mouse, float partialTick) override;

private:
    int rows_;
};

// Furnaces, smokers and blast furnaces (vanilla AbstractFurnaceScreen): the flame shrinks
// with the fuel's burn time, the arrow fills with the cooking.
class FurnaceScreen : public ContainerScreen {
public:
    FurnaceScreen(ScreenContext ctx, FurnaceMenu& menu, Player& player);
    void init(glm::vec2 size) override;

protected:
    void renderBg(GuiGraphics& g, glm::vec2 mouse, float partialTick) override;

private:
    const FurnaceMenu& furnace_;
    std::string kind_; // "furnace", "smoker" or "blast_furnace": texture and sprites
};

} // namespace mc
