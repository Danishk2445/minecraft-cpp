#include "ui/WidgetScreen.h"

#include <algorithm>

#include <GLFW/glfw3.h>

#include "game/Options.h"
#include "ui/GuiGraphics.h"

namespace mc {

void WidgetScreen::setFocus(Widget* w) {
    if (focused_) focused_->focused = false;
    focused_ = w;
    if (focused_) focused_->focused = true;
}

void WidgetScreen::renderBackground(GuiGraphics& g) {
    // Vanilla Screen.renderBackground: what is behind (the world, or the panorama the
    // game draws without one) blurred, then the translucent menu texture over it.
    g.blurBehind(ctx_.host->options().menuBlur);
    renderMenuBackground(g, 0, 0, size_.x, size_.y);
}

void WidgetScreen::renderMenuBackground(GuiGraphics& g, float x, float y, float w, float h) const {
    const char* texture = inWorld() ? "minecraft:gui/inworld_menu_background" : "minecraft:gui/menu_background";
    g.texture(texture, x, y, 0, 0, w, h, 32, 32);
}

void WidgetScreen::renderSeparators(GuiGraphics& g, float headerHeight, float footerHeight) {
    const char* header = inWorld() ? "minecraft:gui/inworld_header_separator" : "minecraft:gui/header_separator";
    const char* footer = inWorld() ? "minecraft:gui/inworld_footer_separator" : "minecraft:gui/footer_separator";
    g.texture(header, 0, headerHeight - 2, 0, 0, size_.x, 2, 32, 2);
    g.texture(footer, 0, size_.y - footerHeight, 0, 0, size_.x, 2, 32, 2);
}

namespace {

bool inside(const glm::vec4& r, glm::vec2 p) { return p.x >= r.x && p.y >= r.y && p.x < r.z && p.y < r.w; }

} // namespace

void WidgetScreen::render(GuiGraphics& g, glm::vec2 mouse, float partialTick) {
    renderBackground(g);
    for (auto& w : widgets_) {
        if (const std::optional<glm::vec4> clip = clipOf(*w)) {
            g.enableScissor(clip->x, clip->y, clip->z, clip->w);
            w->render(g, inside(*clip, mouse) ? mouse : glm::vec2(-1.0f), partialTick);
            g.disableScissor();
        } else {
            w->render(g, mouse, partialTick);
        }
    }
    renderContents(g, mouse, partialTick);
    for (auto& w : widgets_) {
        const std::optional<glm::vec4> clip = clipOf(*w);
        if (!w->tooltip.empty() && w->isMouseOver(mouse) && (!clip || inside(*clip, mouse)))
            g.textTooltip(w->tooltip, mouse.x, mouse.y);
    }
}

bool WidgetScreen::mouseClicked(glm::vec2 mouse, int button, int) {
    for (auto& w : widgets_) {
        if (const std::optional<glm::vec4> clip = clipOf(*w); clip && !inside(*clip, mouse)) continue;
        if (w->visible && w->mouseClicked(mouse, button)) {
            setFocus(w->focusable() ? w.get() : nullptr);
            return true;
        }
    }
    setFocus(nullptr);
    return false;
}

bool WidgetScreen::mouseReleased(glm::vec2 mouse, int button, int) {
    bool handled = false;
    for (auto& w : widgets_) handled |= w->mouseReleased(mouse, button);
    return handled;
}

bool WidgetScreen::mouseDragged(glm::vec2 mouse, int button, glm::vec2) {
    for (auto& w : widgets_)
        if (w->mouseDragged(mouse, button)) return true;
    return false;
}

bool WidgetScreen::keyPressed(int key, int, int mods) {
    if (focused_ && focused_->keyPressed(key, mods)) return true;
    if (key == GLFW_KEY_TAB) {
        // Tab (Shift+Tab backwards) moves the focus through the focusable widgets.
        std::vector<Widget*> order;
        for (auto& w : widgets_)
            if (w->focusable()) order.push_back(w.get());
        if (order.empty()) return true;
        auto it = std::find(order.begin(), order.end(), focused_);
        const int n = int(order.size());
        int i = it == order.end() ? -1 : int(it - order.begin());
        i = (mods & GLFW_MOD_SHIFT) ? (i <= 0 ? n - 1 : i - 1) : (i + 1) % n;
        setFocus(order[size_t(i)]);
        return true;
    }
    return false;
}

bool WidgetScreen::charTyped(char32_t c) { return focused_ && focused_->charTyped(c); }

} // namespace mc
