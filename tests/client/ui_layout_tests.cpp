// Menu screens laid out at GUI scales 1-4 for common window sizes, without GL:
// every widget on screen, none overlapping another, every label translated and
// fitting its widget. Plus the title-side screens' behavior through a fake host.
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>
#include <gtest/gtest.h>

#include "game/Options.h"
#include "resources/FontSet.h"
#include "resources/Language.h"
#include "resources/ResourcePack.h"
#include "ui/GameMenuScreens.h"
#include "ui/GuiScale.h"
#include "ui/SignEditScreen.h"
#include "ui/TitleScreens.h"
#include "world/storage/WorldSave.h"

namespace mc {
namespace {

class FakeHost : public ScreenHost {
public:
    explicit FakeHost(std::filesystem::path saves) : saves_(std::move(saves)) {}

    void setScreen(std::unique_ptr<Screen> s) override { calls.push_back(s ? "set" : "set null"), last = std::move(s); }
    void pushScreen(std::unique_ptr<Screen> s) override { calls.push_back("push"), last = std::move(s); }
    void popScreen() override { calls.push_back("pop"); }
    void startWorld(const WorldParams& p) override {
        calls.push_back("start");
        started = p;
    }
    void leaveWorld() override { calls.push_back("leave"); }
    void quitGame() override { calls.push_back("quit"); }
    bool inWorld() const override { return world; }
    Options& options() override { return options_; }
    void optionsChanged() override {}
    std::filesystem::path savesDir() const override { return saves_; }
    const std::string& splash() const override { return splash_; }
    WorldLoadStatus worldLoadStatus() const override { return {}; }
    Difficulty difficulty() const override { return Difficulty::Normal; }
    void setDifficulty(Difficulty) override {}
    void sendChat(const std::string&) override {}
    void respawn() override { calls.push_back("respawn"); }
    void leaveBed() override { calls.push_back("leave bed"); }

    bool world = false;
    std::vector<std::string> calls;
    std::unique_ptr<Screen> last;
    WorldParams started;

private:
    std::filesystem::path saves_;
    Options options_;
    std::string splash_ = "Splash!";
};

class UiLayout : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        pack_ = std::make_unique<ResourcePack>(MC_DEFAULT_PACK_DIR);
        lang_ = std::make_unique<Language>();
        ASSERT_TRUE(lang_->load(*pack_));
        font_ = std::make_unique<FontSet>();
        ASSERT_TRUE(font_->load(*pack_));
    }
    static void TearDownTestSuite() {
        font_.reset();
        lang_.reset();
        pack_.reset();
    }
    void SetUp() override {
        saves_ = std::filesystem::path(::testing::TempDir()) / "ui_layout_saves";
        std::filesystem::remove_all(saves_);
        std::filesystem::create_directories(saves_);
        host_ = std::make_unique<FakeHost>(saves_);
    }
    void TearDown() override { std::filesystem::remove_all(saves_); }

    ScreenContext ctx() const {
        ScreenContext c;
        c.lang = lang_.get();
        c.host = host_.get();
        c.font = font_.get();
        return c;
    }
    void addWorld(const std::string& folder, const std::string& name, bool hardcore = false) {
        WorldInfo info;
        info.name = name;
        info.hardcore = hardcore;
        info.level = Json::object();
        ASSERT_TRUE(writeWorldInfo(saves_ / folder, info));
    }

    // GUI sizes: 854x480, 1280x720, 1920x1080 and 2560x1440 at each scale 1..4 they allow.
    static std::vector<glm::vec2> guiSizes() {
        std::vector<glm::vec2> sizes;
        for (glm::ivec2 window : {glm::ivec2(854, 480), glm::ivec2(1280, 720), glm::ivec2(1920, 1080), glm::ivec2(2560, 1440)})
            for (int s = 1; s <= std::min(4, autoGuiScale(window)); ++s) sizes.push_back(glm::vec2(window / s));
        return sizes;
    }

