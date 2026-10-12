#include "ui/Widgets.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <GLFW/glfw3.h>

#include "resources/FontSet.h"
#include "ui/GuiGraphics.h"

namespace mc {
namespace {

long long nowMillis() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

} // namespace

void Widget::renderCenteredText(GuiGraphics& g, uint32_t color) const {
    const float left = x + 2, right = x + width - 2;
    const float textY = std::floor((y + y + height - 9) / 2) + 1;
    const float w = float(g.textWidth(message));
    if (w <= right - left) {
        g.centeredText(message, std::floor((left + right) / 2), textY, color);
        return;
    }
    // Too long: clipped, like vanilla's scrolling string at rest.
    g.enableScissor(left, y, right, y + height);
    g.text(message, left, textY, color);
    g.disableScissor();
}

void Button::render(GuiGraphics& g, glm::vec2 mouse, float) {
    if (!visible) return;
    const bool hot = active && (isMouseOver(mouse) || focused);
    g.sprite(!active ? "widget/button_disabled" : hot ? "widget/button_highlighted" : "widget/button", x, y, width, height);
    if (!message.empty()) renderCenteredText(g, active ? 0xFFFFFFFF : 0xFFA0A0A0);
    if (!icon.empty())
        g.sprite(icon, x + std::floor(width / 2) - std::floor(iconWidth / 2), y + std::floor(height / 2) - std::floor(iconHeight / 2),
                 iconWidth, iconHeight);
}

void TabButton::render(GuiGraphics& g, glm::vec2 mouse, float) {
    if (!visible) return;
    const bool hot = isMouseOver(mouse) || focused;
    g.sprite(selected ? (hot ? "widget/tab_selected_highlighted" : "widget/tab_selected")
                      : (hot ? "widget/tab_highlighted" : "widget/tab"),
             x, y, width, height);
    const uint32_t color = active ? 0xFFFFFFFF : 0xFFA0A0A0;
    if (selected) {
        // The page's background shows through the open tab, underlined under the label.
        g.texture("minecraft:gui/menu_background", x + 2, y + 2, 0, 0, width - 4, height - 2, 32, 32);
        const float w = std::min(float(g.textWidth(message)), width - 4), x0 = x + std::floor((width - w) / 2);
        g.fill(x0, y + height - 2, x0 + w, y + height - 1, color);
    }
    // Vanilla renderScrollingString between x+1 and the right edge - 1, 3 lower when not selected.
    const float top = y + (selected ? 0 : 3), bottom = y + height;
    g.centeredText(message, std::floor((x + 1 + x + width - 1) / 2), std::floor((top + bottom - 9) / 2) + 1, color);
}

bool Button::mouseClicked(glm::vec2 m, int button) {
    if (button != 0 || !active || !isMouseOver(m)) return false;
    press();
    return true;
}

bool Button::keyPressed(int key, int) {
    if (!active || !focused || (key != GLFW_KEY_ENTER && key != GLFW_KEY_SPACE && key != GLFW_KEY_KP_ENTER)) return false;
    press();
    return true;
}

CycleButton::CycleButton(float x, float y, float width, float height, std::string label,
                         std::vector<std::string> values, int index, std::function<void(int)> onChange)
    : Button(x, y, width, height, "", {}), label_(std::move(label)), values_(std::move(values)),
      index_(std::clamp(index, 0, std::max(0, int(values_.size()) - 1))), onChange_(std::move(onChange)) {
    onPress = [this](Button&) { setIndex((index_ + 1) % std::max(1, int(values_.size()))); };
    refresh();
}

void CycleButton::setIndex(int i, bool notify) {
    index_ = i;
    refresh();
    if (notify && onChange_) onChange_(index_);
}

void CycleButton::refresh() {
    const std::string value = index_ < int(values_.size()) ? values_[size_t(index_)] : "";
    message = label_.empty() ? value : label_ + ": " + value;
}

Slider::Slider(float x, float y, float width, float height, double value, std::function<std::string(double)> label,
               std::function<void(double)> onChange)
    : Widget(x, y, width, height, ""), value_(std::clamp(value, 0.0, 1.0)), label_(std::move(label)),
      onChange_(std::move(onChange)) {
    if (label_) message = label_(value_);
}

void Slider::setValue(double v) {
    const double clamped = std::clamp(v, 0.0, 1.0);
    if (clamped == value_) return;
    value_ = clamped;
    if (onChange_) onChange_(value_);
    if (label_) message = label_(value_);
}

void Slider::render(GuiGraphics& g, glm::vec2 mouse, float) {
    if (!visible) return;
    hovered_ = isMouseOver(mouse);
    g.sprite(focused ? "widget/slider_highlighted" : "widget/slider", x, y, width, height);
    g.sprite(hovered_ || dragging_ ? "widget/slider_handle_highlighted" : "widget/slider_handle",
             x + float(int(value_ * double(width - 8))), y, 8, height);
    renderCenteredText(g, active ? 0xFFFFFFFF : 0xFFA0A0A0);
}

bool Slider::mouseClicked(glm::vec2 m, int button) {
    if (button != 0 || !active || !isMouseOver(m)) return false;
    dragging_ = true;
    setValue((m.x - (x + 4)) / (width - 8));
    return true;
}

bool Slider::mouseDragged(glm::vec2 m, int) {
    if (!dragging_) return false;
    setValue((m.x - (x + 4)) / (width - 8));
    return true;
}

bool Slider::mouseReleased(glm::vec2, int) {
    const bool was = dragging_;
    dragging_ = false;
    return was;
}

bool Slider::keyPressed(int key, int) {
    if (!focused || !active) return false;
    const double step = 1.0 / double(width - 8);
    if (key == GLFW_KEY_LEFT) setValue(value_ - step);
    else if (key == GLFW_KEY_RIGHT) setValue(value_ + step);
    else return false;
    return true;
}

EditBox::EditBox(float x, float y, float width, float height, std::string hint)
    : Widget(x, y, width, height, ""), hint(std::move(hint)) {}

std::string EditBox::value() const { return encodeUtf8(text_); }

void EditBox::setValue(const std::string& text) {
    text_ = decodeUtf8(text);
    if (int(text_.size()) > maxLength) text_.resize(size_t(maxLength));
    moveCursorToEnd();
    changed();
}

void EditBox::moveCursorToEnd() { setCursor(text_.size(), false); }

void EditBox::changed() {
    if (onChange) onChange(value());
}

void EditBox::setCursor(size_t pos, bool select) {
    cursor_ = std::min(pos, text_.size());
    if (!select) highlight_ = cursor_;
    // Keep the cursor inside the visible part (vanilla scrollTo).
    if (display_ > cursor_) display_ = cursor_;
    if (graphics_) {
        const float inner = bordered ? width - 8 : width;
        while (display_ < cursor_ &&
               float(graphics_->textWidth(encodeUtf8(text_.substr(display_, cursor_ - display_)))) > inner)
            ++display_;
    }
}

void EditBox::insert(std::u32string_view text) {
    const size_t a = std::min(cursor_, highlight_), b = std::max(cursor_, highlight_);
    std::u32string filtered;
    for (char32_t c : text)
        if (c >= 32 && c != 0x7F && c != U'§' && (!filter || filter(c))) filtered += c;
    const size_t room = size_t(std::max(0, maxLength - int(text_.size() - (b - a))));
    if (filtered.size() > room) filtered.resize(room);
    text_.replace(a, b - a, filtered);
    setCursor(a + filtered.size(), false);
    changed();
}

void EditBox::deleteChars(int count) {
    if (cursor_ != highlight_) {
        insert(U"");
        return;
    }
    const size_t from = count < 0 ? cursor_ - std::min<size_t>(cursor_, size_t(-count)) : cursor_;
    const size_t to = count < 0 ? cursor_ : std::min(text_.size(), cursor_ + size_t(count));
    if (from == to) return;
    text_.erase(from, to - from);
    setCursor(from, false);
    changed();
}

bool EditBox::mouseClicked(glm::vec2 m, int button) {
    if (!visible || !isMouseOver(m)) return false;
    if (button == 0 && graphics_) {
        // The cursor goes to the character nearest the click.
        const float start = bordered ? x + 4 : x;
        size_t pos = display_;
        while (pos < text_.size() &&
               start + float(graphics_->textWidth(encodeUtf8(text_.substr(display_, pos + 1 - display_)))) <= m.x)
            ++pos;
        setCursor(pos, false);
    }
    return true;
}

bool EditBox::keyPressed(int key, int mods) {
    if (!focused) return false;
    const bool shift = mods & GLFW_MOD_SHIFT, ctrl = mods & GLFW_MOD_CONTROL;
    if (ctrl && key == GLFW_KEY_A) {
        cursor_ = text_.size();
        highlight_ = 0;
        return true;
    }
    if (ctrl && (key == GLFW_KEY_C || key == GLFW_KEY_X)) {
        const size_t a = std::min(cursor_, highlight_), b = std::max(cursor_, highlight_);
        if (GLFWwindow* w = glfwGetCurrentContext()) glfwSetClipboardString(w, encodeUtf8(text_.substr(a, b - a)).c_str());
        if (key == GLFW_KEY_X && a != b) insert(U"");
        return true;
    }
    if (ctrl && key == GLFW_KEY_V) {
        if (GLFWwindow* w = glfwGetCurrentContext())
            if (const char* clip = glfwGetClipboardString(w)) insert(decodeUtf8(clip));
        return true;
    }
    switch (key) {
    case GLFW_KEY_BACKSPACE: deleteChars(-1); return true;
    case GLFW_KEY_DELETE: deleteChars(1); return true;
    case GLFW_KEY_LEFT: setCursor(cursor_ > 0 ? cursor_ - 1 : 0, shift); return true;
    case GLFW_KEY_RIGHT: setCursor(cursor_ + 1, shift); return true;
    case GLFW_KEY_HOME: setCursor(0, shift); return true;
    case GLFW_KEY_END: setCursor(text_.size(), shift); return true;
    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER:
        if (onSubmit) {
            onSubmit();
            return true;
        }
        return false;
    default: return false;
    }
}

bool EditBox::charTyped(char32_t c) {
    if (!focused || !active) return false;
    insert(std::u32string(1, c));
    return true;
}

void EditBox::render(GuiGraphics& g, glm::vec2, float) {
    if (!visible) return;
    graphics_ = &g;
    if (bordered) g.sprite(focused ? "widget/text_field_highlighted" : "widget/text_field", x, y, width, height);
    const float inner = bordered ? width - 8 : width;
    const float tx = bordered ? x + 4 : x, ty = bordered ? y + std::floor((height - 8) / 2) : y;
    const uint32_t color = active ? textColor : 0xFF707070;
    // The visible slice of the text.
    std::u32string shown;
    for (size_t i = display_; i < text_.size(); ++i) {
        shown += text_[i];
        if (float(g.textWidth(encodeUtf8(shown))) > inner) {
            shown.pop_back();
            break;
        }
    }
    const size_t cursorInShown = cursor_ - std::min(cursor_, display_);
    float cursorX = tx;
    if (!shown.empty()) {
        const std::u32string before = shown.substr(0, std::min(cursorInShown, shown.size()));
        cursorX = g.text(encodeUtf8(before), tx, ty, color);
        if (cursorInShown < shown.size()) g.text(encodeUtf8(shown.substr(cursorInShown)), cursorX, ty, color);
    } else if (!focused && !hint.empty()) {
        g.text(hint, tx, ty, 0xFF555555); // vanilla's default hint style: dark gray
    }
    // Selection: a translucent box over the selected characters.
    if (highlight_ != cursor_) {
        const size_t a = std::clamp(std::min(cursor_, highlight_), display_, display_ + shown.size()) - display_;
        const size_t b = std::clamp(std::max(cursor_, highlight_), display_, display_ + shown.size()) - display_;
        const float x0 = tx + float(g.textWidth(encodeUtf8(shown.substr(0, a))));
        const float x1 = tx + float(g.textWidth(encodeUtf8(shown.substr(0, b))));
        g.fill(x0, ty - 1, x1, ty + 10, 0x800000FF);
    }
    // Vanilla blinks the cursor every 300 ms: '_' at the end, a bar inside the text.
    if (focused && (nowMillis() / 300) % 2 == 0) {
        if (cursor_ < text_.size()) g.fill(cursorX - 1, ty - 1, cursorX, ty + 10, 0xFFD0D0D0);
        else g.text("_", cursorX, ty, color);
    }
}

Checkbox::Checkbox(float x, float y, std::string label, bool selected, std::function<void(bool)> onChange)
    : Widget(x, y, 17, 17, std::move(label)), selected(selected), onChange_(std::move(onChange)) {}

void Checkbox::render(GuiGraphics& g, glm::vec2 mouse, float) {
    if (!visible) return;
    const bool hot = isMouseOver(mouse) || focused;
    std::string sprite = selected ? "widget/checkbox_selected" : "widget/checkbox";
    if (hot) sprite += "_highlighted";
    g.sprite(sprite, x, y, 17, 17);
    g.text(message, x + 17 + 4, y + std::floor((17 - 8) / 2.0f), 0xFFE0E0E0);
    width = 17 + 4 + float(g.textWidth(message)); // the label is clickable too
}

bool Checkbox::mouseClicked(glm::vec2 m, int button) {
    if (button != 0 || !active || !isMouseOver(m)) return false;
    selected = !selected;
    if (onChange_) onChange_(selected);
    return true;
}

bool Checkbox::keyPressed(int key, int) {
    if (!focused || (key != GLFW_KEY_ENTER && key != GLFW_KEY_SPACE)) return false;
    selected = !selected;
    if (onChange_) onChange_(selected);
    return true;
}

void Label::render(GuiGraphics& g, glm::vec2, float) {
    if (!visible) return;
    if (centered) g.centeredText(message, std::floor(x + width / 2), y, color);
    else g.text(message, x, y, color);
}

} // namespace mc
