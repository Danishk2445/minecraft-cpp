#include "render/Environment.h"

#include "gfx/GL.h"

namespace mc {

void Lightmap::init() {
    texture_.create(16, 16, 1);
    texture_.setFilter(GL_LINEAR, GL_LINEAR);
    texture_.setWrap(GL_CLAMP_TO_EDGE);
}

void Lightmap::update(const EnvironmentState& env, float gamma, float blockFlicker) {
    texels_ = computeLightmap(env, gamma, blockFlicker);
    uint8_t pixels[16 * 16 * 4];
    for (size_t i = 0; i < 256; ++i) {
        pixels[i * 4 + 0] = uint8_t(texels_[i].r * 255.0f + 0.5f);
        pixels[i * 4 + 1] = uint8_t(texels_[i].g * 255.0f + 0.5f);
        pixels[i * 4 + 2] = uint8_t(texels_[i].b * 255.0f + 0.5f);
        pixels[i * 4 + 3] = 255;
    }
    texture_.upload(0, 0, 0, 16, 16, pixels);
}

float LightFlicker::next() {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 7;
    state_ ^= state_ << 17;
    return float(state_ >> 40) / float(1 << 24);
}

void LightFlicker::tick() {
    value_ += (next() - next()) * next() * next() * 0.1f;
    value_ *= 0.9f;
}

} // namespace mc