    // A translation key shown as is, like "menu.options": lowercase words joined by dots.
    static bool untranslated(const std::string& text) {
        if (text.empty() || text.front() == '.' || text.back() == '.' || text.find("..") != std::string::npos ||
            text.find('.') == std::string::npos)
            return false;
        for (char c : text)
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.')) return false;
        return true;
    }

    // Every visible widget on screen, no two overlapping, labels translated and fitting.
    void checkLayout(const WidgetScreen& screen, glm::vec2 size, const std::string& what) const {
        std::vector<const Widget*> shown;
        for (const auto& w : screen.widgets())
            if (w->visible) shown.push_back(w.get());
        for (const Widget* w : shown) {
            const std::string where = what + " at " + std::to_string(int(size.x)) + "x" + std::to_string(int(size.y)) +
                                      ": '" + w->message + "'";
            EXPECT_GE(w->x, 0.0f) << where;
            EXPECT_GE(w->y, 0.0f) << where;
            EXPECT_LE(w->x + w->width, size.x) << where;
            EXPECT_LE(w->y + w->height, size.y) << where;
            EXPECT_FALSE(untranslated(w->message)) << where;
            const auto* button = dynamic_cast<const Button*>(w);
            if (button && button->icon.empty() && dynamic_cast<const CycleButton*>(w) == nullptr) {
                EXPECT_FALSE(w->message.empty()) << where;
            }
            if (button || dynamic_cast<const Label*>(w)) {
                EXPECT_LE(font_->width(w->message), w->width - (button ? 4 : 0)) << where << " does not fit";
            }
            for (const Widget* o : shown) {
                if (o <= w) continue;
                const bool overlap = w->x < o->x + o->width && o->x < w->x + w->width && w->y < o->y + o->height &&
                                     o->y < w->y + w->height;
                EXPECT_FALSE(overlap) << where << " overlaps '" << o->message << "'";
            }
        }
    }

    void checkAllSizes(WidgetScreen& screen, const std::string& what) {
        for (glm::vec2 size : guiSizes()) {
            screen.init(size);
            checkLayout(screen, size, what);
        }
    }

    static std::unique_ptr<ResourcePack> pack_;
    static std::unique_ptr<Language> lang_;
    static std::unique_ptr<FontSet> font_;
    std::filesystem::path saves_;
    std::unique_ptr<FakeHost> host_;
};

std::unique_ptr<ResourcePack> UiLayout::pack_;
std::unique_ptr<Language> UiLayout::lang_;
std::unique_ptr<FontSet> UiLayout::font_;

const Widget* findWidget(const WidgetScreen& s, const std::string& message) {
    for (const auto& w : s.widgets())
        if (w->visible && w->message == message) return w.get();
    return nullptr;
}

glm::vec2 centerOf(const Widget& w) { return {w.x + w.width / 2, w.y + w.height / 2}; }

TEST_F(UiLayout, TitleAndWorldScreens) {
    addWorld("One", "One");
    addWorld("Two", "Two", true);
    TitleScreen title(ctx());
    checkAllSizes(title, "title");
    SelectWorldScreen worlds(ctx());
    checkAllSizes(worlds, "world list");
    CreateWorldScreen create(ctx());
    for (int tab = 0; tab < 3; ++tab) {
        checkAllSizes(create, "create world tab " + std::to_string(tab));
        create.keyPressed(GLFW_KEY_TAB, 0, GLFW_MOD_CONTROL); // the next tab
    }
    ConfirmScreen confirm(ctx(), "Are you sure?", "It goes away for ever and ever.", "Yes", "No", [](bool) {});
    checkAllSizes(confirm, "confirm");
}

