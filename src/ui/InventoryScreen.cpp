#include "ui/InventoryScreen.h"

#include "ui/GuiGraphics.h"

namespace mc {

InventoryScreen::InventoryScreen(ScreenContext ctx, InventoryMenu& menu, Player& player)
    : ContainerScreen(ctx, menu, player, ctx.translate("container.crafting")) {
    titleX_ = 97;
}

void InventoryScreen::renderBg(GuiGraphics& g, glm::vec2 mouse, float) {
    g.texture("minecraft:gui/container/inventory", leftPos_, topPos_, 0, 0, float(imageWidth_), float(imageHeight_));
    g.playerFollowingMouse(player_, leftPos_ + 26, topPos_ + 8, leftPos_ + 75, topPos_ + 78, 30.0f, 0.0625f, mouse);
}

void InventoryScreen::renderLabels(GuiGraphics& g, glm::vec2) {
    // Only "Crafting" above the grid; the inventory itself has no label here.
    g.text(title_, leftPos_ + titleX_, topPos_ + titleY_, 0xFF404040, false);
}

} // namespace mc
