#include <gtest/gtest.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "game/Options.h"
#include "resources/FontSet.h"
#include "resources/ResourcePack.h"
#include "ui/GuiScale.h"

using namespace mc;

namespace {

const FontSet& defaultFont() {
    static const FontSet font = [] {
        FontSet f;
        const ResourcePack pack(MC_DEFAULT_PACK_DIR);
        f.load(pack);
        return f;
    }();
    return font;
}

} // namespace

// Vanilla glyph metrics: advance = (int)(0.5 + actualWidth * scale) + 1, space 4.
TEST(Font, GlyphWidths) {
    const FontSet& f = defaultFont();
    EXPECT_FLOAT_EQ(f.glyph(U'i').advance, 2.0f);
    EXPECT_FLOAT_EQ(f.glyph(U'l').advance, 3.0f);
    EXPECT_FLOAT_EQ(f.glyph(U' ').advance, 4.0f);
    EXPECT_FLOAT_EQ(f.glyph(U'A').advance, 6.0f);
    EXPECT_FLOAT_EQ(f.glyph(U'!').advance, 2.0f);
    EXPECT_FLOAT_EQ(f.width("Hello World"), 6 + 6 + 3 + 3 + 6 + 4 + 6 + 6 + 6 + 3 + 6);
    // ASCII glyphs sit with their top on the line; accented ones reach 3 pixels above it.
    EXPECT_FLOAT_EQ(f.glyph(U'A').up, 0.0f);
    ASSERT_TRUE(f.has(U'É')); // É
    EXPECT_FLOAT_EQ(f.glyph(U'É').up, -3.0f);
    EXPECT_TRUE(f.has(U'Ж')); // Ж (nonlatin_european)
    // Characters no provider covers (unifont is not in the pack) draw the missing box.
    EXPECT_FALSE(f.has(U'中'));
    EXPECT_FLOAT_EQ(f.glyph(U'中').advance, 6.0f);
}

TEST(Font, FormattingCodes) {
    const FontSet& f = defaultFont();
    // Colors don't change the width; bold adds a pixel per character until a color resets it.
    EXPECT_FLOAT_EQ(f.width("§cHi"), f.width("Hi"));
    EXPECT_FLOAT_EQ(f.width("§lHi"), f.width("Hi") + 2.0f);
    EXPECT_FLOAT_EQ(f.width("§lH§ai"), f.width("Hi") + 1.0f);
    EXPECT_FLOAT_EQ(f.width("§lH§ri"), f.width("Hi") + 1.0f);
    EXPECT_FLOAT_EQ(f.width("Hi§"), f.width("Hi")); // a trailing § is dropped
    EXPECT_FLOAT_EQ(f.width("§zHi"), f.width("Hi")); // unknown codes vanish too
    EXPECT_EQ(stripFormatting("§aHello§r §lW§korld"), "Hello World");

    TextStyle style;
    FontSet::applyFormat(style, TextStyle{}, U'l');
    FontSet::applyFormat(style, TextStyle{}, U'o');
    EXPECT_TRUE(style.bold && style.italic);
    FontSet::applyFormat(style, TextStyle{}, U'E');
    EXPECT_FALSE(style.bold || style.italic);
    EXPECT_TRUE(style.hasColor);
    EXPECT_EQ(style.color, 0xFFFF55u);
    // Obfuscated characters swap for glyphs of the same width.
    for (uint32_t r = 0; r < 20; ++r) EXPECT_FLOAT_EQ(f.glyph(f.obfuscated(U'A', r)).advance, 6.0f);
}

TEST(Font, SplitWrapsAndBreaksLines) {
    const FontSet& f = defaultFont();
    // Wraps at the last space that fits; a newline always breaks, keeping the formatting.
    EXPECT_EQ(f.split("Hello World", 40.0f), (std::vector<std::string>{"Hello", "World"}));
    EXPECT_EQ(f.split("Hello World", 400.0f), (std::vector<std::string>{"Hello World"}));
    EXPECT_EQ(f.split("§cThis key:\nJump", 400.0f), (std::vector<std::string>{"§cThis key:", "§cJump"}));
    EXPECT_EQ(f.split("a\n\nb", 400.0f), (std::vector<std::string>{"a", "", "b"}));
}

TEST(Font, Utf8RoundTrip) {
    const std::string text = "AéЖ中\U0001F600";
    const std::u32string cps = decodeUtf8(text);
    ASSERT_EQ(cps.size(), 5u);
    EXPECT_EQ(cps[4], U'\U0001F600');
    EXPECT_EQ(encodeUtf8(cps), text);
    EXPECT_EQ(decodeUtf8("\xFF" "a"), std::u32string(U"�a"));
}

