// Input.h -- keyboard/mouse state with per-frame edges, fed by Window's GLFW callbacks.
#pragma once

#include <array>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "game/KeyBindings.h"

namespace mc {

class Input {
public:
    struct KeyEvent {
        int key;
        int scancode;
        int action; // GLFW_PRESS / GLFW_RELEASE / GLFW_REPEAT
        int mods;
    };

    // Clears the edges, deltas and typed text gathered during the previous frame.
    void beginFrame();

    bool keyDown(int key) const;
    bool keyPressed(int key) const;  // went down this frame
    bool keyReleased(int key) const; // went up this frame
    bool mouseDown(int button) const;
    bool mousePressed(int button) const;
    bool mouseReleased(int button) const;
    int mods() const { return mods_; }
    // A bound key or mouse button: held, or went down this frame.
    bool down(const InputKey& k) const { return k.type == InputKey::Type::Mouse ? mouseDown(k.code) : keyDown(k.code); }
    bool pressed(const InputKey& k) const {
        return k.type == InputKey::Type::Mouse ? mousePressed(k.code) : keyPressed(k.code);
    }

    glm::dvec2 cursor() const { return cursor_; }
    glm::dvec2 mouseDelta() const { return delta_; }
    double scrollY() const { return scroll_.y; }
    const std::u32string& typedText() const { return typed_; }
    const std::vector<KeyEvent>& keyEvents() const { return keyEvents_; }

    // Called when cursor capture changes so the first motion event doesn't jump.
    void resetMouseTracking() { haveCursor_ = false; }

    // Window callbacks.
    void onKey(int key, int scancode, int action, int mods);
    void onMouseButton(int button, int action, int mods);
    void onCursor(double x, double y);
    void onScroll(double dx, double dy);
    void onChar(unsigned codepoint);
    void onFocusLost();

    static constexpr int kButtons = 8;

private:
    static constexpr int kKeys = 512;
    std::array<bool, kKeys> down_{};
    std::array<bool, kKeys> pressed_{};
    std::array<bool, kKeys> released_{};
    std::array<bool, kButtons> mouseDown_{};
    std::array<bool, kButtons> mousePressed_{};
    std::array<bool, kButtons> mouseReleased_{};
    int mods_ = 0;
    glm::dvec2 cursor_{0.0};
    glm::dvec2 delta_{0.0};
    glm::dvec2 scroll_{0.0};
    bool haveCursor_ = false;
    std::u32string typed_;
    std::vector<KeyEvent> keyEvents_;
};

} // namespace mc
