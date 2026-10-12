// GameMenuScreens.h -- the in-game menus: the pause screen (vanilla
// PauseScreen), options and their sub-screens (video, controls, mouse, chat),
// and the chat input line.
#pragma once

#include <cmath>
#include <string>

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

// A list of option widgets in two columns under a title (vanilla OptionsSubScreen).
class OptionsSubScreen : public WidgetScreen {
public:
    OptionsSubScreen(ScreenContext ctx, std::string title) : WidgetScreen(ctx, std::move(title)) {}
    void init(glm::vec2 size) override;

protected:
    // Adds the screen's options (call option(...) for each, in order).
    virtual void addOptions() = 0;
    // Places a 150-wide option widget in the next cell.
    template <class W, class... Args>
    W& option(Args&&... args) {
        const float x = column_ == 0 ? std::floor(size_.x / 2) - 155 : std::floor(size_.x / 2) + 5;
        W& w = add<W>(x, 32.0f + 25.0f * float(row_) + 4, 150.0f, 20.0f, std::forward<Args>(args)...);
        if (++column_ == 2) column_ = 0, ++row_;
        return w;
    }
    void renderContents(GuiGraphics& g, glm::vec2 mouse, float partialTick) override;
    Options& opts() const { return ctx_.host->options(); }
    void changed() const { ctx_.host->optionsChanged(); }
    std::string onOff(bool on) const { return t(on ? "options.on" : "options.off"); }

private:
    int row_ = 0, column_ = 0;
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

} // namespace mc