TEST_F(UiLayout, MenuScreens) {
    for (bool world : {false, true}) {
        host_->world = world;
        const std::string suffix = world ? " in a world" : "";
        PauseScreen pause(ctx());
        if (world) checkAllSizes(pause, "pause");
        OptionsScreen options(ctx());
        checkAllSizes(options, "options" + suffix);
        VideoSettingsScreen video(ctx());
        checkAllSizes(video, "video settings" + suffix);
        ControlsScreen controls(ctx());
        checkAllSizes(controls, "controls" + suffix);
        MouseSettingsScreen mouse(ctx());
        checkAllSizes(mouse, "mouse settings" + suffix);
        ChatSettingsScreen chat(ctx());
        checkAllSizes(chat, "chat settings" + suffix);
    }
}

TEST_F(UiLayout, TitleButtons) {
    TitleScreen title(ctx());
    title.init({427, 240});
    // Vanilla: Singleplayer at height / 4 + 48, Options and Quit 84 lower.
    const Widget* single = findWidget(title, "Singleplayer");
    ASSERT_NE(single, nullptr);
    EXPECT_EQ(single->x, 113);
    EXPECT_EQ(single->y, 108);
    const Widget* quit = findWidget(title, "Quit Game");
    ASSERT_NE(quit, nullptr);
    EXPECT_EQ(quit->y, 192);
    // No worlds: Singleplayer opens Create World.
    title.mouseClicked(centerOf(*single), 0, 0);
    ASSERT_EQ(host_->calls, (std::vector<std::string>{"push"}));
    EXPECT_NE(dynamic_cast<CreateWorldScreen*>(host_->last.get()), nullptr);
    addWorld("One", "One");
    title.mouseClicked(centerOf(*single), 0, 0);
    EXPECT_NE(dynamic_cast<SelectWorldScreen*>(host_->last.get()), nullptr);
    title.mouseClicked(centerOf(*quit), 0, 0);
    EXPECT_EQ(host_->calls, (std::vector<std::string>{"push", "push", "quit"}));
}

TEST_F(UiLayout, WorldListSelectsSearchesAndPlays) {
    addWorld("Alpha", "Alpha");
    addWorld("Beta", "Beta");
    SelectWorldScreen worlds(ctx());
    worlds.init({427, 240});
    const Widget* play = findWidget(worlds, "Play Selected World");
    ASSERT_NE(play, nullptr);
    EXPECT_FALSE(play->active); // nothing selected yet
    // Typing goes to the search box: only Beta is left.
    for (char32_t c : std::u32string(U"bet")) worlds.charTyped(c);
    // The first row (at y 48 + 4) is Beta now; clicking its name selects it.
    worlds.mouseClicked({213 + 20, 48 + 4 + 10}, 0, 0);
    EXPECT_TRUE(play->active);
    worlds.mouseClicked(centerOf(*play), 0, 0);
    ASSERT_FALSE(host_->calls.empty());
    ASSERT_EQ(host_->calls.back(), "start");
    EXPECT_EQ(host_->started.folder, "Beta");
    EXPECT_FALSE(host_->started.commandLine);
}

TEST_F(UiLayout, CreateWorldRules) {
    addWorld("New World", "New World");
    CreateWorldScreen create(ctx());
    create.init({427, 240});
    // The game mode cycles survival -> hardcore: the difficulty is locked to Hard.
    const auto* mode = dynamic_cast<const CycleButton*>(findWidget(create, "Game Mode: Survival"));
    ASSERT_NE(mode, nullptr);
    create.mouseClicked(centerOf(*mode), 0, 0);
    EXPECT_EQ(mode->message, "Game Mode: Hardcore");
    const Widget* difficulty = findWidget(create, "Difficulty: Hard");
    ASSERT_NE(difficulty, nullptr);
    EXPECT_FALSE(difficulty->active);
    const Widget* commands = findWidget(create, "Allow Commands: OFF");
    ASSERT_NE(commands, nullptr);
    EXPECT_FALSE(commands->active);
    // Enter (with no button focused: it would press that) creates it, in a new folder
    // next to the existing "New World".
    create.mouseClicked({2, 120}, 0, 0);
    create.keyPressed(GLFW_KEY_ENTER, 0, 0);
    ASSERT_FALSE(host_->calls.empty());
    ASSERT_EQ(host_->calls.back(), "start");
    EXPECT_EQ(host_->started.name, "New World");
    EXPECT_EQ(host_->started.folder, "New World (1)");
    EXPECT_TRUE(host_->started.hardcore);
    EXPECT_EQ(host_->started.difficulty, Difficulty::Hard);
    EXPECT_EQ(host_->started.gameMode, "survival");
    EXPECT_FALSE(host_->started.cheats);
}

