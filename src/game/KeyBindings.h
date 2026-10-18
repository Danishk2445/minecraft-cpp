// KeyBindings.h -- the rebindable controls (vanilla KeyMapping and InputConstants): each
// action's key or mouse button, saved by vanilla's names ("key.keyboard.w", "key.mouse.left").
// Key codes are GLFW's, which vanilla's names map one to one. Only the actions this game
// has are listed (no player list, advancements or hotbar saving). Actions sharing a key
// all respond to it.
#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace mc {

// A keyboard key (GLFW key code) or a mouse button (GLFW button, 0 = left).
struct InputKey {
    enum class Type : unsigned char { Keyboard, Mouse };
    Type type = Type::Keyboard;
    int code = -1; // -1: not bound

    static constexpr InputKey key(int code) { return {Type::Keyboard, code}; }
    static constexpr InputKey mouse(int button) { return {Type::Mouse, button}; }
    bool bound() const { return code >= 0; }
    bool operator==(const InputKey&) const = default;

    // Vanilla's name: "key.keyboard.left.shift", "key.mouse.right", "key.mouse.4",
    // "key.keyboard.unknown" when unbound. It is also the language key of what menus show,
    // except for keys named by the keyboard layout (letters, digits, punctuation).
    std::string name() const;
    static std::optional<InputKey> parse(std::string_view name);
};

// In vanilla's Options.keyMappings order.
enum class KeyAction : unsigned char {
    Attack, Use, Forward, Left, Back, Right, Jump, Sneak, Sprint, Drop, Inventory, Chat, Command, PickItem,
    Screenshot, TogglePerspective, Fullscreen, SwapOffhand,
    Hotbar1, Hotbar2, Hotbar3, Hotbar4, Hotbar5, Hotbar6, Hotbar7, Hotbar8, Hotbar9,
    Count
};
constexpr size_t kKeyActionCount = size_t(KeyAction::Count);

struct KeyMappingInfo {
    const char* name;     // "key.forward": the language key, and the saved name
    const char* category; // "key.categories.movement"
    InputKey defaultKey;
};
const KeyMappingInfo& keyMappingInfo(KeyAction action);
// Vanilla KeyMapping.CATEGORY_SORT_ORDER: movement, gameplay, inventory, creative,
// multiplayer, ui, misc.
int keyCategoryOrder(std::string_view category);
inline KeyAction hotbarAction(int slot) { return KeyAction(int(KeyAction::Hotbar1) + slot); }

struct KeyBindings {
    KeyBindings();
    InputKey& operator[](KeyAction a) { return keys[size_t(a)]; }
    const InputKey& operator[](KeyAction a) const { return keys[size_t(a)]; }
    bool isDefault(KeyAction a) const { return (*this)[a] == keyMappingInfo(a).defaultKey; }
    bool allDefault() const;
    void resetAll();
    // Another action on the same key (vanilla KeyMapping.same), for the conflict marks.
    bool conflicts(KeyAction a) const;

    std::array<InputKey, kKeyActionCount> keys;
};

} // namespace mc
