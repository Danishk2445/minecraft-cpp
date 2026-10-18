#include "ui/GameMenuScreens.h"

#include <algorithm>
#include <cctype>
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
    listed_.clear();
    row_ = column_ = 0;
    addOptions();
    ScreenHost* host = ctx_.host;
    add<Button>(std::floor(size.x / 2) - 100, size.y - 27, 200, 20, t("gui.done"), [host](Button&) { host->popScreen(); });
    layoutList();
}

float OptionsSubScreen::maxScroll() const {
    // AbstractScrollArea: the rows against the list's height less its 4-pixel padding.
    const int rows = row_ + (column_ > 0 ? 1 : 0);
    return std::max(0.0f, 25.0f * float(rows) - (listBottom() - 33 - 4));
}

void OptionsSubScreen::layoutList() {
    scroll_ = std::clamp(scroll_, 0.0f, maxScroll());
    for (auto& [w, y] : listed_) {
        w->y = y - std::floor(scroll_);
        w->visible = w->y + w->height > 33 && w->y < listBottom();
    }
}

bool OptionsSubScreen::mouseScrolled(glm::vec2, double amount) {
    scroll_ -= float(amount) * 25.0f / 2.0f; // half a row a notch
    layoutList();
    return true;
}

std::optional<glm::vec4> OptionsSubScreen::clipOf(const Widget& w) const {
    for (const auto& [listed, y] : listed_)
        if (listed == &w) return glm::vec4(0.0f, 33.0f, size_.x, listBottom());
    return std::nullopt;
}

void OptionsSubScreen::renderContents(GuiGraphics& g, glm::vec2, float) {
    renderSeparators(g, 33, 33);
    g.centeredText(title_, std::floor(size_.x / 2), 12, 0xFFFFFFFF);
    if (maxScroll() > 0) {
        // AbstractScrollArea.renderScrollbar, right of the 310-wide rows.
        const float top = 33, view = listBottom() - top, content = float(row_ + (column_ > 0 ? 1 : 0)) * 25.0f + 4;
        const float thumb = std::clamp(float(int(view * view / content)), 32.0f, view - 8);
        const float x = std::floor(size_.x / 2) - 155 + 310 + 10;
        const float y = std::max(top, float(int(scroll_) * int(view - thumb) / int(maxScroll())) + top);
        g.sprite("widget/scroller_background", x, top, 6, view);
        g.sprite("widget/scroller", x, y, 6, thumb);
    }
}

VideoSettingsScreen::VideoSettingsScreen(ScreenContext ctx) : OptionsSubScreen(ctx, ctx.translate("options.videoTitle")) {}

