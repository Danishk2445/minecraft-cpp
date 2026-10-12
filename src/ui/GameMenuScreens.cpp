#include "ui/GameMenuScreens.h"

#include <algorithm>
#include <cmath>

#include <GLFW/glfw3.h>

#include "game/Options.h"
#include "ui/Chat.h"
#include "ui/GuiGraphics.h"

namespace mc {
namespace {

std::string percent(double v) { return std::to_string(int(std::lround(v * 100.0))) + "%"; }

} // namespace

// --- Pause ---

PauseScreen::PauseScreen(ScreenContext ctx) : WidgetScreen(ctx, ctx.translate("menu.game")) {}

void PauseScreen::init(glm::vec2 size) {
    WidgetScreen::init(size);
    clearWidgets();
    // Vanilla createPauseMenu: a grid of 204- and 98-wide buttons, a quarter of the way down.
    const float gridHeight = 70 + 24 * 3;
    const float x = std::floor((size.x - 212) / 2) + 4, y = std::floor((size.y - gridHeight) * 0.25f) + 50;
    ScreenHost* host = ctx_.host;
    add<Button>(x, y, 204, 20, t("menu.returnToGame"), [host](Button&) { host->setScreen(nullptr); });
    add<Button>(x, y + 24, 98, 20, t("gui.advancements"), nullptr).active = false;
    add<Button>(x + 106, y + 24, 98, 20, t("gui.stats"), nullptr).active = false;
    ScreenContext ctx = ctx_;
    add<Button>(x, y + 48, 98, 20, t("menu.options"),
                [ctx](Button&) { ctx.host->pushScreen(std::make_unique<OptionsScreen>(ctx)); });
    add<Button>(x + 106, y + 48, 98, 20, t("menu.shareToLan"), nullptr).active = false;
    add<Button>(x, y + 72, 204, 20, t("menu.returnToMenu"), [host](Button&) { host->leaveWorld(); });
}

void PauseScreen::renderContents(GuiGraphics& g, glm::vec2, float) {
    g.centeredText(title_, std::floor(size_.x / 2), 40, 0xFFFFFFFF);
}

// --- Options ---

OptionsScreen::OptionsScreen(ScreenContext ctx) : WidgetScreen(ctx, ctx.translate("options.title")) {}

void OptionsScreen::init(glm::vec2 size) {
    WidgetScreen::init(size);
    clearWidgets();
    ScreenHost* host = ctx_.host;
    Options& o = host->options();
    const float cx = std::floor(size.x / 2);
    // Header (61 tall): the title, then FOV and the difficulty (vanilla OptionsScreen).
    const ScreenContext ctx = ctx_;
    add<Slider>(cx - 154, 29, 150, 20, (o.fov - 30) / 80.0,
                [ctx](double v) {
                    const int fov = 30 + int(std::lround(v * 80.0));
                    const std::string value = fov == 70    ? ctx.translate("options.fov.min")
                                              : fov == 110 ? ctx.translate("options.fov.max")
                                                           : std::to_string(fov);
                    return ctx.translate("options.generic_value", {ctx.translate("options.fov"), value});
                },
                [host](double v) {
                    host->options().fov = 30 + int(std::lround(v * 80.0));
                    host->optionsChanged();
                });
    if (inWorld()) {
        std::vector<std::string> names;
        for (const char* d : {"peaceful", "easy", "normal", "hard"}) names.push_back(t(std::string("options.difficulty.") + d));
        add<CycleButton>(cx + 4, 29, 150, 20, t("options.difficulty"), names, int(host->difficulty()),
                         [host](int i) { host->setDifficulty(Difficulty(i)); });
    } else {
        add<Button>(cx + 4, 29, 150, 20, t("options.online"), nullptr).active = false;
    }
    // Contents: two columns of 150-wide buttons, centered between header and footer.
    struct Entry {
        const char* key;
        std::function<std::unique_ptr<Screen>()> open;
    };
    const Entry entries[] = {
        {"options.skinCustomisation", nullptr},
        {"options.sounds", nullptr},
        {"options.video", [ctx] { return std::make_unique<VideoSettingsScreen>(ctx); }},
        {"options.controls", [ctx] { return std::make_unique<ControlsScreen>(ctx); }},
        {"options.language", nullptr},
        {"options.chat", [ctx] { return std::make_unique<ChatSettingsScreen>(ctx); }},
        {"options.resourcepack", nullptr},
        {"options.accessibility", nullptr},
        {"options.telemetry", nullptr},
        {"options.credits_and_attribution", nullptr},
    };
    const float rows = 5, contentTop = 61, contentBottom = size.y - 33;
    const float top = std::floor(contentTop + (contentBottom - contentTop - rows * 24) / 2);
    for (size_t i = 0; i < std::size(entries); ++i) {
        const float x = cx - 154 + float(i % 2) * 158, y = top + float(i / 2) * 24;
        auto open = entries[i].open;
        Button& b = add<Button>(x, y, 150, 20, t(entries[i].key), [host, open](Button&) {
            if (open) host->pushScreen(open());
        });
        b.active = bool(open);
    }
    add<Button>(cx - 100, size.y - 27, 200, 20, t("gui.done"), [host](Button&) { host->popScreen(); });
}

void OptionsScreen::renderContents(GuiGraphics& g, glm::vec2, float) {
    g.centeredText(title_, std::floor(size_.x / 2), 12, 0xFFFFFFFF);
}

// --- Option sub-screens ---

void OptionsSubScreen::init(glm::vec2 size) {
    WidgetScreen::init(size);
    clearWidgets();
    row_ = column_ = 0;
    addOptions();
    ScreenHost* host = ctx_.host;
    add<Button>(std::floor(size.x / 2) - 100, size.y - 27, 200, 20, t("gui.done"), [host](Button&) { host->popScreen(); });
}

void OptionsSubScreen::renderContents(GuiGraphics& g, glm::vec2, float) {
    renderSeparators(g, 33, 33);
    g.centeredText(title_, std::floor(size_.x / 2), 12, 0xFFFFFFFF);
}

VideoSettingsScreen::VideoSettingsScreen(ScreenContext ctx) : OptionsSubScreen(ctx, ctx.translate("options.videoTitle")) {}

void VideoSettingsScreen::addOptions() {
    Options& o = opts();
    const ScreenContext ctx = ctx_;
    ScreenHost* host = ctx_.host;
    auto label = [ctx](const char* key, const std::string& value) {
        return ctx.translate("options.generic_value", {ctx.translate(key), value});
    };
    option<Slider>((o.renderDistance - 2) / 30.0,
                   [label, ctx](double v) {
                       return label("options.renderDistance",
                                    ctx.translate("options.chunks", {std::to_string(2 + int(std::lround(v * 30.0)))}));
                   },
                   [host](double v) { host->options().renderDistance = 2 + int(std::lround(v * 30.0)); host->optionsChanged(); });
    option<Slider>((o.maxFps - 10) / 250.0,
                   [label, ctx](double v) {
                       const int fps = 10 + int(std::lround(v * 25.0)) * 10;
                       return label("options.framerateLimit", fps >= 260 ? ctx.translate("options.framerateLimit.max")
                                                                         : ctx.translate("options.framerate", {std::to_string(fps)}));
                   },
                   [host](double v) { host->options().maxFps = 10 + int(std::lround(v * 25.0)) * 10; host->optionsChanged(); });
    option<CycleButton>(t("options.vsync"), std::vector<std::string>{onOff(false), onOff(true)}, int(o.vsync),
                        [host](int i) { host->options().vsync = i == 1; host->optionsChanged(); });
    option<CycleButton>(t("options.fullscreen"), std::vector<std::string>{onOff(false), onOff(true)}, int(o.fullscreen),
                        [host](int i) { host->options().fullscreen = i == 1; host->optionsChanged(); });
    std::vector<std::string> scales = {t("options.guiScale.auto")};
    for (int s = 1; s <= 6; ++s) scales.push_back(std::to_string(s));
    option<CycleButton>(t("options.guiScale"), scales, std::clamp(o.guiScale, 0, 6), [host](int i) {
        host->options().guiScale = i;
        host->optionsChanged();
    });
    option<Slider>(o.gamma,
                   [label, ctx](double v) {
                       const int p = int(std::lround(v * 100.0));
                       return label("options.gamma", p == 0    ? ctx.translate("options.gamma.min")
                                                     : p == 50  ? ctx.translate("options.gamma.default")
                                                     : p == 100 ? ctx.translate("options.gamma.max")
                                                                : std::to_string(p));
                   },
                   [host](double v) { host->options().gamma = float(v); host->optionsChanged(); });
    option<CycleButton>(t("options.ao"), std::vector<std::string>{onOff(false), onOff(true)}, int(o.smoothLighting),
                        [host](int i) { host->options().smoothLighting = i == 1; host->optionsChanged(); });
    option<CycleButton>(t("options.renderClouds"),
                        std::vector<std::string>{onOff(false), t("options.clouds.fast"), t("options.clouds.fancy")},
                        int(o.clouds), [host](int i) { host->options().clouds = CloudMode(i); host->optionsChanged(); });
    option<CycleButton>(t("options.particles"),
                        std::vector<std::string>{t("options.particles.all"), t("options.particles.decreased"),
                                                 t("options.particles.minimal")},
                        int(o.particles), [host](int i) { host->options().particles = ParticleMode(i); host->optionsChanged(); });
    option<CycleButton>(t("options.entityShadows"), std::vector<std::string>{onOff(false), onOff(true)},
                        int(o.entityShadows), [host](int i) { host->options().entityShadows = i == 1; host->optionsChanged(); });
    option<CycleButton>(t("options.viewBobbing"), std::vector<std::string>{onOff(false), onOff(true)}, int(o.viewBobbing),
                        [host](int i) { host->options().viewBobbing = i == 1; host->optionsChanged(); });
    // Vanilla genericValueOrOffLabel: OFF at 0.
    option<Slider>(o.menuBlur / 10.0,
                   [label, ctx](double v) {
                       const int n = int(std::lround(v * 10.0));
                       return label("options.accessibility.menu_background_blurriness",
                                    n == 0 ? ctx.translate("options.off") : std::to_string(n));
                   },
                   [host](double v) { host->options().menuBlur = int(std::lround(v * 10.0)); host->optionsChanged(); });
}

ControlsScreen::ControlsScreen(ScreenContext ctx) : OptionsSubScreen(ctx, ctx.translate("controls.title")) {}

void ControlsScreen::addOptions() {
    const ScreenContext ctx = ctx_;
    option<Button>(t("options.mouse_settings"),
                   [ctx](Button&) { ctx.host->pushScreen(std::make_unique<MouseSettingsScreen>(ctx)); });
    option<Button>(t("controls.keybinds"), nullptr).active = false; // key rebinding: M13
}

MouseSettingsScreen::MouseSettingsScreen(ScreenContext ctx)
    : OptionsSubScreen(ctx, ctx.translate("options.mouse_settings.title")) {}

void MouseSettingsScreen::addOptions() {
    const ScreenContext ctx = ctx_;
    ScreenHost* host = ctx_.host;
    option<Slider>(opts().mouseSensitivity,
                   [ctx](double v) {
                       const std::string value = v <= 0.0   ? ctx.translate("options.sensitivity.min")
                                                 : v >= 1.0 ? ctx.translate("options.sensitivity.max")
                                                            : std::to_string(int(std::lround(v * 200.0))) + "%";
                       return ctx.translate("options.generic_value", {ctx.translate("options.sensitivity"), value});
                   },
                   [host](double v) { host->options().mouseSensitivity = float(v); host->optionsChanged(); });
    option<CycleButton>(t("options.invertMouse"), std::vector<std::string>{onOff(false), onOff(true)},
                        int(opts().invertMouse), [host](int i) { host->options().invertMouse = i == 1; host->optionsChanged(); });
}

ChatSettingsScreen::ChatSettingsScreen(ScreenContext ctx) : OptionsSubScreen(ctx, ctx.translate("options.chat.title")) {}

void ChatSettingsScreen::addOptions() {
    const ScreenContext ctx = ctx_;
    ScreenHost* host = ctx_.host;
    option<CycleButton>(t("options.chat.visibility"),
                        std::vector<std::string>{t("options.chat.visibility.full"), t("options.chat.visibility.system"),
                                                 t("options.chat.visibility.hidden")},
                        int(opts().chatVisibility),
                        [host](int i) { host->options().chatVisibility = ChatVisibility(i); host->optionsChanged(); });
    auto pct = [ctx](const char* key) {
        return [ctx, key](double v) { return ctx.translate("options.generic_value", {ctx.translate(key), percent(v)}); };
    };
    option<Slider>((opts().chatOpacity - 0.1) / 0.9, [ctx](double v) {
        return ctx.translate("options.generic_value", {ctx.translate("options.chat.opacity"), percent(v * 0.9 + 0.1)});
    }, [host](double v) { host->options().chatOpacity = float(v * 0.9 + 0.1); host->optionsChanged(); });
    option<Slider>(opts().textBackgroundOpacity, pct("options.accessibility.text_background_opacity"),
                   [host](double v) { host->options().textBackgroundOpacity = float(v); host->optionsChanged(); });
    option<Slider>(opts().chatScale, pct("options.chat.scale"),
                   [host](double v) { host->options().chatScale = float(v); host->optionsChanged(); });
}

// --- Chat ---

ChatScreen::ChatScreen(ScreenContext ctx, Chat& chat, std::string initial)
    : WidgetScreen(ctx, ctx.translate("chat_screen.title")), chat_(chat), initial_(std::move(initial)) {}

void ChatScreen::init(glm::vec2 size) {
    WidgetScreen::init(size);
    const std::string keep = input_ ? input_->value() : initial_;
    clearWidgets();
    // Vanilla ChatScreen: an unbordered box along the bottom, 256 characters at most.
    EditBox& box = add<EditBox>(4, size.y - 12, size.x - 4, 12);
    box.bordered = false;
    box.maxLength = 256;
    box.setValue(keep);
    ScreenHost* host = ctx_.host;
    Chat* chat = &chat_;
    box.onSubmit = [this, host, chat] {
        const std::string line = input_->value();
        chat->resetScroll();
        host->setScreen(nullptr);
        host->sendChat(line);
    };
    input_ = &box;
    setFocus(&box);
}

void ChatScreen::renderBackground(GuiGraphics& g) {
    g.fill(2, size_.y - 14, size_.x - 2, size_.y - 2, 0x80000000);
}

bool ChatScreen::keyPressed(int key, int scancode, int mods) {
    const auto& sent = chat_.sent();
    if (key == GLFW_KEY_UP || key == GLFW_KEY_DOWN) {
        // Walk the sent lines; past the newest comes back to what was being typed.
        if (historyIndex_ < 0) draft_ = input_->value();
        historyIndex_ += key == GLFW_KEY_UP ? 1 : -1;
        historyIndex_ = std::clamp(historyIndex_, -1, int(sent.size()) - 1);
        input_->setValue(historyIndex_ < 0 ? draft_ : sent[sent.size() - 1 - size_t(historyIndex_)]);
        return true;
    }
    if (key == GLFW_KEY_PAGE_UP || key == GLFW_KEY_PAGE_DOWN) {
        chat_.scroll(key == GLFW_KEY_PAGE_UP ? 19 : -19);
        return true;
    }
    return WidgetScreen::keyPressed(key, scancode, mods);
}

bool ChatScreen::mouseScrolled(glm::vec2, double amount) {
    chat_.scroll(amount > 0 ? 1 : -1);
    return true;
}

} // namespace mc
