// SignEditScreen.h -- writing on a sign (vanilla SignEditScreen and HangingSignEditScreen):
// the sign drawn large in the middle with its four lines, a blinking cursor, and Done.
// Lines can't grow wider than the sign holds; Enter and the arrow keys move between them.
#pragma once

#include <functional>
#include <memory>

#include "game/BlockEntities.h"
#include "ui/WidgetScreen.h"

namespace mc {

class SignEditScreen : public WidgetScreen {
public:
    // `wood` names the textures ("oak"); `standing` draws the stick below the board;
    // `onClose` runs when the screen goes (the session stops tracking the sign).
    SignEditScreen(ScreenContext ctx, std::shared_ptr<SignBlockEntity> sign, bool front, std::string wood, bool standing,
                   std::function<void()> onClose = {});
    void init(glm::vec2 size) override;
    void tick() override { ++frame_; }
    bool keyPressed(int key, int scancode, int mods) override;
    bool charTyped(char32_t c) override;
    bool isPauseScreen() const override { return false; }
    void removed() override;
    // Lines as typed, for tests.
    const std::shared_ptr<SignBlockEntity>& sign() const { return sign_; }
    int line() const { return line_; }

protected:
    // Vanilla renderTransparentBackground: the world stays sharp behind the sign.
    void renderBackground(GuiGraphics& g) override;
    void renderContents(GuiGraphics& g, glm::vec2 mouse, float partialTick) override;

private:
    SignBlockEntity::Side& side() { return front_ ? sign_->front : sign_->back; }
    std::u32string lineText(int line) const;
    void setLine(int line, const std::u32string& text);
    // Typed or pasted characters at the cursor, when the line still fits the sign.
    void insert(const std::u32string& chars);

    std::shared_ptr<SignBlockEntity> sign_;
    bool front_;
    std::string wood_;
    bool standing_;
    std::function<void()> onClose_;
    int line_ = 0;
    size_t cursor_ = 0;
    int frame_ = 0; // ticks, for the cursor's blink
};

} // namespace mc
