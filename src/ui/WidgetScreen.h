// WidgetScreen.h -- a menu screen made of widgets (vanilla Screen with its
// renderables): mouse and key dispatch, Tab focus, the menu background and an
// optional header and footer band.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ui/Screen.h"
#include "ui/Widgets.h"

namespace mc {

class WidgetScreen : public Screen {
public:
    WidgetScreen(ScreenContext ctx, std::string title) : Screen(ctx), title_(std::move(title)) {}

    void render(GuiGraphics& g, glm::vec2 mouse, float partialTick) override;
    bool mouseClicked(glm::vec2 mouse, int button, int mods) override;
    bool mouseReleased(glm::vec2 mouse, int button, int mods) override;
    bool mouseDragged(glm::vec2 mouse, int button, glm::vec2 delta) override;
    bool keyPressed(int key, int scancode, int mods) override;
    bool charTyped(char32_t c) override;
    const std::vector<std::unique_ptr<Widget>>& widgets() const { return widgets_; }
    // The part of the screen a widget shows in (x0, y0, x1, y1), for widgets in a scrolling
    // list: drawn clipped to it and clicked only inside it. None for the rest.
    virtual std::optional<glm::vec4> clipOf(const Widget&) const { return std::nullopt; }

protected:
    template <class W, class... Args>
    W& add(Args&&... args) {
        widgets_.push_back(std::make_unique<W>(std::forward<Args>(args)...));
        return static_cast<W&>(*widgets_.back());
    }
    void clearWidgets() {
        widgets_.clear();
        focused_ = nullptr;
    }
    void setFocus(Widget* w);
    // The menu background: the world (or the title panorama) blurred and darkened.
    virtual void renderBackground(GuiGraphics& g);
    // The menu texture over part of the screen (vanilla renderMenuBackground).
    void renderMenuBackground(GuiGraphics& g, float x, float y, float w, float h) const;
    // Draws after the widgets (labels, lists, previews).
    virtual void renderContents(GuiGraphics&, glm::vec2, float) {}
    // Header and footer separators of vanilla's HeaderAndFooterLayout screens.
    void renderSeparators(GuiGraphics& g, float headerHeight, float footerHeight);
    bool inWorld() const { return ctx_.host && ctx_.host->inWorld(); }
    std::string t(const std::string& key) const { return ctx_.translate(key); }

    std::string title_;
    std::vector<std::unique_ptr<Widget>> widgets_;
    Widget* focused_ = nullptr;
};

} // namespace mc
