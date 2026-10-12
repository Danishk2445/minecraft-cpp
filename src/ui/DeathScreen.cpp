#include "ui/DeathScreen.h"

#include <cmath>

#include "ui/GuiGraphics.h"
#include "ui/TitleScreens.h"

namespace mc {

DeathScreen::DeathScreen(ScreenContext ctx, std::string cause, int score, bool hardcore)
    : WidgetScreen(ctx, ctx.translate(hardcore ? "deathScreen.title.hardcore" : "deathScreen.title")),
      cause_(std::move(cause)), score_(score), hardcore_(hardcore) {}

void DeathScreen::init(glm::vec2 size) {
    WidgetScreen::init(size);
    clearWidgets();
    buttons_.clear();
    const float cx = std::floor(size.x / 2), y = std::floor(size.y / 4);
    const ScreenContext ctx = ctx_;
    buttons_.push_back(&add<Button>(cx - 100, y + 72, 200, 20, t(hardcore_ ? "deathScreen.spectate" : "deathScreen.respawn"),
                                    [ctx](Button& b) {
                                        b.active = false;
                                        ctx.host->respawn();
                                    }));
    const bool hardcore = hardcore_;
    buttons_.push_back(&add<Button>(cx - 100, y + 96, 200, 20, t("deathScreen.titleScreen"), [ctx, hardcore](Button&) {
        if (hardcore) {
            ctx.host->leaveWorld();
            return;
        }
        // Vanilla TitleConfirmScreen: quit, or respawn after all.
        ctx.host->pushScreen(std::make_unique<ConfirmScreen>(
            ctx, ctx.translate("deathScreen.quit.confirm"), "", ctx.translate("deathScreen.titleScreen"),
            ctx.translate("deathScreen.respawn"), [ctx](bool quit) {
                if (quit) ctx.host->leaveWorld();
                else ctx.host->respawn();
            }));
    }));
    for (Button* b : buttons_) b->active = delay_ >= 20;
}

void DeathScreen::tick() {
    if (++delay_ == 20)
        for (Button* b : buttons_) b->active = true;
}

void DeathScreen::renderBackground(GuiGraphics& g) {
    g.fillGradient(0, 0, size_.x, size_.y, 0x60500000, 0xA0803030);
}

void DeathScreen::renderContents(GuiGraphics& g, glm::vec2, float) {
    const float cx = std::floor(size_.x / 2);
    // The title at twice the size, centered at y 30 of the halved screen.
    g.batch().setTransform(glm::mat3(glm::vec3(2, 0, 0), glm::vec3(0, 2, 0), glm::vec3(0, 0, 1)));
    g.centeredText(title_, std::floor(cx / 2), 30, 0xFFFFFFFF);
    g.batch().resetTransform();
    if (!cause_.empty()) g.centeredText(cause_, cx, 85, 0xFFFFFFFF);
    g.centeredText(ctx_.translate("deathScreen.score.value", {"§e" + std::to_string(score_)}), cx, 100, 0xFFFFFFFF);
}

} // namespace mc
