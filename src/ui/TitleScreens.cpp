#include "ui/TitleScreens.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <format>
#include <random>

#include <GLFW/glfw3.h>

#include "game/Options.h"
#include "resources/FontSet.h"
#include "ui/GameMenuScreens.h"
#include "ui/GuiGraphics.h"

namespace mc {
namespace {

constexpr std::string_view kVersion = "minecraft-cpp 0.9 (1.21.8 assets)";

double nowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Java's short localized date and time for en_US: "10/11/26, 7:04 PM".
std::string shortDate(int64_t millis) {
    const std::time_t t = std::time_t(millis / 1000);
    std::tm tm{};
    localtime_r(&t, &tm);
    const int hour = tm.tm_hour % 12 == 0 ? 12 : tm.tm_hour % 12;
    return std::format("{}/{}/{:02}, {}:{:02} {}", tm.tm_mon + 1, tm.tm_mday, tm.tm_year % 100, hour, tm.tm_min,
                       tm.tm_hour < 12 ? "AM" : "PM");
}

// Vanilla FrameLayout.alignInDimension: (int) lerp(delta, 0, room).
float align(float room, float delta) { return float(int(room * delta)); }

} // namespace

// --- Title ---

TitleScreen::TitleScreen(ScreenContext ctx) : WidgetScreen(ctx, "") {
    std::mt19937 rng(std::random_device{}());
    easterEgg_ = std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < 1.0e-4f;
}

void TitleScreen::init(glm::vec2 size) {
    WidgetScreen::init(size);
    clearWidgets();
    const float cx = std::floor(size.x / 2), l = std::floor(size.y / 4) + 48;
    const ScreenContext ctx = ctx_;
    add<Button>(cx - 100, l, 200, 20, t("menu.singleplayer"), [ctx](Button&) {
        // No worlds yet: straight to Create World (vanilla does so from the empty list).
        if (listWorlds(ctx.host->savesDir()).empty()) ctx.host->pushScreen(std::make_unique<CreateWorldScreen>(ctx));
        else ctx.host->pushScreen(std::make_unique<SelectWorldScreen>(ctx));
    });
    add<Button>(cx - 100, l + 24, 200, 20, t("menu.multiplayer"), nullptr).active = false;
    add<Button>(cx - 100, l + 48, 200, 20, t("menu.online"), nullptr).active = false;
    Button& language = add<Button>(cx - 124, l + 84, 20, 20, "", nullptr);
    language.icon = "icon/language";
    language.tooltip = t("options.language");
    language.active = false; // en_us only
    add<Button>(cx - 100, l + 84, 98, 20, t("menu.options"),
                [ctx](Button&) { ctx.host->pushScreen(std::make_unique<OptionsScreen>(ctx)); });
    add<Button>(cx + 2, l + 84, 98, 20, t("menu.quit"), [ctx](Button&) { ctx.host->quitGame(); });
    Button& accessibility = add<Button>(cx + 104, l + 84, 20, 20, "", nullptr);
    accessibility.icon = "icon/accessibility";
    accessibility.tooltip = t("options.accessibility");
    accessibility.active = false;
}

void TitleScreen::renderContents(GuiGraphics& g, glm::vec2, float) {
    const float cx = std::floor(size_.x / 2);
    // LogoRenderer: the top 44 rows of the 256x64 logo at y 30, the edition overlapping it by 7.
    g.texture(easterEgg_ ? "minecraft:gui/title/minceraft" : "minecraft:gui/title/minecraft", cx - 128, 30, 0, 0, 256,
              44, 256, 64);
    g.texture("minecraft:gui/title/edition", cx - 64, 30 + 44 - 7, 0, 0, 128, 14, 128, 16);
    // SplashRenderer: yellow, tilted 20 degrees up, pulsing once a second, smaller when long.
    const std::string& splash = ctx_.host->splash();
    if (!splash.empty()) {
        const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count() %
                             1000;
        float scale = 1.8f - std::abs(std::sin(float(ms) / 1000.0f * 6.2831855f) * 0.1f);
        scale = scale * 100.0f / float(g.textWidth(splash) + 32);
        const float a = -3.1415927f / 9.0f, c = std::cos(a) * scale, s = std::sin(a) * scale;
        g.batch().setTransform(glm::mat3(glm::vec3(c, s, 0), glm::vec3(-s, c, 0), glm::vec3(cx + 123, 69, 1)));
        g.centeredText(splash, 0, -8, 0xFFFFFF00);
        g.batch().resetTransform();
    }
    g.text(kVersion, 2, size_.y - 10, 0xFFFFFFFF);
    // Where vanilla puts its copyright line.
    const std::string notice = "Not an official Minecraft product.";
    g.text(notice, size_.x - float(g.textWidth(notice)) - 2, size_.y - 10, 0xFFFFFFFF);
}

// --- Select World ---

SelectWorldScreen::SelectWorldScreen(ScreenContext ctx) : WidgetScreen(ctx, ctx.translate("selectWorld.title")) {}

void SelectWorldScreen::filter() {
    const std::string q = search_ ? lower(search_->value()) : "";
    shown_.clear();
    for (const WorldSummary& w : all_)
        if (q.empty() || lower(w.info.name).find(q) != std::string::npos || lower(w.folder).find(q) != std::string::npos)
            shown_.push_back(w);
    scroll_ = std::clamp(scroll_, 0.0f, maxScroll());
    select(-1);
}

void SelectWorldScreen::select(int index) {
    selected_ = index;
    for (Button* b : needSelection_) b->active = selected_ >= 0;
}

float SelectWorldScreen::maxScroll() const {
    // AbstractScrollArea: the rows against the list's height less 4.
    const float content = float(shown_.size()) * kRowHeight;
    return std::max(0.0f, content - (listBottom() - kTop - 4));
}

void SelectWorldScreen::init(glm::vec2 size) {
    WidgetScreen::init(size);
    const std::string keep = search_ ? search_->value() : "";
    clearWidgets();
    needSelection_.clear();
    const float cx = std::floor(size.x / 2);
    EditBox& search = add<EditBox>(cx - 100, 22, 200, 20);
    search.setValue(keep);
    search_ = &search;
    const ScreenContext ctx = ctx_;
    needSelection_.push_back(
        &add<Button>(cx - 154, size.y - 52, 150, 20, t("selectWorld.select"), [this](Button&) { play(); }));
    add<Button>(cx + 4, size.y - 52, 150, 20, t("selectWorld.create"),
                [ctx](Button&) { ctx.host->pushScreen(std::make_unique<CreateWorldScreen>(ctx)); });
    add<Button>(cx - 154, size.y - 28, 72, 20, t("selectWorld.edit"), nullptr).active = false;
    needSelection_.push_back(&add<Button>(cx - 76, size.y - 28, 72, 20, t("selectWorld.delete"), [this, ctx](Button&) {
        if (selected_ < 0) return;
        const WorldSummary w = shown_[size_t(selected_)];
        ctx.host->pushScreen(std::make_unique<ConfirmScreen>(
            ctx, ctx.translate("selectWorld.deleteQuestion"),
            ctx.translate("selectWorld.deleteWarning", {w.info.name}), ctx.translate("selectWorld.deleteButton"),
            ctx.translate("gui.cancel"), [ctx, w](bool yes) {
                if (yes) deleteWorld(ctx.host->savesDir(), w.folder);
                ctx.host->popScreen(); // back to the list, which reads the folder again
            }));
    }));
    add<Button>(cx + 4, size.y - 28, 72, 20, t("selectWorld.recreate"), nullptr).active = false;
    add<Button>(cx + 82, size.y - 28, 72, 20, t("gui.back"), [ctx](Button&) { ctx.host->popScreen(); });
    all_ = listWorlds(ctx_.host->savesDir());
    filter();
    search.onChange = [this](const std::string&) { filter(); };
    setFocus(&search); // typing searches at once
}

int SelectWorldScreen::rowAt(glm::vec2 mouse) const {
    // AbstractSelectionList.getEntryAtPosition
    const float cx = std::floor(size_.x / 2);
    if (mouse.x < cx - kRowWidth / 2 || mouse.x > cx + kRowWidth / 2 || mouse.y < kTop || mouse.y >= listBottom())
        return -1;
    const int offset = int(std::floor(mouse.y - kTop)) + int(scroll_) - 4;
    const int row = offset / int(kRowHeight);
    return offset >= 0 && row < int(shown_.size()) ? row : -1;
}

bool SelectWorldScreen::mouseClicked(glm::vec2 mouse, int button, int mods) {
    if (WidgetScreen::mouseClicked(mouse, button, mods)) return true;
    const int row = rowAt(mouse);
    if (row < 0) return false;
    select(row);
    setFocus(nullptr); // the list has the focus
    // WorldListEntry.mouseClicked: the icon plays at once, a second click within 250 ms too.
    const double now = nowSeconds();
    if (mouse.x - rowLeft() <= 32 || (row == lastClickRow_ && now - lastClick_ < 0.25)) {
        play();
    } else {
        lastClick_ = now;
        lastClickRow_ = row;
    }
    return true;
}

bool SelectWorldScreen::mouseScrolled(glm::vec2, double amount) {
    scroll_ = std::clamp(scroll_ - float(amount) * kRowHeight / 2, 0.0f, maxScroll());
    return true;
}

bool SelectWorldScreen::keyPressed(int key, int scancode, int mods) {
    if (focused_ == nullptr && !shown_.empty() && (key == GLFW_KEY_UP || key == GLFW_KEY_DOWN)) {
        select(std::clamp(selected_ + (key == GLFW_KEY_UP ? -1 : 1), 0, int(shown_.size()) - 1));
        // Keep the selected row in view.
        const float top = float(selected_) * kRowHeight, view = listBottom() - kTop - 4;
        scroll_ = std::clamp(scroll_, std::max(0.0f, top + kRowHeight - view), top);
        return true;
    }
    if ((key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER || key == GLFW_KEY_SPACE) && focused_ == nullptr &&
        selected_ >= 0) {
        play();
        return true;
    }
    return WidgetScreen::keyPressed(key, scancode, mods);
}

void SelectWorldScreen::play() {
    if (selected_ < 0) return;
    const WorldSummary& w = shown_[size_t(selected_)];
    WorldParams params;
    params.folder = w.folder;
    params.name = w.info.name;
    params.gameMode = w.info.gameMode;
    params.difficulty = difficultyFromName(w.info.difficulty).value_or(Difficulty::Normal);
    params.cheats = w.info.cheats;
    params.hardcore = w.info.hardcore;
    ctx_.host->startWorld(params);
}

void SelectWorldScreen::renderContents(GuiGraphics& g, glm::vec2 mouse, float) {
    const float cx = std::floor(size_.x / 2), bottom = listBottom();
    // renderListBackground and renderListSeparators.
    g.texture(inWorld() ? "minecraft:gui/inworld_menu_list_background" : "minecraft:gui/menu_list_background", 0, kTop,
              size_.x, bottom + std::floor(scroll_), size_.x, bottom - kTop, 32, 32);
    g.texture(inWorld() ? "minecraft:gui/inworld_header_separator" : "minecraft:gui/header_separator", 0, kTop - 2, 0,
              0, size_.x, 2, 32, 2);
    g.texture(inWorld() ? "minecraft:gui/inworld_footer_separator" : "minecraft:gui/footer_separator", 0, bottom, 0, 0,
              size_.x, 2, 32, 2);
    g.enableScissor(0, kTop, size_.x, bottom);
    const float left = rowLeft();
    const int hovered = rowAt(mouse);
    for (int i = 0; i < int(shown_.size()); ++i) {
        const float top = rowTop(i);
        if (top + kRowHeight - 4 < kTop || top > bottom) continue;
        const WorldSummary& w = shown_[size_t(i)];
        if (i == selected_) {
            // renderSelection: white while the list has the focus, else gray.
            const float x0 = cx - kRowWidth / 2, x1 = cx + kRowWidth / 2;
            g.fill(x0, top - 2, x1, top + 32 + 2, focused_ == nullptr ? 0xFFFFFFFF : 0xFF808080);
            g.fill(x0 + 1, top - 1, x1 - 1, top + 32 + 1, 0xFF000000);
        }
        // WorldListEntry.render
        std::string name = w.info.name;
        if (name.empty()) name = t("selectWorld.world") + " " + std::to_string(i + 1);
        std::string id = w.folder;
        if (w.info.lastPlayed > 0) id += " (" + shortDate(w.info.lastPlayed) + ")";
        // LevelSummary.createInfo: the mode (hardcore in red), commands, the version.
        std::string info = w.info.hardcore ? "" : t("gameMode." + w.info.gameMode);
        if (w.info.cheats) info += ", " + t("selectWorld.commands");
        info += ", " + t("selectWorld.version") + " 1.21.8";
        g.text(name, left + 35, top + 1, 0xFFFFFFFF, false);
        g.text(id, left + 35, top + 12, 0xFF808080, false);
        float infoX = left + 35;
        if (w.info.hardcore) infoX = g.text(t("gameMode.hardcore"), infoX, top + 21, 0xFFFF0000, false);
        g.text(info, infoX, top + 21, 0xFF808080, false);
        const Texture* icon = w.hasIcon ? g.fileTexture(ctx_.host->savesDir() / w.folder / "icon.png") : nullptr;
        if (icon) g.blitTexture(*icon, left, top, 32, 32);
        else g.texture("minecraft:misc/unknown_server", left, top, 0, 0, 32, 32, 32, 32);
        if (i == hovered) {
            g.fill(left, top, left + 32, top + 32, 0xA0909090);
            g.sprite(mouse.x - left < 32 ? "world_list/join_highlighted" : "world_list/join", left, top, 32, 32);
        }
    }
    g.disableScissor();
    // The scrollbar when the rows don't fit (AbstractScrollArea.renderScrollbar).
    if (maxScroll() > 0) {
        const float view = bottom - kTop, content = float(shown_.size()) * kRowHeight;
        const float thumb = std::clamp(float(int(view * view / content)), 32.0f, view - 8);
        const float x = left + kRowWidth + 6 + 2;
        const float y = std::max(kTop, float(int(scroll_) * int(view - thumb) / int(maxScroll())) + kTop);
        g.sprite("widget/scroller_background", x, kTop, 6, view);
        g.sprite("widget/scroller", x, y, 6, thumb);
    }
    g.centeredText(title_, cx, 8, 0xFFFFFFFF);
}

// --- Create World ---

CreateWorldScreen::CreateWorldScreen(ScreenContext ctx)
    : WidgetScreen(ctx, ctx.translate("selectWorld.create")), name_(ctx.translate("selectWorld.newWorld")) {}

void CreateWorldScreen::init(glm::vec2 size) {
    WidgetScreen::init(size);
    clearWidgets();
    for (auto& list : tabWidgets_) list.clear();
    const ScreenContext ctx = ctx_;
    // TabNavigationBar.arrangeElements: equal tabs (an even width) across at most 400 - 28.
    const int barWidth = int(std::min(400.0f, size.x)) - 28;
    auto roundUpEven = [](int v) { return (v + 1) / 2 * 2; };
    const float tabWidth = float(roundUpEven(barWidth / 3));
    const float tabX = float(roundUpEven((int(size.x) - barWidth) / 2));
    const char* tabKeys[3] = {"createWorld.tab.game.title", "createWorld.tab.world.title",
                              "createWorld.tab.more.title"};
    for (int i = 0; i < 3; ++i)
        tabs_[i] = &add<TabButton>(tabX + tabWidth * float(i), 0, tabWidth, kTabBarHeight, t(tabKeys[i]),
                                   [this, i](Button&) { showTab(i); });
    // Each tab's grid sits between the bar and the footer, centered across and a sixth
    // of the spare height down (GridLayoutTab.doLayout).
    const float areaHeight = size.y - kFooterHeight - kTabBarHeight;
    auto origin = [&](float w, float h) {
        return glm::vec2(align(size.x - w, 0.5f), kTabBarHeight + align(areaHeight - h, 1.0f / 6.0f));
    };
    auto inTab = [this](int tab, Widget& w) -> Widget& {
        tabWidgets_[tab].push_back(&w);
        return w;
    };

    // Game: a 210-wide column, rows 8 apart; the name box is labeled (label, 4, box).
    glm::vec2 o = origin(210, 33 + 8 + 20 + 8 + 20 + 8 + 20);
    inTab(0, add<Label>(o.x + 1, o.y, 208, t("selectWorld.enterName"), 0xFFFFFFFF, false));
    nameBox_ = &add<EditBox>(o.x + 1, o.y + 13, 208, 20);
    nameBox_->setValue(name_);
    nameBox_->onChange = [this](const std::string& v) {
        name_ = v;
        refreshStates();
    };
    inTab(0, *nameBox_);
    gameModeButton_ = &add<CycleButton>(
        o.x, o.y + 41, 210, 20, t("selectWorld.gameMode"),
        std::vector<std::string>{t("selectWorld.gameMode.survival"), t("selectWorld.gameMode.hardcore"),
                                 t("selectWorld.gameMode.creative"), t("selectWorld.gameMode.spectator")},
        gameMode_, [this](int i) {
            gameMode_ = i % 3; // spectator only shows for debug worlds
            refreshStates();
        });
    inTab(0, *gameModeButton_);
    std::vector<std::string> difficulties;
    for (const char* d : {"peaceful", "easy", "normal", "hard"})
        difficulties.push_back(t(std::string("options.difficulty.") + d));
    difficultyButton_ = &add<CycleButton>(o.x, o.y + 69, 210, 20, t("options.difficulty"), difficulties,
                                          int(difficulty_), [this](int i) {
                                              difficulty_ = Difficulty(i);
                                              refreshStates();
                                          });
    inTab(0, *difficultyButton_);
    commandsButton_ = &add<CycleButton>(o.x, o.y + 97, 210, 20, t("selectWorld.allowCommands.new"),
                                        std::vector<std::string>{t("options.off"), t("options.on")}, int(cheats_),
                                        [this](int i) {
                                            cheats_ = i == 1;
                                            refreshStates();
                                        });
    commandsButton_->tooltip = t("selectWorld.allowCommands.info");
    inTab(0, *commandsButton_);

    // World: two 150-wide columns 10 apart; the seed box is labeled; then the switches.
    o = origin(310, 20 + 8 + 33 + 8 + 48);
    inTab(1, add<CycleButton>(o.x, o.y, 150, 20, t("selectWorld.mapType"),
                              std::vector<std::string>{t("generator.minecraft.normal"), t("generator.minecraft.flat"),
                                                       t("generator.minecraft.debug_all_block_states")},
                              worldType_, [this](int i) {
                                  worldType_ = i;
                                  refreshStates();
                              }));
    inTab(1, add<Button>(o.x + 160, o.y, 150, 20, t("selectWorld.customizeType"), nullptr)).active = false;
    inTab(1, add<Label>(o.x, o.y + 28, 308, t("selectWorld.enterSeed"), 0xFFFFFFFF, false));
    EditBox& seed = add<EditBox>(o.x, o.y + 41, 308, 20, t("selectWorld.seedInfo"));
    seed.setValue(seed_);
    seed.onChange = [this](const std::string& v) { seed_ = v; };
    inTab(1, seed);
    // SwitchGrid: label left, a 44-wide ON/OFF button right, rows 4 apart after an empty one.
    const char* switches[2] = {"selectWorld.mapFeatures", "selectWorld.bonusItems"};
    for (int i = 0; i < 2; ++i) {
        const float y = o.y + 69 + 4 + 24 * float(i);
        inTab(1, add<Label>(o.x, y + 5, 266, t(switches[i]), 0xFFFFFFFF, false));
        Widget& b =
            inTab(1, add<CycleButton>(o.x + 266, y, 44, 20, "", std::vector<std::string>{t("options.off")}, 0, nullptr));
        b.active = false; // no structures or bonus chests in this game
        if (i == 0) b.tooltip = t("selectWorld.mapFeatures.info");
    }

    // More: game rules, experiments and data packs (none of them exist here).
    o = origin(210, 20 * 3 + 8 * 2);
    const char* more[3] = {"selectWorld.gameRules", "selectWorld.experiments", "selectWorld.dataPacks"};
    for (int i = 0; i < 3; ++i)
        inTab(2, add<Button>(o.x, o.y + 28 * float(i), 210, 20, t(more[i]), nullptr)).active = false;

    // The footer: two default buttons 8 apart, centered in its 33 rows.
    const float fx = align(size.x - 308, 0.5f), fy = size.y - kFooterHeight + align(kFooterHeight - 20, 0.5f);
    add<Button>(fx, fy, 150, 20, t("selectWorld.create"), [this](Button&) { create(); });
    add<Button>(fx + 158, fy, 150, 20, t("gui.cancel"), [ctx](Button&) { ctx.host->popScreen(); });
    showTab(tab_);
    refreshStates();
    if (tab_ == 0) setFocus(nameBox_);
}

void CreateWorldScreen::refreshStates() {
    if (!gameModeButton_) return;
    gameModeButton_->setIndex(debug() ? 3 : gameMode_, false);
    gameModeButton_->active = !debug();
    static constexpr const char* kModeInfo[3] = {"selectWorld.gameMode.survival.info",
                                                 "selectWorld.gameMode.hardcore.info",
                                                 "selectWorld.gameMode.creative.info"};
    gameModeButton_->tooltip = debug() ? "" : t(kModeInfo[gameMode_]);
    const Difficulty shown = hardcore() ? Difficulty::Hard : difficulty_;
    difficultyButton_->setIndex(int(shown), false);
    difficultyButton_->active = !hardcore();
    difficultyButton_->tooltip = t(std::format("options.difficulty.{}.info", difficultyName(shown)));
    commandsButton_->setIndex(debug() ? 1 : hardcore() ? 0 : int(cheats_), false);
    commandsButton_->active = !debug() && !hardcore();
    const std::string name = name_.empty() ? t("selectWorld.newWorld") : name_;
    nameBox_->tooltip =
        ctx_.translate("selectWorld.targetFolder", {"§o" + uniqueWorldFolder(ctx_.host->savesDir(), name)});
}

void CreateWorldScreen::showTab(int tab) {
    tab_ = tab;
    for (int i = 0; i < 3; ++i) {
        tabs_[i]->selected = i == tab;
        for (Widget* w : tabWidgets_[i]) w->visible = i == tab;
    }
    if (focused_ && !focused_->visible) setFocus(nullptr);
}

bool CreateWorldScreen::keyPressed(int key, int scancode, int mods) {
    // TabNavigationBar: Ctrl+Tab (with Shift backwards) and Ctrl+1..3 switch tabs.
    if (mods & GLFW_MOD_CONTROL) {
        if (key == GLFW_KEY_TAB) {
            showTab((tab_ + ((mods & GLFW_MOD_SHIFT) ? 2 : 1)) % 3);
            return true;
        }
        if (key >= GLFW_KEY_1 && key <= GLFW_KEY_3) {
            showTab(key - GLFW_KEY_1);
            return true;
        }
    }
    if (WidgetScreen::keyPressed(key, scancode, mods)) return true;
    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
        create();
        return true;
    }
    return false;
}