TEST_F(UiLayout, ConfirmAnswersAndEscapeSaysNo) {
    std::vector<bool> answers;
    ConfirmScreen confirm(ctx(), "Title", "Message", "Yes", "No", [&](bool yes) { answers.push_back(yes); });
    confirm.init({427, 240});
    confirm.mouseClicked(centerOf(*findWidget(confirm, "Yes")), 0, 0);
    confirm.keyPressed(GLFW_KEY_ESCAPE, 0, 0);
    EXPECT_EQ(answers, (std::vector<bool>{true, false}));
}

TEST_F(UiLayout, SignEditorTypesWithinTheSignAndLeavesOnDone) {
    auto sign = std::make_shared<SignBlockEntity>(BlockPos{0, 0, 0}, false);
    sign->front.lines[1] = "old";
    bool closed = false;
    SignEditScreen screen(ctx(), sign, true, "oak", true, [&] { closed = true; });
    screen.init({427, 240});
    checkLayout(screen, {427, 240}, "sign editor");
    const Widget* done = findWidget(screen, "Done");
    ASSERT_NE(done, nullptr);
    EXPECT_EQ(done->x, 427 / 2 - 100); // vanilla: (width / 2 - 100, height / 4 + 144)
    EXPECT_EQ(done->y, 240 / 4 + 144);
    // Typing fills the first line; Enter (and Down) go to the next, wrapping after four.
    for (char32_t c : U"Hello there") screen.charTyped(c);
    EXPECT_EQ(sign->front.lines[0], "Hello there");
    screen.keyPressed(GLFW_KEY_ENTER, 0, 0);
    EXPECT_EQ(screen.line(), 1);
    screen.charTyped(U'!'); // the cursor starts at the line's end
    EXPECT_EQ(sign->front.lines[1], "old!");
    screen.keyPressed(GLFW_KEY_UP, 0, 0);
    EXPECT_EQ(screen.line(), 0);
    // Ctrl+Backspace takes a word, Backspace a character; § can't be typed.
    screen.keyPressed(GLFW_KEY_BACKSPACE, 0, Screen::kControl);
    EXPECT_EQ(sign->front.lines[0], "Hello ");
    screen.keyPressed(GLFW_KEY_BACKSPACE, 0, 0);
    EXPECT_EQ(sign->front.lines[0], "Hello");
    EXPECT_FALSE(screen.charTyped(U'§'));
    // Nothing goes in past the sign's 90 pixels.
    for (int i = 0; i < 40; ++i) screen.charTyped(U'W');
    const float width = font_->width(sign->front.lines[0]);
    EXPECT_LE(width, 90.0f);
    EXPECT_GT(width, 80.0f);
    EXPECT_TRUE(sign->back.empty());
    screen.mouseClicked(centerOf(*done), 0, 0);
    EXPECT_EQ(host_->calls.back(), "pop");
    screen.removed();
    EXPECT_TRUE(closed);
    // Hanging signs hold 60 pixels.
    auto hanging = std::make_shared<SignBlockEntity>(BlockPos{0, 0, 0}, true);
    SignEditScreen narrow(ctx(), hanging, false, "oak", false);
    narrow.init({427, 240});
    for (int i = 0; i < 40; ++i) narrow.charTyped(U'W');
    EXPECT_LE(font_->width(hanging->back.lines[0]), 60.0f);
    EXPECT_TRUE(hanging->front.empty());
}

} // namespace
} // namespace mc