// Vanilla Window.calculateScale.
TEST(GuiScale, AutoScale) {
    EXPECT_EQ(autoGuiScale({1920, 1080}), 4);
    EXPECT_EQ(autoGuiScale({1280, 720}), 3);
    EXPECT_EQ(autoGuiScale({2560, 1440}), 6);
    EXPECT_EQ(autoGuiScale({854, 480}), 2);
    EXPECT_EQ(autoGuiScale({320, 240}), 1);
    EXPECT_EQ(autoGuiScale({1920, 1080}, 2), 2);
}

// Options survive a save and load; bad values are clamped, unknown keys ignored.
TEST(Options, RoundTripAndClamping) {
    Options o;
    o.fov = 90;
    o.renderDistance = 20;
    o.guiScale = 3;
    o.maxFps = 260;
    o.vsync = false;
    o.gamma = 1.0f;
    o.clouds = CloudMode::Fast;
    o.particles = ParticleMode::Minimal;
    o.mouseSensitivity = 0.25f;
    o.invertMouse = true;
    o.chatVisibility = ChatVisibility::Commands;
    o.menuBlur = 0;
    o.attackIndicator = AttackIndicator::Hotbar;
    o.graphics = GraphicsMode::Fast;
    o.biomeBlend = 7;
    o.mipmapLevels = 0;
    o.entityDistance = 2.5f;
    const Options back = Options::fromJson(o.toJson());
    EXPECT_EQ(back.toJson(), o.toJson());
    EXPECT_TRUE(back.unlimitedFps());
    std::string err;
    const Options clamped =
        Options::fromJson(R"({"fov": 500, "renderDistance": 1, "gamma": -3, "clouds": 9, "menuBlur": 99, "attackIndicator": 7, "extra": 1})", &err);
    EXPECT_EQ(err, "");
    EXPECT_EQ(back.attackIndicator, AttackIndicator::Hotbar);
    EXPECT_EQ(back.graphics, GraphicsMode::Fast);
    EXPECT_FALSE(back.fancyGraphics());
    EXPECT_FLOAT_EQ(back.entityDistance, 2.5f);
    const Options wild = Options::fromJson(R"({"graphics": 5, "biomeBlend": 30, "mipmapLevels": -2, "entityDistance": 9})");
    EXPECT_EQ(wild.graphics, GraphicsMode::Fancy);
    EXPECT_EQ(wild.biomeBlend, 7);
    EXPECT_EQ(wild.mipmapLevels, 0);
    EXPECT_FLOAT_EQ(wild.entityDistance, 5.0f);
    EXPECT_EQ(clamped.attackIndicator, AttackIndicator::Crosshair);
    EXPECT_EQ(clamped.menuBlur, 10);
    EXPECT_EQ(clamped.fov, 110);
    EXPECT_EQ(clamped.renderDistance, 2);
    EXPECT_FLOAT_EQ(clamped.gamma, 0.0f);
    EXPECT_EQ(clamped.clouds, CloudMode::Fancy);
    Options::fromJson("not json", &err);
    EXPECT_NE(err, "");
    // Vanilla mouse sensitivity: 0.5 turns 0.15 degrees per count.
    EXPECT_NEAR(Options{}.mouseDegreesPerCount(), 0.15, 1e-9);
}

