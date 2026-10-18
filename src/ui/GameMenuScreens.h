// GameMenuScreens.h -- the in-game menus: the pause screen (vanilla
// PauseScreen), options and their sub-screens (video, controls, mouse, chat),
// and the chat input line.
#pragma once

#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "game/KeyBindings.h"
#include "ui/WidgetScreen.h"

namespace mc {

class Chat;

class PauseScreen : public WidgetScreen {
public:
    explicit PauseScreen(ScreenContext ctx);
    void init(glm::vec2 size) override;

protected:
    void renderContents(GuiGraphics& g, glm::vec2 mouse, float partialTick) override;
};

class OptionsScreen : public WidgetScreen {
public:
    explicit OptionsScreen(ScreenContext ctx);
    void init(glm::vec2 size) override;
    bool isPauseScreen() const override { return true; }

protected:
    void renderContents(GuiGraphics& g, glm::vec2 mouse, float partialTick) override;
};

// A list of option widgets in two columns under a title (vanilla OptionsSubScreen with its
// OptionsList): rows 25 apart between the 33-pixel header and footer, scrolling when they
// don't fit.
class OptionsSubScreen : public WidgetScreen {
public:
    OptionsSubScreen(ScreenContext ctx, std::string title) : WidgetScreen(ctx, std::move(title)) {}
    void init(glm::vec2 size) override;
    bool mouseScrolled(glm::vec2 mouse, double amount) override;
    std::optional<glm::vec4> clipOf(const Widget& w) const override;

protected:
    // Adds the screen's options (call option(...) for each, in order).
    virtual void addOptions() = 0;
    // Places a 150-wide option widget in the next cell.
    template <class W, class... Args>
    W& option(Args&&... args) {
        const float x = column_ == 0 ? std::floor(size_.x / 2) - 155 : std::floor(size_.x / 2) + 5;
        const float y = 32.0f + 25.0f * float(row_) + 4;
        W& w = add<W>(x, y, 150.0f, 20.0f, std::forward<Args>(args)...);
        listed_.emplace_back(&w, y);
        if (++column_ == 2) column_ = 0, ++row_;
        return w;
    }
    void renderContents(GuiGraphics& g, glm::vec2 mouse, float partialTick) override;
    Options& opts() const { return ctx_.host->options(); }
    void changed() const { ctx_.host->optionsChanged(); }
    std::string onOff(bool on) const { return t(on ? "options.on" : "options.off"); }

private:
    float listBottom() const { return size_.y - 33; }
    float maxScroll() const;
    void layoutList();
    int row_ = 0, column_ = 0;
    std::vector<std::pair<Widget*, float>> listed_; // the options and their unscrolled y
    float scroll_ = 0;
};

class VideoSettingsScreen : public OptionsSubScreen {
public:
    explicit VideoSettingsScreen(ScreenContext ctx);

protected:
    void addOptions() override;
};

class ControlsScreen : public OptionsSubScreen {
public:
    explicit ControlsScreen(ScreenContext ctx);

protected:
    void addOptions() override;
};

// Controls > Key Binds (vanilla KeyBindsScreen and KeyBindsList): the actions by category,
// each with its key and a Reset button. Clicking a key waits for the next key or mouse
// button to bind (Esc unbinds); keys used twice show red with what else uses them.
class KeyBindsScreen : public WidgetScreen {
public:
    explicit KeyBindsScreen(ScreenContext ctx);
    void init(glm::vec2 size) override;
    void render(GuiGraphics& g, glm::vec2 mouse, float partialTick) override;
    bool mouseClicked(glm::vec2 mouse, int button, int mods) override;
    bool mouseScrolled(glm::vec2 mouse, double amount) override;
    bool keyPressed(int key, int scancode, int mods) override;
    bool waitingForKey() const override { return selected_.has_value(); }
    // What a key's button shows: "W", "Left Shift", "Right Button", "Not Bound".
    std::string keyText(const InputKey& key) const;

private:
    // KeyBindsList: rows 20 apart, 340 wide, between the 33-pixel header and footer.
    static constexpr float kRowHeight = 20, kRowWidth = 340, kTop = 33;
    struct Row {
        std::string label; // the category's title, or the action's name
        std::optional<KeyAction> action;
        std::unique_ptr<Button> change, reset;
    };
    float listBottom() const { return size_.y - 33; }
    float rowTop(size_t index) const { return kTop + 4 - std::floor(scroll_) + float(index) * kRowHeight; }
    float maxScroll() const;
    bool inList(glm::vec2 mouse) const { return mouse.y >= kTop && mouse.y < listBottom(); }
    void layoutRows();
    void refresh();
    void bind(const InputKey& key);
    KeyBindings& keys() const;

    std::vector<Row> rows_;
    std::optional<KeyAction> selected_;
    float scroll_ = 0;
    Button* resetAll_ = nullptr;
};

class MouseSettingsScreen : public OptionsSubScreen {
public:
    explicit MouseSettingsScreen(ScreenContext ctx);

protected:
    void addOptions() override;
};

class ChatSettingsScreen : public OptionsSubScreen {
public:
    explicit ChatSettingsScreen(ScreenContext ctx);

protected:
    void addOptions() override;
};

// The chat line (vanilla ChatScreen): Enter sends, Up/Down recall sent lines,
// Page Up/Down scroll the log; the game keeps running under it.
class ChatScreen : public WidgetScreen {
public:
    ChatScreen(ScreenContext ctx, Chat& chat, std::string initial);
    void init(glm::vec2 size) override;
    bool isPauseScreen() const override { return false; }
    bool keyPressed(int key, int scancode, int mods) override;
    bool mouseScrolled(glm::vec2 mouse, double amount) override;

protected:
    void renderBackground(GuiGraphics& g) override;

private:
    Chat& chat_;
    std::string initial_;
    EditBox* input_ = nullptr;
    int historyIndex_ = -1; // into the sent lines, from the end; -1 = the line being typed
    std::string draft_;
};

// In bed (vanilla InBedChatScreen): the chat line and Leave Bed; Esc gets up too.
class SleepScreen : public ChatScreen {
public:
    SleepScreen(ScreenContext ctx, Chat& chat) : ChatScreen(ctx, chat, "") {}
    void init(glm::vec2 size) override;
    bool keyPressed(int key, int scancode, int mods) override;
    bool closeOnEscape() const override { return false; }
};

} // namespace mc
