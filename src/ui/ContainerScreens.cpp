#include "ui/ContainerScreens.h"

#include <cmath>

#include "resources/FontSet.h"
#include "ui/GuiGraphics.h"

namespace mc {

CraftingScreen::CraftingScreen(ScreenContext ctx, CraftingMenu& menu, Player& player)
    : ContainerScreen(ctx, menu, player, ctx.translate("container.crafting")) {
    titleX_ = 29;
}

void CraftingScreen::renderBg(GuiGraphics& g, glm::vec2, float) {
    g.texture("minecraft:gui/container/crafting_table", leftPos_, topPos_, 0, 0, float(imageWidth_), float(imageHeight_));
}

ChestScreen::ChestScreen(ScreenContext ctx, ChestMenu& menu, Player& player, std::string title)
    : ContainerScreen(ctx, menu, player, std::move(title)), rows_(menu.rows()) {
    imageHeight_ = 114 + rows_ * 18;
    inventoryLabelY_ = float(imageHeight_ - 94);
}

void ChestScreen::renderBg(GuiGraphics& g, glm::vec2, float) {
    const float top = float(rows_ * 18 + 17);
    g.texture("minecraft:gui/container/generic_54", leftPos_, topPos_, 0, 0, float(imageWidth_), top);
    g.texture("minecraft:gui/container/generic_54", leftPos_, topPos_ + top, 0, 126, float(imageWidth_), 96);
}

FurnaceScreen::FurnaceScreen(ScreenContext ctx, FurnaceMenu& menu, Player& player)
    : ContainerScreen(ctx, menu, player, ""), furnace_(menu) {
    const RecipeType kind = menu.furnace().kind;
    kind_ = kind == RecipeType::Smoking ? "smoker" : kind == RecipeType::Blasting ? "blast_furnace" : "furnace";
    title_ = ctx.translate("container." + kind_);
}

void FurnaceScreen::init(glm::vec2 size) {
    ContainerScreen::init(size);
    // Vanilla centers the title over the window.
    const int width = ctx_.font ? int(std::ceil(ctx_.font->width(title_))) : 0;
    titleX_ = float((imageWidth_ - width) / 2);
}

void FurnaceScreen::renderBg(GuiGraphics& g, glm::vec2, float) {
    g.texture("minecraft:gui/container/" + kind_, leftPos_, topPos_, 0, 0, float(imageWidth_), float(imageHeight_));
    const FurnaceBlockEntity& f = furnace_.furnace();
    if (f.lit()) {
        // The flame: 14x14, its top cut away as the fuel burns down.
        const int h = int(std::ceil(f.litProgress() * 13.0f)) + 1;
        g.spriteRegion("container/" + kind_ + "/lit_progress", 14, 14, 0, 14 - h, leftPos_ + 56, topPos_ + 36 + 14 - float(h),
                       14, float(h));
    }
    // The arrow: 24x16, filled from the left.
    const int w = int(std::ceil(f.cookProgress() * 24.0f));
    if (w > 0) g.spriteRegion("container/" + kind_ + "/burn_progress", 24, 16, 0, 0, leftPos_ + 79, topPos_ + 34, float(w), 16);
}

} // namespace mc
