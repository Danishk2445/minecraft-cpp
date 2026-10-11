// ParticleRenderer.h -- draws ParticleSystem particles as lit, camera-facing quads.
#pragma once

#include <functional>
#include <vector>

#include <glm/glm.hpp>

#include "core/Math.h"
#include "gfx/Shader.h"

namespace mc {

class Atlas;
class Texture;
class ParticleSystem;
struct Camera;

class ParticleRenderer {
public:
    ~ParticleRenderer();
    bool init();
    // Light levels (block, sky) of a cell.
    using LightFn = std::function<glm::vec2(const BlockPos&)>;
    // The lightmap must be bound to texture unit 1.
    void render(const ParticleSystem& particles, const LightFn& lightAt, const Camera& camera, const glm::mat4& viewProj,
                const Atlas& atlas, const Texture& atlasTexture, float partialTick);

private:
    struct Vertex {
        glm::vec3 pos;
        glm::vec2 uv;
        uint32_t color;
        glm::vec2 light;
    };
    Shader shader_;
    unsigned vao_ = 0, vbo_ = 0;
    std::vector<Vertex> vertices_;
};

} // namespace mc
