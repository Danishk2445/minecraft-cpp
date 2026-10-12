#include "ui/SignEditScreen.h"

#include <cmath>

#include <GLFW/glfw3.h>

#include "resources/FontSet.h"
#include "ui/GuiGraphics.h"

namespace mc {

SignEditScreen::SignEditScreen(ScreenContext ctx, std::shared_ptr<SignBlockEntity> sign, bool front, std::string wood,
                               bool standing, std::function<void()> onClose)
    : WidgetScreen(ctx, ctx.translate(sign->hanging ? "hanging_sign.edit" : "sign.edit")), sign_(std::move(sign)),
      front_(front), wood_(std::move(wood)), standing_(standing), onClose_(std::move(onClose)) {
    cursor_ = lineText(0).size();
}

void SignEditScreen::removed() {
    if (onClose_) onClose_();
}

void SignEditScreen::renderBackground(GuiGraphics& g) {
    g.fillGradient(0, 0, size_.x, size_.y, 0xC0101010, 0xD0101010);
}

std::u32string SignEditScreen::lineText(int line) const {
    const auto& s = front_ ? sign_->front : sign_->back;
    return decodeUtf8(s.lines[size_t(line)]);
}

void SignEditScreen::setLine(int line, const std::u32string& text) { side().lines[size_t(line)] = encodeUtf8(text); }

void SignEditScreen::init(glm::vec2 size) {
    WidgetScreen::init(size);
    clearWidgets();
    const ScreenContext ctx = ctx_;
    add<Button>(std::floor(size.x / 2) - 100, std::floor(size.y / 4) + 144, 200, 20, t("gui.done"),
                [ctx](Button&) { ctx.host->popScreen(); });
}

namespace {

// Vanilla StringSplitter.getWordPosition: past the spaces, then past the word.
size_t wordPosition(const std::u32string& text, size_t from, int direction) {
    size_t i = from;
    if (direction < 0) {
        while (i > 0 && text[i - 1] == U' ') --i;
        while (i > 0 && text[i - 1] != U' ') --i;
    } else {
        while (i < text.size() && text[i] != U' ') ++i;
        while (i < text.size() && text[i] == U' ') ++i;
    }
    return i;
}

} // namespace

bool SignEditScreen::keyPressed(int key, int scancode, int mods) {
    std::u32string text = lineText(line_);
    cursor_ = std::min(cursor_, text.size());
    const bool ctrl = (mods & kControl) != 0;
    // Vanilla TextFieldHelper: Ctrl moves and deletes by words.
    auto step = [&](int direction) {
        if (ctrl) return wordPosition(text, cursor_, direction);
        return direction < 0 ? (cursor_ > 0 ? cursor_ - 1 : 0) : std::min(cursor_ + 1, text.size());
    };
    switch (key) {
    case GLFW_KEY_UP: line_ = (line_ + 3) % 4; cursor_ = lineText(line_).size(); return true;
    case GLFW_KEY_DOWN:
    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER: line_ = (line_ + 1) % 4; cursor_ = lineText(line_).size(); return true;
    case GLFW_KEY_LEFT: cursor_ = step(-1); return true;
    case GLFW_KEY_RIGHT: cursor_ = step(1); return true;
    case GLFW_KEY_HOME: cursor_ = 0; return true;
    case GLFW_KEY_END: cursor_ = text.size(); return true;
    case GLFW_KEY_BACKSPACE: {
        const size_t from = step(-1);
        text.erase(from, cursor_ - from);
        cursor_ = from;
        setLine(line_, text);
        return true;
    }
    case GLFW_KEY_DELETE: {
        const size_t to = step(1);
        text.erase(cursor_, to - cursor_);
        setLine(line_, text);
        return true;
    }
    case GLFW_KEY_V:
        // Paste, formatting codes and line breaks taken out; all of it, if it fits.
        if (ctrl)
            if (GLFWwindow* w = glfwGetCurrentContext())
                if (const char* clip = glfwGetClipboardString(w)) {
                    std::u32string pasted;
                    const std::u32string raw = decodeUtf8(clip);
                    for (size_t i = 0; i < raw.size(); ++i) {
                        if (raw[i] == U'§') ++i; // and its code
                        else if (raw[i] >= 32 && raw[i] != 0x7F) pasted += raw[i];
                    }
                    insert(pasted);
                    return true;
                }
        break;
    default: break;
    }
    return WidgetScreen::keyPressed(key, scancode, mods);
}

void SignEditScreen::insert(const std::u32string& chars) {
    std::u32string text = lineText(line_);
    cursor_ = std::min(cursor_, text.size());
    text.insert(cursor_, chars);
    // Vanilla TextFieldHelper with the sign's width limit: nothing goes in that doesn't fit.
    if (ctx_.font && ctx_.font->width(encodeUtf8(text)) > sign_->maxLineWidth()) return;
    setLine(line_, text);
    cursor_ += chars.size();
}

bool SignEditScreen::charTyped(char32_t c) {
    if (c < 32 || c == 0x7F || c == U'§') return false;
    insert(std::u32string(1, c));
    return true;
}

void SignEditScreen::renderContents(GuiGraphics& g, glm::vec2, float) {
    const float cx = std::floor(size_.x / 2);
    g.centeredText(title_, cx, 40, 0xFFFFFFFF);
    // The sign, large: the board's front (and the stick for standing signs) from the entity
    // texture; hanging signs use their own picture (gui/hanging_signs), 16 x 16 at 4.5 times.
    float textY = 90.0f, scale = 0.9765628f;
    if (sign_->hanging) {
        // 13 up from the text's center, a 16-pixel picture at 4.5 times.
        textY = 125.0f;
        scale = 1.0f;
        g.texture("minecraft:gui/hanging_signs/" + wood_, cx - 36, textY - 13 - 36, 0, 0, 72, 72, 72, 72);
    } else {
        const std::string tex = "minecraft:entity/signs/" + wood_;
        // The board's front face is texels (2, 2)-(26, 14) of the 64 x 32 texture, at 4 pixels a
        // texel; the stick's (2, 16)-(4, 30).
        g.texture(tex, cx - 48, 66, 2 * 4, 2 * 4, 96, 48, 64 * 4, 32 * 4);
        if (standing_) g.texture(tex, cx - 4, 114, 2 * 4, 16 * 4, 8, 54, 64 * 4, 32 * 4);
    }
    // The lines, centered, in the side's (dark) color; the cursor blinks on the current one.
    const auto& s = front_ ? sign_->front : sign_->back;
    uint32_t color = dyeTextColor(s.color);
    if (!s.glowing) {
        const uint32_t r = uint32_t(float((color >> 16) & 0xFF) * 0.4f), gr = uint32_t(float((color >> 8) & 0xFF) * 0.4f),
                       b = uint32_t(float(color & 0xFF) * 0.4f);
        color = (r << 16) | (gr << 8) | b;
    }
    color |= 0xFF000000u;
    const int lineHeight = sign_->lineHeight(), top = 4 * lineHeight / 2;
    g.batch().setTransform(glm::mat3(glm::vec3(scale, 0, 0), glm::vec3(0, scale, 0), glm::vec3(cx, textY, 1)));
    const bool blink = frame_ / 6 % 2 == 0;
    for (int i = 0; i < 4; ++i) {
        const std::string& line = s.lines[size_t(i)];
        const float w = float(g.textWidth(line));
        const float x = -std::floor(w / 2), y = float(i * lineHeight - top);
        g.text(line, x, y, color, false);
        if (i == line_ && blink) {
            const std::u32string text = decodeUtf8(line);
            const float before = float(g.textWidth(encodeUtf8(text.substr(0, std::min(cursor_, text.size())))));
            if (cursor_ >= text.size()) g.text("_", x + before, y, color, false);
            else g.fill(x + before, y - 1, x + before + 1, y + float(lineHeight), color);
        }
    }
    g.batch().resetTransform();
}

} // namespace mc
