// TitleScreens.h -- the screens before a world: the title (vanilla TitleScreen),
// the world list (SelectWorldScreen), Create World (with its Game, World and More
// tabs), a yes/no question (ConfirmScreen) and the loading and message screens.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ui/WidgetScreen.h"
#include "world/storage/WorldSave.h"

namespace mc {

class TitleScreen : public WidgetScreen {
public:
    explicit TitleScreen(ScreenContext ctx);
    void init(glm::vec2 size) override;
    bool closeOnEscape() const override { return false; }

protected:
    void renderBackground(GuiGraphics&) override {} // the game draws the panorama, unblurred
    void renderContents(GuiGraphics& g, glm::vec2 mouse, float partialTick) override;

private:
    bool easterEgg_ = false; // "Minceraft", one time in ten thousand
};

class SelectWorldScreen : public WidgetScreen {
public:
    explicit SelectWorldScreen(ScreenContext ctx);
    void init(glm::vec2 size) override;
    bool mouseClicked(glm::vec2 mouse, int button, int mods) override;
    bool mouseScrolled(glm::vec2 mouse, double amount) override;
    bool keyPressed(int key, int scancode, int mods) override;

protected:
    void renderContents(GuiGraphics& g, glm::vec2 mouse, float partialTick) override;

private:
    // WorldSelectionList: rows 36 apart (32 drawn), 270 wide, between y 48 and height - 64.
    static constexpr float kRowHeight = 36, kRowWidth = 270, kTop = 48;
    void filter();
    void select(int index);
    void play();
    float listBottom() const { return size_.y - 64; }
    float rowLeft() const { return std::floor(size_.x / 2) - kRowWidth / 2 + 2; }
    float rowTop(int index) const { return kTop + 4 - std::floor(scroll_) + float(index) * kRowHeight; }
    float maxScroll() const;
    int rowAt(glm::vec2 mouse) const;

    std::vector<WorldSummary> all_, shown_;
    int selected_ = -1;
    float scroll_ = 0;
    double lastClick_ = -1.0;
    int lastClickRow_ = -1;
    EditBox* search_ = nullptr;
    std::vector<Button*> needSelection_;
};

class CreateWorldScreen : public WidgetScreen {
public:
    explicit CreateWorldScreen(ScreenContext ctx);
    void init(glm::vec2 size) override;
    bool keyPressed(int key, int scancode, int mods) override;

protected:
    void renderBackground(GuiGraphics& g) override;
    void renderContents(GuiGraphics& g, glm::vec2 mouse, float partialTick) override;

private:
    static constexpr float kTabBarHeight = 24, kFooterHeight = 33;
    void showTab(int tab);
    // Vanilla WorldCreationUiState listeners: hardcore locks the difficulty to Hard and
    // commands off; the debug world type is spectator with commands.
    void refreshStates();
    void create();
    bool hardcore() const { return gameMode_ == 1; }
    bool debug() const { return worldType_ == 2; }

    int tab_ = 0;
    std::string name_;
    int gameMode_ = 0; // survival, hardcore, creative
    Difficulty difficulty_ = Difficulty::Normal;
    bool cheats_ = false;
    int worldType_ = 0; // default, flat, debug
    std::string seed_;
    TabButton* tabs_[3] = {};
    std::vector<Widget*> tabWidgets_[3];
    EditBox* nameBox_ = nullptr;
    CycleButton *gameModeButton_ = nullptr, *difficultyButton_ = nullptr, *commandsButton_ = nullptr;
};

// A question with two answers (vanilla ConfirmScreen); Esc answers no.
class ConfirmScreen : public WidgetScreen {
public:
    ConfirmScreen(ScreenContext ctx, std::string title, std::string message, std::string yes, std::string no,
                  std::function<void(bool)> answer);
    void init(glm::vec2 size) override;
    bool keyPressed(int key, int scancode, int mods) override;

protected:
    void renderContents(GuiGraphics& g, glm::vec2 mouse, float partialTick) override;

private:
    float titleTop() const;
    std::string message_, yes_, no_;
    std::vector<std::string> lines_; // the message wrapped to the screen
    std::function<void(bool)> answer_;
};

// The spawn area generating (vanilla LevelLoadingScreen: the percentage over a map of
// chunk stages), or with a message, one line in the middle (vanilla GenericMessageScreen,
// "Saving world"). The game replaces it when it is done.
class LoadingScreen : public WidgetScreen {
public:
    explicit LoadingScreen(ScreenContext ctx, std::string message = "");
    bool closeOnEscape() const override { return false; }

protected:
    void renderContents(GuiGraphics& g, glm::vec2 mouse, float partialTick) override;

private:
    std::string message_;
};

} // namespace mc
