#include "render/ParticleRenderer.h"

#include <cmath>
#include <cstddef>

#include "entity/Particles.h"
#include "gfx/Camera.h"
#include "gfx/GL.h"
#include "gfx/Texture.h"
#include "resources/Atlas.h"

namespace mc {

ParticleRenderer::~ParticleRenderer() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
}

bool ParticleRenderer::init() {
    if (!shader_.load("particle.vert", "particle.frag")) return false;
    glCreateVertexArrays(1, &vao_);
    glCreateBuffers(1, &vbo_);
    glEnableVertexArrayAttrib(vao_, 0);
    glVertexArrayAttribFormat(vao_, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, pos));
    glVertexArrayAttribBinding(vao_, 0, 0);
    glEnableVertexArrayAttrib(vao_, 1);
    glVertexArrayAttribFormat(vao_, 1, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, uv));
    glVertexArrayAttribBinding(vao_, 1, 0);
    glEnableVertexArrayAttrib(vao_, 2);
    glVertexArrayAttribFormat(vao_, 2, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(Vertex, color));
    glVertexArrayAttribBinding(vao_, 2, 0);
    glEnableVertexArrayAttrib(vao_, 3);
    glVertexArrayAttribFormat(vao_, 3, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, light));
    glVertexArrayAttribBinding(vao_, 3, 0);
    return true;
}

void ParticleRenderer::render(const ParticleSystem& particles, const LightFn& lightAt, const Camera& camera,
                              const glm::mat4& viewProj, const Atlas& atlas, const Texture& atlasTexture,
                              float partialTick) {
    const auto& list = particles.particles();
    if (list.empty()) return;
    vertices_.clear();
    vertices_.reserve(list.size() * 6);
    // Billboard axes from the camera rotation.
    const glm::vec3 fwd = camera.forward();
    const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 up = glm::cross(right, fwd);
    for (const Particle& p : list) {
        const glm::dvec3 at = glm::mix(p.oldPos, p.pos, double(partialTick));
        const glm::vec3 c = glm::vec3(at - camera.position);
        const SpriteInfo& sp = atlas.sprite(p.sprite);
        const float u0 = sp.u0 + (sp.u1 - sp.u0) * p.uv.x, u1 = sp.u0 + (sp.u1 - sp.u0) * p.uv.z;
        const float v0 = sp.v0 + (sp.v1 - sp.v0) * p.uv.y, v1 = sp.v0 + (sp.v1 - sp.v0) * p.uv.w;
        const glm::vec2 light = lightAt(BlockPos(int(std::floor(at.x)), int(std::floor(at.y)), int(std::floor(at.z))));
        const uint32_t color = uint32_t(std::clamp(p.color.r, 0.0f, 1.0f) * 255.0f) |
                               (uint32_t(std::clamp(p.color.g, 0.0f, 1.0f) * 255.0f) << 8) |
                               (uint32_t(std::clamp(p.color.b, 0.0f, 1.0f) * 255.0f) << 16) | 0xFF000000u;
        const glm::vec3 r = right * p.size, u = up * p.size;
        const Vertex q[4] = {{c - r - u, {u1, v1}, color, light}, {c - r + u, {u1, v0}, color, light},
                             {c + r + u, {u0, v0}, color, light}, {c + r - u, {u0, v1}, color, light}};
        for (int i : {0, 1, 2, 0, 2, 3}) vertices_.push_back(q[i]);
    }
    glNamedBufferData(vbo_, GLsizeiptr(vertices_.size() * sizeof(Vertex)), vertices_.data(), GL_STREAM_DRAW);
    glVertexArrayVertexBuffer(vao_, 0, vbo_, 0, sizeof(Vertex));
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    shader_.use();
    shader_.set("uViewProj", viewProj);
    atlasTexture.bind(0);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(vertices_.size()));
}

} // namespace mc
