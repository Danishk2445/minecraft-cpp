// GuiBatch.h -- batched 2D quads in GUI pixels (y down), one texture per batch.
// Colors are vanilla-style ARGB (0xAARRGGBB).
#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "gfx/Shader.h"
#include "gfx/Texture.h"
#include "ui/GuiScale.h"

namespace mc {


class GuiBatch {
public:
    ~GuiBatch();
    bool init();

    // Starts a frame: GUI pixels are framebuffer pixels / scale.
    void begin(glm::ivec2 framebuffer, int scale);
    void fill(float x0, float y0, float x1, float y1, uint32_t argb);
    void blit(const Texture& texture, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
              uint32_t argb = 0xFFFFFFFF);
    // A quad with free corners (top-left, bottom-left, bottom-right, top-right), e.g. italic glyphs.
    void blitQuad(const Texture& texture, const glm::vec2 (&corners)[4], float u0, float v0, float u1, float v1,
                  uint32_t argb = 0xFFFFFFFF);
    void flush();
    void end();
    // Re-applies the pass's GL state after other drawing in the middle of it (the menu blur).
    void restoreState();

    // A 2D affine transform (GUI pixels) applied to everything drawn until reset.
    void setTransform(const glm::mat3& m) {
        transform_ = m;
        transformed_ = true;
    }
    void resetTransform() { transformed_ = false; }

    // Alpha blending, vanilla's crosshair blend that inverts what is underneath, or the
    // vignette's that darkens it by the source color (ZERO, ONE_MINUS_SRC_COLOR).
    enum class Blend : uint8_t { Alpha, Invert, Darken };
    void setBlend(Blend blend);

    int scale() const { return scale_; }
    glm::vec2 size() const { return size_; } // GUI pixels

private:
    struct Vertex {
        glm::vec2 pos;
        glm::vec2 uv;
        uint32_t color; // RGBA8 in memory order
    };
    void quad(const glm::vec2 (&p)[4], float u0, float v0, float u1, float v1, uint32_t argb);
    void use(const Texture& texture);
    void applyBlend();

    Shader shader_;
    Texture white_;
    const Texture* texture_ = nullptr;
    std::vector<Vertex> vertices_;
    unsigned vao_ = 0, vbo_ = 0;
    size_t capacity_ = 0;
    int scale_ = 1;
    Blend blend_ = Blend::Alpha;
    glm::mat3 transform_{1.0f};
    bool transformed_ = false;
    glm::vec2 size_{0.0f};
    glm::ivec2 framebuffer_{0};
};

} // namespace mc
