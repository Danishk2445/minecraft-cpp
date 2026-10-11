// InventoryScreen.h -- the survival inventory (vanilla InventoryScreen): armor,
// the 2x2 crafting grid, the off hand and the player turned toward the mouse.
#pragma once

#include "ui/ContainerScreen.h"

namespace mc {

class InventoryScreen : public ContainerScreen {
public:
    InventoryScreen(ScreenContext ctx, InventoryMenu& menu, Player& player);

protected:
    void renderBg(GuiGraphics& g, glm::vec2 mouse, float partialTick) override;
    void renderLabels(GuiGraphics& g, glm::vec2 mouse) override;
};

} // namespace mc
