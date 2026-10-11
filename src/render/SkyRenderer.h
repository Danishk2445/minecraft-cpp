// SkyRenderer.h -- vanilla overworld sky: dome disc with horizon fog, sunrise
// fan, sun, moon phases, stars; and the 3D "fancy" clouds.
#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "core/Image.h"
#include "gfx/Camera.h"
#include "gfx/Shader.h"
#include "gfx/Texture.h"
#include "render/Environment.h"

namespace mc {

class ResourcePack;

struct SkyVertex {
    glm::vec3 pos;
    uint32_t color; // RGBA8
    glm::vec2 uv;
};

// A small dynamic mesh of SkyVertex triangles.
class SkyMesh {
public:
    ~SkyMesh();
    void upload(const std::vector<SkyVertex>& vertices);
    void draw() const;
    size_t size() const { return count_; }

private:
    unsigned vao_ = 0, vbo_ = 0;
    size_t capacity_ = 0, count_ = 0;
};

class SkyRenderer {
public:
    bool init(const ResourcePack& pack);
    // Draws the sky behind everything (call first, after clearing to the fog color).
    void renderSky(const Camera& camera, float aspect, const EnvironmentState& env);
    // Draws clouds (call after the translucent terrain pass).
    void renderClouds(const Camera& camera, float aspect, const EnvironmentState& env, double ticks);

    static constexpr double kCloudHeight = 192.33;
    static constexpr double kCloudCell = 12.0;

private:
    void setFog(const EnvironmentState& env, float end);

    Shader shader_;
    Texture sun_, moon_, white_;
    SkyMesh disc_, sunrise_, sunQuad_, moonQuad_, stars_, clouds_;
    Image cloudImage_;
    std::vector<SkyVertex> scratch_;
};

} // namespace mc