TEST(KeyBindings, VanillaNamesAndGlfwCodes) {
    // Vanilla InputConstants names for GLFW's codes.
    const std::pair<int, const char*> keys[] = {
        {GLFW_KEY_W, "key.keyboard.w"},          {GLFW_KEY_0, "key.keyboard.0"},
        {GLFW_KEY_SPACE, "key.keyboard.space"},  {GLFW_KEY_LEFT_SHIFT, "key.keyboard.left.shift"},
        {GLFW_KEY_F11, "key.keyboard.f11"},      {GLFW_KEY_KP_7, "key.keyboard.keypad.7"},
        {GLFW_KEY_SLASH, "key.keyboard.slash"},  {GLFW_KEY_RIGHT_SUPER, "key.keyboard.right.win"},
        {GLFW_KEY_GRAVE_ACCENT, "key.keyboard.grave.accent"}, {GLFW_KEY_KP_ENTER, "key.keyboard.keypad.enter"},
        {GLFW_KEY_PAGE_DOWN, "key.keyboard.page.down"}, {GLFW_KEY_MENU, "key.keyboard.menu"},
    };
    for (const auto& [code, name] : keys) {
        EXPECT_EQ(InputKey::key(code).name(), name);
        EXPECT_EQ(InputKey::parse(name), InputKey::key(code)) << name;
    }
    EXPECT_EQ(InputKey::mouse(GLFW_MOUSE_BUTTON_LEFT).name(), "key.mouse.left");
    EXPECT_EQ(InputKey::mouse(GLFW_MOUSE_BUTTON_MIDDLE).name(), "key.mouse.middle");
    EXPECT_EQ(InputKey::mouse(GLFW_MOUSE_BUTTON_4).name(), "key.mouse.4");
    EXPECT_EQ(InputKey::parse("key.mouse.5"), InputKey::mouse(GLFW_MOUSE_BUTTON_5));
    EXPECT_EQ(InputKey{}.name(), "key.keyboard.unknown");
    EXPECT_EQ(InputKey::parse("key.keyboard.unknown"), InputKey{});
    EXPECT_FALSE(InputKey::parse("key.keyboard.nonsense").has_value());
    EXPECT_FALSE(InputKey::parse("w").has_value());
    // Every named code round-trips.
    for (int code = 0; code < 512; ++code) {
        const std::string n = InputKey::key(code).name();
        if (n != "key.keyboard.unknown") {
            EXPECT_EQ(InputKey::parse(n), InputKey::key(code)) << n;
        }
    }

    // Vanilla's defaults.
    const KeyBindings k;
    EXPECT_EQ(k[KeyAction::Forward], InputKey::key(GLFW_KEY_W));
    EXPECT_EQ(k[KeyAction::Sneak], InputKey::key(GLFW_KEY_LEFT_SHIFT));
    EXPECT_EQ(k[KeyAction::Sprint], InputKey::key(GLFW_KEY_LEFT_CONTROL));
    EXPECT_EQ(k[KeyAction::Attack], InputKey::mouse(GLFW_MOUSE_BUTTON_LEFT));
    EXPECT_EQ(k[KeyAction::Use], InputKey::mouse(GLFW_MOUSE_BUTTON_RIGHT));
    EXPECT_EQ(k[KeyAction::PickItem], InputKey::mouse(GLFW_MOUSE_BUTTON_MIDDLE));
    EXPECT_EQ(k[KeyAction::Inventory], InputKey::key(GLFW_KEY_E));
    EXPECT_EQ(k[KeyAction::Command], InputKey::key(GLFW_KEY_SLASH));
    EXPECT_EQ(k[KeyAction::Screenshot], InputKey::key(GLFW_KEY_F2));
    EXPECT_EQ(k[KeyAction::TogglePerspective], InputKey::key(GLFW_KEY_F5));
    EXPECT_EQ(k[KeyAction::Fullscreen], InputKey::key(GLFW_KEY_F11));
    EXPECT_EQ(k[hotbarAction(8)], InputKey::key(GLFW_KEY_9));
    EXPECT_TRUE(k.allDefault());
    for (size_t i = 0; i < kKeyActionCount; ++i) EXPECT_FALSE(k.conflicts(KeyAction(i))) << keyMappingInfo(KeyAction(i)).name;
    EXPECT_LT(keyCategoryOrder("key.categories.movement"), keyCategoryOrder("key.categories.gameplay"));
    EXPECT_LT(keyCategoryOrder("key.categories.multiplayer"), keyCategoryOrder("key.categories.misc"));
}

TEST(KeyBindings, SavedInOptions) {
    Options o;
    o.keys[KeyAction::Forward] = InputKey::key(GLFW_KEY_UP);
    o.keys[KeyAction::Jump] = InputKey::mouse(GLFW_MOUSE_BUTTON_4);
    o.keys[KeyAction::Drop] = InputKey{};
    o.keys[KeyAction::Chat] = InputKey::key(GLFW_KEY_E); // shares the inventory's key
    EXPECT_TRUE(o.keys.conflicts(KeyAction::Chat));
    EXPECT_TRUE(o.keys.conflicts(KeyAction::Inventory));
    EXPECT_FALSE(o.keys.conflicts(KeyAction::Drop)); // unbound keys never conflict
    const std::string json = o.toJson();
    EXPECT_NE(json.find("\"key.forward\": \"key.keyboard.up\""), std::string::npos);
    const Options back = Options::fromJson(json);
    EXPECT_EQ(back.keys.keys, o.keys.keys);
    EXPECT_FALSE(back.keys.allDefault());
    // Unknown names keep the default; missing keys too.
    const Options partial = Options::fromJson(R"({"keys": {"key.jump": "key.keyboard.bogus", "key.sneak": "key.keyboard.z"}})");
    EXPECT_EQ(partial.keys[KeyAction::Jump], InputKey::key(GLFW_KEY_SPACE));
    EXPECT_EQ(partial.keys[KeyAction::Sneak], InputKey::key(GLFW_KEY_Z));
    EXPECT_EQ(partial.keys[KeyAction::Forward], InputKey::key(GLFW_KEY_W));
    Options reset = back;
    reset.keys.resetAll();
    EXPECT_TRUE(reset.keys.allDefault());
}
