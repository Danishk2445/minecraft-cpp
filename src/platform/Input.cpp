#include "platform/Input.h"

#include <GLFW/glfw3.h>

namespace mc {

void Input::beginFrame() {
    pressed_.fill(false);
    released_.fill(false);
    mousePressed_.fill(false);
    mouseReleased_.fill(false);
    delta_ = glm::dvec2(0.0);
    scroll_ = glm::dvec2(0.0);
    typed_.clear();
    keyEvents_.clear();
}

bool Input::keyDown(int key) const { return key >= 0 && key < kKeys && down_[size_t(key)]; }
bool Input::keyPressed(int key) const { return key >= 0 && key < kKeys && pressed_[size_t(key)]; }
bool Input::keyReleased(int key) const { return key >= 0 && key < kKeys && released_[size_t(key)]; }
bool Input::mouseDown(int b) const { return b >= 0 && b < kButtons && mouseDown_[size_t(b)]; }
bool Input::mousePressed(int b) const { return b >= 0 && b < kButtons && mousePressed_[size_t(b)]; }
bool Input::mouseReleased(int b) const { return b >= 0 && b < kButtons && mouseReleased_[size_t(b)]; }

void Input::onKey(int key, int scancode, int action, int mods) {
    mods_ = mods;
    keyEvents_.push_back({key, scancode, action, mods});
    if (key < 0 || key >= kKeys) return;
    if (action == GLFW_PRESS) {
        if (!down_[size_t(key)]) pressed_[size_t(key)] = true;
        down_[size_t(key)] = true;
    } else if (action == GLFW_RELEASE) {
        if (down_[size_t(key)]) released_[size_t(key)] = true;
        down_[size_t(key)] = false;
    }
}

void Input::onMouseButton(int button, int action, int mods) {
    mods_ = mods;
    if (button < 0 || button >= kButtons) return;
    if (action == GLFW_PRESS) {
        if (!mouseDown_[size_t(button)]) mousePressed_[size_t(button)] = true;
        mouseDown_[size_t(button)] = true;
    } else if (action == GLFW_RELEASE) {
        if (mouseDown_[size_t(button)]) mouseReleased_[size_t(button)] = true;
        mouseDown_[size_t(button)] = false;
    }
}

void Input::onCursor(double x, double y) {
    const glm::dvec2 p(x, y);
    if (haveCursor_) delta_ += p - cursor_;
    cursor_ = p;
    haveCursor_ = true;
}

void Input::onScroll(double dx, double dy) { scroll_ += glm::dvec2(dx, dy); }

void Input::onChar(unsigned codepoint) { typed_.push_back(char32_t(codepoint)); }

void Input::onFocusLost() {
    // Keys released while unfocused never report a release; drop everything.
    for (int k = 0; k < kKeys; ++k)
        if (down_[size_t(k)]) released_[size_t(k)] = true;
    down_.fill(false);
    mouseDown_.fill(false);
    haveCursor_ = false;
}

} // namespace mc