void CreateWorldScreen::create() {
    WorldParams params;
    params.name = name_.empty() ? t("selectWorld.newWorld") : name_;
    params.folder = uniqueWorldFolder(ctx_.host->savesDir(), params.name);
    params.type = worldType_ == 1 ? "flat" : worldType_ == 2 ? "debug" : "default";
    std::random_device rd;
    params.seed = seedFromString(seed_, (uint64_t(rd()) << 32 | rd()) ^ uint64_t(std::time(nullptr)));
    params.gameMode = debug() ? "spectator" : gameMode_ == 2 ? "creative" : "survival";
    params.hardcore = hardcore() && !debug();
    params.difficulty = hardcore() ? Difficulty::Hard : difficulty_;
    params.cheats = debug() || (!hardcore() && cheats_);
    ctx_.host->startWorld(params);
}

void CreateWorldScreen::renderBackground(GuiGraphics& g) {
    // The bar's own background, the menu background under it, over the blur.
    g.blurBehind(ctx_.host->options().menuBlur);
    g.texture("minecraft:gui/tab_header_background", 0, 0, 0, 0, size_.x, kTabBarHeight, 16, 16);
    renderMenuBackground(g, 0, kTabBarHeight, size_.x, size_.y);
}

void CreateWorldScreen::renderContents(GuiGraphics& g, glm::vec2, float) {
    // Header separators either side of the tabs (TabNavigationBar.render) and the footer's.
    const char* header = inWorld() ? "minecraft:gui/inworld_header_separator" : "minecraft:gui/header_separator";
    const char* footer = inWorld() ? "minecraft:gui/inworld_footer_separator" : "minecraft:gui/footer_separator";
    g.texture(header, 0, kTabBarHeight - 2, 0, 0, tabs_[0]->x, 2, 32, 2);
    const float right = tabs_[2]->x + tabs_[2]->width;
    g.texture(header, right, kTabBarHeight - 2, 0, 0, size_.x - right, 2, 32, 2);
    g.texture(footer, 0, size_.y - kFooterHeight - 2, 0, 0, size_.x, 2, 32, 2);
}

