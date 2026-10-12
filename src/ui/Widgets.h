// Widgets.h -- vanilla GUI widgets drawn with the pack's sprites: buttons,
// cycle buttons ("Label: Value"), sliders, text boxes, checkboxes and labels.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace mc {

class GuiGraphics;

class Widget {
public:
    Widget(float x, float y, float width, float height, std::string message)
        : x(x), y(y), width(width), height(height), message(std::move(message)) {}
    virtual ~Widget() = default;

    virtual void render(GuiGraphics& g, glm::vec2 mouse, float partialTick) = 0;
    virtual bool mouseClicked(glm::vec2, int) { return false; }
    virtual bool mouseReleased(glm::vec2, int) { return false; }
    virtual bool mouseDragged(glm::vec2, int) { return false; }
    virtual bool keyPressed(int, int) { return false; }
    virtual bool charTyped(char32_t) { return false; }
    virtual bool focusable() const { return active && visible; }

    bool isMouseOver(glm::vec2 m) const {
        return visible && m.x >= x && m.y >= y && m.x < x + width && m.y < y + height;
    }

    float x, y, width, height;
    std::string message;
    bool active = true, visible = true, focused = false;
    std::string tooltip;

protected:
    // Vanilla renderScrollingString: centered between the margins, the message's vertical middle.
    void renderCenteredText(GuiGraphics& g, uint32_t color) const;
};

class Button : public Widget {
public:
    using Action = std::function<void(Button&)>;
    Button(float x, float y, float width, float height, std::string message, Action onPress)
        : Widget(x, y, width, height, std::move(message)), onPress(std::move(onPress)) {}
    void render(GuiGraphics& g, glm::vec2 mouse, float partialTick) override;
    bool mouseClicked(glm::vec2 m, int button) override;
    bool keyPressed(int key, int mods) override;
    void press() {
        if (onPress) onPress(*this);
    }
    Action onPress;
    // A GUI sprite drawn in the middle (vanilla SpriteIconButton), usually with no message.
    std::string icon;
    float iconWidth = 15, iconHeight = 15;
};

// One tab of a tab bar (vanilla TabButton): the selected tab opens into the page below.
class TabButton : public Button {
public:
    using Button::Button;
    void render(GuiGraphics& g, glm::vec2 mouse, float partialTick) override;
    bool selected = false;
};

// A button that steps through values, showing "Label: Value" (vanilla CycleButton).
class CycleButton : public Button {
public:
    CycleButton(float x, float y, float width, float height, std::string label, std::vector<std::string> values,
                int index, std::function<void(int)> onChange);
    int index() const { return index_; }
    // Shows value i; `notify` false only changes what the button shows.
    void setIndex(int i, bool notify = true);

private:
    void refresh();
    std::string label_;
    std::vector<std::string> values_;
    int index_ = 0;
    std::function<void(int)> onChange_;
};

// A slider over 0..1 whose label describes the value (vanilla AbstractSliderButton).
class Slider : public Widget {
public:
    Slider(float x, float y, float width, float height, double value, std::function<std::string(double)> label,
           std::function<void(double)> onChange);
    void render(GuiGraphics& g, glm::vec2 mouse, float partialTick) override;
    bool mouseClicked(glm::vec2 m, int button) override;
    bool mouseReleased(glm::vec2 m, int button) override;
    bool mouseDragged(glm::vec2 m, int button) override;
    bool keyPressed(int key, int mods) override;
    double value() const { return value_; }
    void setValue(double v);

private:
    double value_;
    bool dragging_ = false;
    bool hovered_ = false;
    std::function<std::string(double)> label_;
    std::function<void(double)> onChange_;
};

// A one-line text box with cursor, selection and scrolling (vanilla EditBox).
class EditBox : public Widget {
public:
    EditBox(float x, float y, float width, float height, std::string hint = "");
    void render(GuiGraphics& g, glm::vec2 mouse, float partialTick) override;
    bool mouseClicked(glm::vec2 m, int button) override;
    bool keyPressed(int key, int mods) override;
    bool charTyped(char32_t c) override;

    std::string value() const;
    void setValue(const std::string& text);
    void moveCursorToEnd();

    int maxLength = 32;
    bool bordered = true;
    uint32_t textColor = 0xFFE0E0E0;
    std::string hint;
    std::function<void(const std::string&)> onChange;
    std::function<void()> onSubmit; // Enter
    std::function<bool(char32_t)> filter; // characters it accepts

private:
    void insert(std::u32string_view text);
    void deleteChars(int count);
    void setCursor(size_t pos, bool select);
    void changed();
    std::u32string text_;
    size_t cursor_ = 0, highlight_ = 0, display_ = 0;
    const GuiGraphics* graphics_ = nullptr; // for widths while editing
};

class Checkbox : public Widget {
public:
    Checkbox(float x, float y, std::string label, bool selected, std::function<void(bool)> onChange);
    void render(GuiGraphics& g, glm::vec2 mouse, float partialTick) override;
    bool mouseClicked(glm::vec2 m, int button) override;
    bool keyPressed(int key, int mods) override;
    bool selected;

private:
    std::function<void(bool)> onChange_;
};

// Text that is drawn and nothing else (vanilla StringWidget).
class Label : public Widget {
public:
    Label(float x, float y, float width, std::string text, uint32_t color = 0xFFFFFFFF, bool centered = true)
        : Widget(x, y, width, 9, std::move(text)), color(color), centered(centered) {}
    void render(GuiGraphics& g, glm::vec2 mouse, float partialTick) override;
    bool focusable() const override { return false; }
    uint32_t color;
    bool centered;
};

} // namespace mc
