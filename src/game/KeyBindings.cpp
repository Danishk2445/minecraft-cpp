#include "game/KeyBindings.h"

#include <algorithm>
#include <charconv>
#include <format>

namespace mc {
namespace {

// GLFW key codes (glfw3.h), which vanilla's InputConstants names follow.
struct KeyName {
    int code;
    const char* name;
};
constexpr KeyName kNamedKeys[] = {
    {32, "space"}, {39, "apostrophe"}, {44, "comma"}, {45, "minus"}, {46, "period"}, {47, "slash"},
    {59, "semicolon"}, {61, "equal"}, {91, "left.bracket"}, {92, "backslash"}, {93, "right.bracket"},
    {96, "grave.accent"}, {161, "world.1"}, {162, "world.2"}, {256, "escape"}, {257, "enter"}, {258, "tab"},
    {259, "backspace"}, {260, "insert"}, {261, "delete"}, {262, "right"}, {263, "left"}, {264, "down"},
    {265, "up"}, {266, "page.up"}, {267, "page.down"}, {268, "home"}, {269, "end"}, {280, "caps.lock"},
    {281, "scroll.lock"}, {282, "num.lock"}, {283, "print.screen"}, {284, "pause"}, {330, "keypad.decimal"},
    {331, "keypad.divide"}, {332, "keypad.multiply"}, {333, "keypad.subtract"}, {334, "keypad.add"},
    {335, "keypad.enter"}, {336, "keypad.equal"}, {340, "left.shift"}, {341, "left.control"}, {342, "left.alt"},
    {343, "left.win"}, {344, "right.shift"}, {345, "right.control"}, {346, "right.alt"}, {347, "right.win"},
    {348, "menu"},
};
constexpr int kKey0 = 48, kKeyA = 65, kKeyF1 = 290, kKeyKp0 = 320;

std::string keyboardName(int code) {
    if (code >= kKeyA && code < kKeyA + 26) return std::string(1, char('a' + code - kKeyA));
    if (code >= kKey0 && code < kKey0 + 10) return std::string(1, char('0' + code - kKey0));
    if (code >= kKeyF1 && code < kKeyF1 + 25) return std::format("f{}", code - kKeyF1 + 1);
    if (code >= kKeyKp0 && code < kKeyKp0 + 10) return std::format("keypad.{}", code - kKeyKp0);
    for (const KeyName& k : kNamedKeys)
        if (k.code == code) return k.name;
    return "";
}

std::optional<int> parseInt(std::string_view s) {
    int v = 0;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc() || end != s.data() + s.size()) return std::nullopt;
    return v;
}

constexpr const char* kMovement = "key.categories.movement";
constexpr const char* kGameplay = "key.categories.gameplay";
constexpr const char* kInventory = "key.categories.inventory";
constexpr const char* kMultiplayer = "key.categories.multiplayer";
constexpr const char* kMisc = "key.categories.misc";

constexpr KeyMappingInfo kMappings[kKeyActionCount] = {
    {"key.attack", kGameplay, InputKey::mouse(0)},
    {"key.use", kGameplay, InputKey::mouse(1)},
    {"key.forward", kMovement, InputKey::key(87)},       // W
    {"key.left", kMovement, InputKey::key(65)},          // A
    {"key.back", kMovement, InputKey::key(83)},          // S
    {"key.right", kMovement, InputKey::key(68)},         // D
    {"key.jump", kMovement, InputKey::key(32)},          // Space
    {"key.sneak", kMovement, InputKey::key(340)},        // Left Shift
    {"key.sprint", kMovement, InputKey::key(341)},       // Left Control
    {"key.drop", kInventory, InputKey::key(81)},         // Q
    {"key.inventory", kInventory, InputKey::key(69)},    // E
    {"key.chat", kMultiplayer, InputKey::key(84)},       // T
    {"key.command", kMultiplayer, InputKey::key(47)},    // /
    {"key.pickItem", kGameplay, InputKey::mouse(2)},
    {"key.screenshot", kMisc, InputKey::key(291)},        // F2
    {"key.togglePerspective", kMisc, InputKey::key(294)}, // F5
    {"key.fullscreen", kMisc, InputKey::key(300)},        // F11
    {"key.swapOffhand", kInventory, InputKey::key(70)},   // F
    {"key.hotbar.1", kInventory, InputKey::key(49)},
    {"key.hotbar.2", kInventory, InputKey::key(50)},
    {"key.hotbar.3", kInventory, InputKey::key(51)},
    {"key.hotbar.4", kInventory, InputKey::key(52)},
    {"key.hotbar.5", kInventory, InputKey::key(53)},
    {"key.hotbar.6", kInventory, InputKey::key(54)},
    {"key.hotbar.7", kInventory, InputKey::key(55)},
    {"key.hotbar.8", kInventory, InputKey::key(56)},
    {"key.hotbar.9", kInventory, InputKey::key(57)},
};

} // namespace

std::string InputKey::name() const {
    if (!bound()) return "key.keyboard.unknown";
    if (type == Type::Mouse) {
        switch (code) {
        case 0: return "key.mouse.left";
        case 1: return "key.mouse.right";
        case 2: return "key.mouse.middle";
        default: return std::format("key.mouse.{}", code + 1);
        }
    }
    const std::string n = keyboardName(code);
    return n.empty() ? "key.keyboard.unknown" : "key.keyboard." + n;
}

std::optional<InputKey> InputKey::parse(std::string_view name) {
    if (name == "key.keyboard.unknown") return InputKey{};
    if (name.starts_with("key.mouse.")) {
        const std::string_view b = name.substr(10);
        if (b == "left") return mouse(0);
        if (b == "right") return mouse(1);
        if (b == "middle") return mouse(2);
        if (const auto n = parseInt(b); n && *n >= 1 && *n <= 8) return mouse(*n - 1);
        return std::nullopt;
    }
    if (!name.starts_with("key.keyboard.")) return std::nullopt;
    const std::string_view k = name.substr(13);
    // Every code a name can stand for, searched by name.
    for (int code = 0; code < 512; ++code)
        if (keyboardName(code) == k) return key(code);
    return std::nullopt;
}

const KeyMappingInfo& keyMappingInfo(KeyAction action) { return kMappings[size_t(action)]; }

int keyCategoryOrder(std::string_view category) {
    constexpr std::string_view kOrder[] = {kMovement, kGameplay, kInventory, "key.categories.creative", kMultiplayer,
                                           "key.categories.ui", kMisc};
    const auto it = std::find(std::begin(kOrder), std::end(kOrder), category);
    return int(it - std::begin(kOrder));
}

KeyBindings::KeyBindings() { resetAll(); }

void KeyBindings::resetAll() {
    for (size_t i = 0; i < kKeyActionCount; ++i) keys[i] = kMappings[i].defaultKey;
}

bool KeyBindings::allDefault() const {
    for (size_t i = 0; i < kKeyActionCount; ++i)
        if (!isDefault(KeyAction(i))) return false;
    return true;
}

bool KeyBindings::conflicts(KeyAction a) const {
    const InputKey& k = (*this)[a];
    if (!k.bound()) return false;
    for (size_t i = 0; i < kKeyActionCount; ++i)
        if (KeyAction(i) != a && keys[i] == k) return true;
    return false;
}

} // namespace mc
