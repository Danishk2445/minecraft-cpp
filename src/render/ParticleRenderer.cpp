#include "render/ParticleRenderer.h"

#include <algorithm>
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
                              float partialTick, const glm::vec4& fogColor, const glm::vec4& fogDistances) {
    const auto& list = particles.particles();
    if (list.empty()) return;
    vertices_.clear();
    vertices_.reserve(list.size() * 6);
    // Billboard axes from the camera rotation.
    const glm::vec3 fwd = camera.forward();
    const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 up = glm::cross(right, fwd);
    size_t opaqueEnd = 0;
    // Opaque sheets first (vanilla PARTICLE_SHEET_OPAQUE and TERRAIN_SHEET), then translucent.
    for (const bool translucentPass : {false, true}) {
        for (const Particle& p : list) {
            if (ParticleSystem::translucent(p) != translucentPass || p.type == ParticleType::ExplosionEmitter) continue;
            const float size = ParticleSystem::quadSize(p, partialTick);
            if (size <= 0.0f) continue;
            const glm::dvec3 at = glm::mix(p.oldPos, p.pos, double(partialTick));
            const glm::vec3 c = glm::vec3(at - camera.position);
            const SpriteInfo& sp = atlas.sprite(p.sprite);
            const float u0 = sp.u0 + (sp.u1 - sp.u0) * p.uv.x, u1 = sp.u0 + (sp.u1 - sp.u0) * p.uv.z;
            const float v0 = sp.v0 + (sp.v1 - sp.v0) * p.uv.y, v1 = sp.v0 + (sp.v1 - sp.v0) * p.uv.w;
            const glm::vec2 light = ParticleSystem::light(p, lightAt(BlockPos::containing(at)), partialTick);
            auto ch = [](float v) { return uint32_t(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
            const uint32_t color = ch(p.color.r) | (ch(p.color.g) << 8) | (ch(p.color.b) << 16) | (ch(p.alpha) << 24);
            // Turned about the view axis (falling leaves roll).
            glm::vec3 r = right * size, u = up * size;
            if (p.roll != 0.0f || p.oldRoll != 0.0f) {
                const float a = p.oldRoll + (p.roll - p.oldRoll) * partialTick;
                const float ca = std::cos(a), sa = std::sin(a);
                const glm::vec3 r2 = r * ca + u * sa, u2 = u * ca - r * sa;
                r = r2;
                u = u2;
            }
            const Vertex q[4] = {{c - r - u, {u1, v1}, color, light}, {c - r + u, {u1, v0}, color, light},
                                 {c + r + u, {u0, v0}, color, light}, {c + r - u, {u0, v1}, color, light}};
            for (int i : {0, 1, 2, 0, 2, 3}) vertices_.push_back(q[i]);
        }
        if (!translucentPass) opaqueEnd = vertices_.size();
    }
    if (vertices_.empty()) return;
    glNamedBufferData(vbo_, GLsizeiptr(vertices_.size() * sizeof(Vertex)), vertices_.data(), GL_STREAM_DRAW);
    glVertexArrayVertexBuffer(vao_, 0, vbo_, 0, sizeof(Vertex));
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    shader_.use();
    shader_.set("uViewProj", viewProj);
    shader_.set("uFogColor", fogColor);
    shader_.set("uFog", fogDistances);
    atlasTexture.bind(0);
    glBindVertexArray(vao_);
    if (opaqueEnd > 0) glDrawArrays(GL_TRIANGLES, 0, GLsizei(opaqueEnd));
    if (vertices_.size() > opaqueEnd) {
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glDrawArrays(GL_TRIANGLES, GLint(opaqueEnd), GLsizei(vertices_.size() - opaqueEnd));
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
}

} // namespace mc
