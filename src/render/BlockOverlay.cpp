#include "render/BlockOverlay.h"

#include <cstddef>
#include <format>

#include "gfx/Camera.h"
#include "gfx/GL.h"
#include "render/ClientAssets.h"

namespace mc {

BlockOverlay::~BlockOverlay() {
    if (lineVao_) glDeleteVertexArrays(1, &lineVao_);
    if (lineVbo_) glDeleteBuffers(1, &lineVbo_);
    if (crackVao_) glDeleteVertexArrays(1, &crackVao_);
    if (crackVbo_) glDeleteBuffers(1, &crackVbo_);
}

bool BlockOverlay::init(const ClientAssets& assets) {
    assets_ = &assets;
    if (!lines_.load("lines.vert", "lines.frag") || !crack_.load("crumbling.vert", "crumbling.frag")) return false;
    for (int i = 0; i < 10; ++i) crackSprites_[i] = assets.blockAtlas().find(std::format("minecraft:block/destroy_stage_{}", i));

    glCreateVertexArrays(1, &lineVao_);
    glCreateBuffers(1, &lineVbo_);
    glEnableVertexArrayAttrib(lineVao_, 0);
    glVertexArrayAttribFormat(lineVao_, 0, 3, GL_FLOAT, GL_FALSE, offsetof(LineVertex, pos));
    glVertexArrayAttribBinding(lineVao_, 0, 0);
    glEnableVertexArrayAttrib(lineVao_, 1);
    glVertexArrayAttribFormat(lineVao_, 1, 3, GL_FLOAT, GL_FALSE, offsetof(LineVertex, other));
    glVertexArrayAttribBinding(lineVao_, 1, 0);
    glEnableVertexArrayAttrib(lineVao_, 2);
    glVertexArrayAttribFormat(lineVao_, 2, 1, GL_FLOAT, GL_FALSE, offsetof(LineVertex, side));
    glVertexArrayAttribBinding(lineVao_, 2, 0);

    glCreateVertexArrays(1, &crackVao_);
    glCreateBuffers(1, &crackVbo_);
    glEnableVertexArrayAttrib(crackVao_, 0);
    glVertexArrayAttribFormat(crackVao_, 0, 3, GL_FLOAT, GL_FALSE, offsetof(CrackVertex, pos));
    glVertexArrayAttribBinding(crackVao_, 0, 0);
    glEnableVertexArrayAttrib(crackVao_, 1);
    glVertexArrayAttribFormat(crackVao_, 1, 2, GL_FLOAT, GL_FALSE, offsetof(CrackVertex, uv));
    glVertexArrayAttribBinding(crackVao_, 1, 0);
    return true;
}

void BlockOverlay::render(const Camera& camera, glm::ivec2 viewport, const BlockPos& pos, const Shape& shape,
                          int crackStage) {
    if (shape.empty()) return;
    const glm::vec3 origin = glm::vec3(glm::dvec3(pos.vec()) - camera.position);
    const glm::mat4 viewProj = camera.projection(float(viewport.x) / float(std::max(viewport.y, 1))) * camera.view();

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);

    // Cracks: every face of every box, the sprite mapped by block-local position.
    if (crackStage >= 0 && crackStage < 10) {
        crackVerts_.clear();
        const SpriteInfo& sp = assets_->blockAtlas().sprite(crackSprites_[crackStage]);
        for (const AABB& b : shape) {
            const glm::vec3 lo = glm::vec3(b.min) - 0.001f, hi = glm::vec3(b.max) + 0.001f;
            auto uv = [&](float s, float t) { return glm::vec2(sp.u0 + (sp.u1 - sp.u0) * s, sp.v0 + (sp.v1 - sp.v0) * t); };
            auto face = [&](glm::vec3 a, glm::vec3 b2, glm::vec3 c, glm::vec3 d, int axisS, int axisT, bool flipT) {
                const glm::vec3 q[4] = {a, b2, c, d};
                glm::vec2 t[4];
                for (int i = 0; i < 4; ++i) {
                    const float s = glm::fract(q[i][axisS] - 1e-4f), tt = glm::fract(q[i][axisT] - 1e-4f);
                    t[i] = uv(s, flipT ? 1.0f - tt : tt);
                }
                const int order[6] = {0, 1, 2, 0, 2, 3};
                for (int i : order) crackVerts_.push_back({origin + q[i], t[i]});
            };
            face({lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, lo.y, hi.z}, {lo.x, lo.y, hi.z}, 0, 2, false); // down
            face({lo.x, hi.y, lo.z}, {lo.x, hi.y, hi.z}, {hi.x, hi.y, hi.z}, {hi.x, hi.y, lo.z}, 0, 2, false); // up
            face({lo.x, lo.y, lo.z}, {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, lo.y, lo.z}, 0, 1, true);  // north
            face({lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}, 0, 1, true);  // south
            face({lo.x, lo.y, lo.z}, {lo.x, lo.y, hi.z}, {lo.x, hi.y, hi.z}, {lo.x, hi.y, lo.z}, 2, 1, true);  // west
            face({hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, hi.y, hi.z}, {hi.x, lo.y, hi.z}, 2, 1, true);  // east
        }
        glNamedBufferData(crackVbo_, GLsizeiptr(crackVerts_.size() * sizeof(CrackVertex)), crackVerts_.data(),
                          GL_STREAM_DRAW);
        glVertexArrayVertexBuffer(crackVao_, 0, crackVbo_, 0, sizeof(CrackVertex));
        glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-3.0f, -3.0f);
        crack_.use();
        crack_.set("uViewProj", viewProj);
        assets_->blockAtlasTexture().bind(0);
        glBindVertexArray(crackVao_);
        glDrawArrays(GL_TRIANGLES, 0, GLsizei(crackVerts_.size()));
        glDisable(GL_POLYGON_OFFSET_FILL);
    }

    // Outline: the 12 edges of each box, slightly inflated like vanilla.
    lineVerts_.clear();
    for (const AABB& b : shape) {
        const glm::vec3 lo = glm::vec3(b.min) - 0.002f, hi = glm::vec3(b.max) + 0.002f;
        const glm::vec3 c[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, lo.y, hi.z}, {lo.x, lo.y, hi.z},
                                {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
        static constexpr int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                             {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (const auto& e : edges) {
            const glm::vec3 a = origin + c[e[0]], z = origin + c[e[1]];
            const LineVertex q[4] = {{a, z, -1.0f}, {a, z, 1.0f}, {z, a, 1.0f}, {z, a, -1.0f}};
            const int order[6] = {0, 1, 2, 0, 2, 3};
            for (int i : order) lineVerts_.push_back(q[i]);
        }
    }
    glNamedBufferData(lineVbo_, GLsizeiptr(lineVerts_.size() * sizeof(LineVertex)), lineVerts_.data(), GL_STREAM_DRAW);
    glVertexArrayVertexBuffer(lineVao_, 0, lineVbo_, 0, sizeof(LineVertex));
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    lines_.use();
    lines_.set("uViewProj", viewProj);
    lines_.set("uViewport", glm::vec2(viewport));
    lines_.set("uWidth", std::max(2.0f, float(viewport.y) / 400.0f));
    lines_.set("uColor", glm::vec4(0.0f, 0.0f, 0.0f, 0.4f));
    glBindVertexArray(lineVao_);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(lineVerts_.size()));
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

} // namespace mc