// --- Confirm ---

ConfirmScreen::ConfirmScreen(ScreenContext ctx, std::string title, std::string message, std::string yes,
                             std::string no, std::function<void(bool)> answer)
    : WidgetScreen(ctx, std::move(title)), message_(std::move(message)), yes_(std::move(yes)), no_(std::move(no)),
      answer_(std::move(answer)) {}

float ConfirmScreen::titleTop() const {
    const int messageHeight = int(lines_.size()) * 9;
    return std::clamp(float((int(size_.y) - messageHeight) / 2 - 20 - 9), 10.0f, 80.0f);
}

void ConfirmScreen::init(glm::vec2 size) {
    WidgetScreen::init(size);
    clearWidgets();
    // MultiLineLabel: the message wrapped at the screen width less 50; the buttons 20 under
    // it, but at least a sixth of the screen + 96 down.
    lines_ = ctx_.font ? ctx_.font->split(message_, size.x - 50) : std::vector<std::string>{message_};
    const float cx = std::floor(size.x / 2);
    const float y = std::clamp(titleTop() + 20 + 9.0f * float(lines_.size()) + 20, std::floor(size.y / 6) + 96,
                               size.y - 24);
    auto answer = answer_;
    add<Button>(cx - 155, y, 150, 20, yes_, [answer](Button&) { answer(true); });
    add<Button>(cx + 5, y, 150, 20, no_, [answer](Button&) { answer(false); });
}

