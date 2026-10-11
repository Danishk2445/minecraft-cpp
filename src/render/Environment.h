// Environment.h (render) -- the GL lightmap texture and block-light flicker.
// The math is GL-free in world/level/Environment.
#pragma once

#include <cstdint>

#include "gfx/Texture.h"
#include "world/level/Environment.h"

namespace mc {

class Lightmap {
public:
    void init();
    // Rebuilds the 16x16 texture (x = block light, y = sky light).
    void update(const EnvironmentState& env, float gamma, float blockFlicker);
    void bind(unsigned unit) const { texture_.bind(unit); }
    const LightmapTexels& texels() const { return texels_; }

private:
    Texture texture_;
    LightmapTexels texels_{};
};

// Vanilla block-light flicker: a damped random walk updated every tick.
class LightFlicker {
public:
    void tick();
    float value() const { return value_; }

private:
    float value_ = 0;
    uint64_t state_ = 0x2545F4914F6CDD1Dull;
    float next();
};

} // namespace mc
