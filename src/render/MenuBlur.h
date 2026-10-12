// MenuBlur.h -- the blur behind menus (vanilla post_effect/blur.json): three
// horizontal and vertical box-blur passes over what is on screen so far.
#pragma once

#include <glm/glm.hpp>

#include "gfx/Shader.h"
#include "gfx/Texture.h"

namespace mc {

class MenuBlur {
public:
    ~MenuBlur();
    bool init();
    // Blurs the default framebuffer (`size` pixels) in place. `radius` is vanilla's
    // menu background blurriness, in framebuffer pixels; below 1 does nothing.
    void apply(glm::ivec2 size, int radius);

private:
    void resize(glm::ivec2 size);

    Shader shader_;
    unsigned vao_ = 0;
    unsigned fbo_[2] = {0, 0};
    Texture color_[2];
    glm::ivec2 size_{0};
};

} // namespace mc