bool ConfirmScreen::keyPressed(int key, int scancode, int mods) {
    if (key == GLFW_KEY_ESCAPE) {
        answer_(false);
        return true;
    }
    return WidgetScreen::keyPressed(key, scancode, mods);
}

void ConfirmScreen::renderContents(GuiGraphics& g, glm::vec2, float) {
    const float cx = std::floor(size_.x / 2), top = titleTop();
    g.centeredText(title_, cx, top, 0xFFFFFFFF);
    for (size_t i = 0; i < lines_.size(); ++i) g.centeredText(lines_[i], cx, top + 20 + 9 * float(i), 0xFFFFFFFF);
}

// --- Loading ---

LoadingScreen::LoadingScreen(ScreenContext ctx, std::string message)
    : WidgetScreen(ctx, ""), message_(std::move(message)) {}

void LoadingScreen::renderContents(GuiGraphics& g, glm::vec2, float) {
    const float cx = std::floor(size_.x / 2), cy = std::floor(size_.y / 2);
    if (!message_.empty()) {
        g.centeredText(message_, cx, cy - 4, 0xFFFFFFFF);
        return;
    }
    const WorldLoadStatus status = ctx_.host->worldLoadStatus();
    // LevelLoadingScreen.renderChunks: 2 pixels per chunk, colored by stage (vanilla's
    // ChunkStatus colors for empty, noise, features, initialize_light, light and full).
    static constexpr uint32_t kColors[6] = {0xFF545454, 0xFFD1D1D1, 0xFF21C600, 0xFFCCCCCC, 0xFFFFE0A0, 0xFFFFFFFF};
    const int d = status.diameter;
    const float x0 = cx - float(d * 2 / 2), y0 = cy - float(d * 2 / 2);
    for (int z = 0; z < d; ++z)
        for (int x = 0; x < d; ++x) {
            const int stage = std::min<int>(status.stages[size_t(z * d + x)], 5);
            g.fill(x0 + float(x * 2), y0 + float(z * 2), x0 + float(x * 2 + 2), y0 + float(z * 2 + 2), kColors[stage]);
        }
    const int percent = std::clamp(int(status.progress * 100.0f), 0, 100);
    g.centeredText(std::to_string(percent) + "%", cx, cy - 4 - 30, 0xFFFFFFFF);
}

} // namespace mc
