// DeathScreen.h -- vanilla DeathScreen: "You Died!" (hardcore: "Game Over!") in double
// size over a red gradient, the cause and the score, and Respawn (Spectate World) and
// Title Screen, which wake up after a second.
#pragma once

#include "ui/WidgetScreen.h"

namespace mc {

class DeathScreen : public WidgetScreen {
public:
    DeathScreen(ScreenContext ctx, std::string cause, int score, bool hardcore);
    void init(glm::vec2 size) override;
    void tick() override;
    bool isPauseScreen() const override { return false; }
    bool closeOnEscape() const override { return false; }

protected:
    void renderBackground(GuiGraphics& g) override;
    void renderContents(GuiGraphics& g, glm::vec2 mouse, float partialTick) override;

private:
    std::string cause_;
    int score_ = 0;
    bool hardcore_ = false;
    int delay_ = 0; // ticks since it opened
    std::vector<Button*> buttons_;
};

} // namespace mc
