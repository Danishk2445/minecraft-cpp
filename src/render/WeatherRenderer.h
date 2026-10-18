// WeatherRenderer.h -- falling rain and snow (vanilla WeatherEffectRenderer): a column
// of the rain or snow texture over every block within 10 of the camera where it falls,
// turned to face the camera, scrolling down, fading toward the edge of the radius.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "gfx/Shader.h"
#include "gfx/Texture.h"

namespace mc {

class Level;
class ResourcePack;
struct Camera;

class WeatherRenderer {
public:
    ~WeatherRenderer();
    bool init(const ResourcePack& pack);
    // The lightmap must be bound to texture unit 1.
    void render(const Level& level, const Camera& camera, const glm::mat4& viewProj, int64_t ticks, float partialTick,
                const glm::vec4& fogColor, const glm::vec4& fogDistances);

private:
    struct Vertex {
        glm::vec3 pos;
        glm::vec2 uv;
        uint32_t color;
        glm::vec2 light;
    };
    Shader shader_;
    Texture rain_, snow_;
    unsigned vao_ = 0, vbo_ = 0;
    std::vector<Vertex> rainVerts_, snowVerts_;
};

} // namespace mc
