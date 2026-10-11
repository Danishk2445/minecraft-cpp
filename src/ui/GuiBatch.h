// GuiBatch.h -- batched 2D quads in GUI pixels (y down), one texture per batch.
// Colors are vanilla-style ARGB (0xAARRGGBB).
#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "gfx/Shader.h"
#include "gfx/Texture.h"

namespace mc {

// Vanilla "auto" GUI scale: the largest s with width >= 320s and height >= 240s.
int autoGuiScale(glm::ivec2 framebuffer, int maxScale = 0);

class GuiBatch {
public:
    ~GuiBatch();
    bool init();

    // Starts a frame: GUI pixels are framebuffer pixels / scale.
    void begin(glm::ivec2 framebuffer, int scale);
    void fill(float x0, float y0, float x1, float y1, uint32_t argb);
    void blit(const Texture& texture, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
              uint32_t argb = 0xFFFFFFFF);
    void flush();
    void end();

    int scale() const { return scale_; }
    glm::vec2 size() const { return size_; } // GUI pixels

private:
    struct Vertex {
        glm::vec2 pos;
        glm::vec2 uv;
        uint32_t color; // RGBA8 in memory order
    };
    void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32_t argb);

    Shader shader_;
    Texture white_;
    const Texture* texture_ = nullptr;
    std::vector<Vertex> vertices_;
    unsigned vao_ = 0, vbo_ = 0;
    size_t capacity_ = 0;
    int scale_ = 1;
    glm::vec2 size_{0.0f};
};

} // namespace mc
