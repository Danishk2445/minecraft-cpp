// GuiScale.h -- vanilla Window.calculateScale: GUI pixels per framebuffer pixel.
#pragma once

#include <glm/glm.hpp>

namespace mc {

// The largest scale s (at most `maxScale`, 0 = no limit) with width >= 320s and
// height >= 240s: 1080p gets 4, 720p gets 3.
constexpr int autoGuiScale(glm::ivec2 fb, int maxScale = 0) {
    int s = 1;
    while (s != maxScale && s < fb.x && s < fb.y && fb.x / (s + 1) >= 320 && fb.y / (s + 1) >= 240) ++s;
    return s;
}

} // namespace mc