void VideoSettingsScreen::addOptions() {
    Options& o = opts();
    const ScreenContext ctx = ctx_;
    ScreenHost* host = ctx_.host;
    auto label = [ctx](const char* key, const std::string& value) {
        return ctx.translate("options.generic_value", {ctx.translate(key), value});
    };
    option<CycleButton>(t("options.graphics"),
                        std::vector<std::string>{t("options.graphics.fast"), t("options.graphics.fancy"),
                                                 t("options.graphics.fabulous")},
                        int(o.graphics), [host](int i) { host->options().graphics = GraphicsMode(i); host->optionsChanged(); });
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
    option<CycleButton>(t("options.attackIndicator"),
                        std::vector<std::string>{t("options.off"), t("options.attack.crosshair"), t("options.attack.hotbar")},
                        int(o.attackIndicator), [host](int i) {
                            host->options().attackIndicator = AttackIndicator(i);
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
    // Vanilla mipmapLevels: OFF at 0.
    option<Slider>(o.mipmapLevels / 4.0,
                   [label, ctx](double v) {
                       const int n = int(std::lround(v * 4.0));
                       return label("options.mipmapLevels", n == 0 ? ctx.translate("options.off") : std::to_string(n));
                   },
                   [host](double v) { host->options().mipmapLevels = int(std::lround(v * 4.0)); host->optionsChanged(); });
    option<CycleButton>(t("options.entityShadows"), std::vector<std::string>{onOff(false), onOff(true)},
                        int(o.entityShadows), [host](int i) { host->options().entityShadows = i == 1; host->optionsChanged(); });
    // Vanilla entityDistanceScaling: 50% to 500% in steps of 25.
    option<Slider>((o.entityDistance - 0.5) / 4.5,
                   [ctx](double v) {
                       const int percent = 50 + int(std::lround(v * 18.0)) * 25;
                       return ctx.translate("options.percent_value",
                                            {ctx.translate("options.entityDistanceScaling"), std::to_string(percent)});
                   },
                   [host](double v) {
                       host->options().entityDistance = 0.5f + float(std::lround(v * 18.0)) * 0.25f;
                       host->optionsChanged();
                   });
    // Vanilla biomeBlendRadius: OFF (Fastest) .. 15x15 (Maximum).
    option<Slider>(o.biomeBlend / 7.0,
                   [label, ctx](double v) {
                       const int r = int(std::lround(v * 7.0));
                       return label("options.biomeBlendRadius",
                                    ctx.translate("options.biomeBlendRadius." + std::to_string(r * 2 + 1)));
                   },
                   [host](double v) { host->options().biomeBlend = int(std::lround(v * 7.0)); host->optionsChanged(); });
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
    option<Button>(t("controls.keybinds"),
                   [ctx](Button&) { ctx.host->pushScreen(std::make_unique<KeyBindsScreen>(ctx)); });
}

// --- Key binds ---

KeyBindsScreen::KeyBindsScreen(ScreenContext ctx) : WidgetScreen(ctx, ctx.translate("controls.keybinds.title")) {}

KeyBindings& KeyBindsScreen::keys() const { return ctx_.host->options().keys; }

std::string KeyBindsScreen::keyText(const InputKey& key) const {
    const std::string name = key.name();
    if (key.type == InputKey::Type::Mouse) {
        const std::string text = t(name);
        return text != name ? text : ctx_.translate("key.mouse", {std::to_string(key.code + 1)});
    }
    // Letters, digits and punctuation by the keyboard layout (vanilla glfwGetKeyName), upper case.
    if (key.bound())
        if (const char* local = glfwGetKeyName(key.code, 0); local && *local) {
            std::string text = local;
            for (char& c : text) c = char(std::toupper(static_cast<unsigned char>(c)));
            return text;
        }
    const std::string text = t(name);
    if (text != name) return text;
    // No layout name (no display) and no translation: the name's last part.
    std::string tail = name.substr(name.rfind('.') + 1);
    for (char& c : tail) c = char(std::toupper(static_cast<unsigned char>(c)));
    return tail;
}

void KeyBindsScreen::init(glm::vec2 size) {
    WidgetScreen::init(size);
    clearWidgets();
    rows_.clear();
    // KeyBindsList: sorted by category, then by translated name; a title row starts each category.
    std::vector<KeyAction> order;
    for (size_t i = 0; i < kKeyActionCount; ++i) order.push_back(KeyAction(i));
    std::stable_sort(order.begin(), order.end(), [&](KeyAction a, KeyAction b) {
        const KeyMappingInfo &ia = keyMappingInfo(a), &ib = keyMappingInfo(b);
        const int ca = keyCategoryOrder(ia.category), cb = keyCategoryOrder(ib.category);
        return ca != cb ? ca < cb : t(ia.name) < t(ib.name);
    });
    std::string category;
    for (KeyAction a : order) {
        const KeyMappingInfo& info = keyMappingInfo(a);
        if (info.category != category) {
            category = info.category;
            rows_.push_back({t(category), std::nullopt, nullptr, nullptr});
        }
        Row row{t(info.name), a, nullptr, nullptr};
        row.change = std::make_unique<Button>(0, 0, 75, 20, "", [this, a](Button&) {
            selected_ = a;
            refresh();
        });
        row.reset = std::make_unique<Button>(0, 0, 50, 20, t("controls.reset"), [this, a](Button&) {
            keys()[a] = keyMappingInfo(a).defaultKey;
            refresh();
            ctx_.host->optionsChanged();
        });
        rows_.push_back(std::move(row));
    }
    const float cx = std::floor(size.x / 2);
    resetAll_ = &add<Button>(cx - 154, size.y - 27, 150, 20, t("controls.resetAll"), [this](Button&) {
        keys().resetAll();
        refresh();
        ctx_.host->optionsChanged();
    });
    add<Button>(cx + 4, size.y - 27, 150, 20, t("gui.done"), [this](Button&) { ctx_.host->popScreen(); });
    scroll_ = std::clamp(scroll_, 0.0f, maxScroll());
    refresh();
    layoutRows();
}

float KeyBindsScreen::maxScroll() const {
    return std::max(0.0f, float(rows_.size()) * kRowHeight - (listBottom() - kTop - 4));
}

void KeyBindsScreen::layoutRows() {
    // KeyEntry.render: Reset 10 left of the scrollbar, the key 5 left of that, both 2 above the row.
    const float rowLeft = std::floor(size_.x / 2) - kRowWidth / 2 + 2;
    const float resetX = rowLeft + kRowWidth + 8 - 50 - 10;
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (!rows_[i].change) continue;
        const float y = rowTop(i) - 2;
        rows_[i].reset->x = resetX;
        rows_[i].change->x = resetX - 5 - 75;
        rows_[i].reset->y = rows_[i].change->y = y;
    }
}

void KeyBindsScreen::refresh() {
    const KeyBindings& k = keys();
    for (Row& row : rows_) {
        if (!row.action) continue;
        const KeyAction a = *row.action;
        std::string text = keyText(k[a]);
        row.change->tooltip.clear();
        // KeyEntry.refreshEntry: what else uses the key, in red brackets with a tooltip.
        if (k.conflicts(a)) {
            std::string others;
            for (size_t i = 0; i < kKeyActionCount; ++i)
                if (KeyAction(i) != a && k.keys[i] == k[a]) {
                    if (!others.empty()) others += ", ";
                    others += t(keyMappingInfo(KeyAction(i)).name);
                }
            row.change->tooltip = ctx_.translate("controls.keybinds.duplicateKeybinds", {others});
            text = "§c[ §f" + text + "§c ]";
        }
        if (selected_ == a) text = "§e> §f§n" + text + "§r§e <";
        row.change->message = text;
        row.reset->active = !k.isDefault(a);
    }
    if (resetAll_) resetAll_->active = !k.allDefault();
}

void KeyBindsScreen::bind(const InputKey& key) {
    keys()[*selected_] = key;
    selected_.reset();
    refresh();
    ctx_.host->optionsChanged();
}

bool KeyBindsScreen::mouseClicked(glm::vec2 mouse, int button, int mods) {
    if (selected_) {
        bind(InputKey::mouse(button));
        return true;
    }
    if (inList(mouse))
        for (Row& row : rows_)
            if (row.change && (row.change->mouseClicked(mouse, button) || row.reset->mouseClicked(mouse, button)))
                return true;
    return WidgetScreen::mouseClicked(mouse, button, mods);
}

bool KeyBindsScreen::mouseScrolled(glm::vec2, double amount) {
    scroll_ = std::clamp(scroll_ - float(amount) * kRowHeight / 2, 0.0f, maxScroll());
    layoutRows();
    return true;
}

bool KeyBindsScreen::keyPressed(int key, int scancode, int mods) {
    if (selected_) {
        bind(key == GLFW_KEY_ESCAPE ? InputKey{} : InputKey::key(key));
        return true;
    }
    return WidgetScreen::keyPressed(key, scancode, mods);
}

void KeyBindsScreen::render(GuiGraphics& g, glm::vec2 mouse, float partialTick) {
    renderBackground(g);
    const float cx = std::floor(size_.x / 2), bottom = listBottom();
    g.texture(inWorld() ? "minecraft:gui/inworld_menu_list_background" : "minecraft:gui/menu_list_background", 0, kTop,
              size_.x, bottom + std::floor(scroll_), size_.x, bottom - kTop, 32, 32);
    renderSeparators(g, kTop, size_.y - bottom);
    // The rows, clipped to the list; a mouse outside it hovers nothing in it.
    const glm::vec2 listMouse = inList(mouse) ? mouse : glm::vec2(-1.0f);
    const float rowLeft = cx - kRowWidth / 2 + 2;
    g.enableScissor(0, kTop, size_.x, bottom);
    for (size_t i = 0; i < rows_.size(); ++i) {
        const float top = rowTop(i);
        if (top + kRowHeight < kTop || top - 2 > bottom) continue;
        const Row& row = rows_[i];
        if (!row.action) {
            // CategoryEntry: centered on the row's bottom line.
            g.text(row.label, cx - std::floor(float(g.textWidth(row.label)) / 2), top + 16 - 9 - 1, 0xFFFFFFFF);
            continue;
        }
        row.reset->render(g, listMouse, partialTick);
        row.change->render(g, listMouse, partialTick);
        g.text(row.label, rowLeft, top + 8 - 4, 0xFFFFFFFF);
        if (!row.change->tooltip.empty()) {
            const float x = row.change->x - 6;
            g.fill(x, top - 1, x + 3, top + 16, 0xFFFF0000);
        }
    }
    g.disableScissor();
    if (maxScroll() > 0) {
        const float view = bottom - kTop, content = float(rows_.size()) * kRowHeight + 4;
        const float thumb = std::clamp(float(int(view * view / content)), 32.0f, view - 8);
        const float x = rowLeft + kRowWidth + 6;
        const float y = std::max(kTop, float(int(scroll_) * int(view - thumb) / int(maxScroll())) + kTop);
        g.sprite("widget/scroller_background", x, kTop, 6, view);
        g.sprite("widget/scroller", x, y, 6, thumb);
    }
    g.centeredText(title_, cx, 12, 0xFFFFFFFF);
    for (auto& w : widgets_) w->render(g, mouse, partialTick);
    for (const Row& row : rows_)
        if (row.change && !row.change->tooltip.empty() && row.change->isMouseOver(listMouse))
            g.textTooltip(row.change->tooltip, mouse.x, mouse.y);
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

void SleepScreen::init(glm::vec2 size) {
    ChatScreen::init(size);
    const ScreenContext ctx = ctx_;
    add<Button>(std::floor(size.x / 2) - 100, size.y - 40, 200, 20, t("multiplayer.stopSleeping"),
                [ctx](Button&) { ctx.host->leaveBed(); });
}

bool SleepScreen::keyPressed(int key, int scancode, int mods) {
    if (key == GLFW_KEY_ESCAPE) {
        ctx_.host->leaveBed();
        return true;
    }
    return ChatScreen::keyPressed(key, scancode, mods);
}

} // namespace mc
