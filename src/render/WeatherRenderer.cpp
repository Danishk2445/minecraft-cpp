#include "render/WeatherRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "core/Rng.h"
#include "gfx/Camera.h"
#include "gfx/GL.h"
#include "resources/ResourcePack.h"
#include "world/level/Level.h"

namespace mc {

WeatherRenderer::~WeatherRenderer() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
}

bool WeatherRenderer::init(const ResourcePack& pack) {
    if (!shader_.load("particle.vert", "particle.frag")) return false;
    for (auto [tex, name] : {std::pair{&rain_, "rain"}, std::pair{&snow_, "snow"}}) {
        auto img = Image::loadPng(pack.file("minecraft", std::string("textures/environment/") + name + ".png"));
        if (!img) return false;
        tex->create(img->width, img->height, 1);
        tex->upload(0, 0, 0, *img);
        tex->setFilter(GL_NEAREST, GL_NEAREST);
        tex->setWrap(GL_REPEAT);
    }
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

void WeatherRenderer::render(const Level& level, const Camera& camera, const glm::mat4& viewProj, int64_t ticks,
                             float partialTick, const glm::vec4& fogColor, const glm::vec4& fogDistances) {
    const float rain = level.rainLevel(partialTick);
    if (rain <= 0.0f) return;
    const int kRadius = radius;
    const glm::dvec3 cam = camera.position;
    const int cx = int(std::floor(cam.x)), cy = int(std::floor(cam.y)), cz = int(std::floor(cam.z));
    rainVerts_.clear();
    snowVerts_.clear();
    JavaRandom random(0);
    for (int z = cz - kRadius; z <= cz + kRadius; ++z)
        for (int x = cx - kRadius; x <= cx + kRadius; ++x) {
            const int height = level.motionBlockingHeight(x, z);
            const int bottom = std::max(cy - kRadius, height), top = std::max(cy + kRadius, height);
            if (top == bottom) continue;
            const Level::Precipitation kind = level.precipitationAt({x, cy, z});
            if (kind == Level::Precipitation::None) continue;
            // Vanilla's per-column seed (Java int arithmetic).
            const uint32_t ux = uint32_t(x), uz = uint32_t(z);
            const uint32_t hx = ux * ux * 3121u + ux * 45238971u, hz = uz * uz * 418711u + uz * 13761u;
            const int32_t seed = int32_t(hx ^ hz);
            random.setSeed(seed);
            const uint8_t packed = level.lightAt({x, std::max(cy, height), z});
            glm::vec2 light(float(packed & 15), float(packed >> 4));
            float uOffset = 0.0f, vOffset = 0.0f;
            if (kind == Level::Precipitation::Rain) {
                const int i = int(ticks & 131071);
                const int j = int((hx + hz) & 0xFFu);
                const float f = 3.0f + random.nextFloat();
                const float g = -(float(i + j) + partialTick) / 32.0f * f;
                vOffset = std::fmod(g, 32.0f);
            } else {
                const float f = float(ticks) + partialTick;
                const double d1 = random.nextDouble();
                const float g = float(d1 + double(f * 0.01f * float(random.nextGaussian())));
                const double d2 = random.nextDouble();
                const float h = float(d2 + double(f * float(random.nextGaussian()) * 0.001f));
                const float i = -(float(ticks & 511) + partialTick) / 512.0f;
                uOffset = g;
                vOffset = i + h;
                light = glm::vec2(std::floor((light.x * 3.0f + 15.0f) / 4.0f), std::floor((light.y * 3.0f + 15.0f) / 4.0f));
            }
            // Fades from full near the camera to half at the edge of the radius.
            const float gx = float(double(x) + 0.5 - cam.x), gz = float(double(z) + 0.5 - cam.z);
            const float dist2 = gx * gx + gz * gz;
            const float amount = kind == Level::Precipitation::Rain ? 1.0f : 0.8f;
            const float alpha = std::clamp((amount + (0.5f - amount) * std::min(dist2 / float(kRadius * kRadius), 1.0f)) * rain,
                                           0.0f, 1.0f);
            const uint32_t color = 0x00FFFFFFu | (uint32_t(alpha * 255.0f + 0.5f) << 24);
            // A unit-wide quad facing the camera's column.
            const float dx = float(x - cx), dz = float(z - cz);
            const float len = std::sqrt(dx * dx + dz * dz);
            const float sx = len > 0.0f ? -dz / len / 2.0f : 0.5f, sz = len > 0.0f ? dx / len / 2.0f : 0.0f;
            const float yTop = float(double(top) - cam.y), yBottom = float(double(bottom) - cam.y);
            const float u0 = uOffset, u1 = uOffset + 1.0f;
            const float vTop = float(bottom) * 0.25f + vOffset, vBottom = float(top) * 0.25f + vOffset;
            const Vertex q[4] = {{{gx - sx, yTop, gz - sz}, {u0, vTop}, color, light},
                                 {{gx + sx, yTop, gz + sz}, {u1, vTop}, color, light},
                                 {{gx + sx, yBottom, gz + sz}, {u1, vBottom}, color, light},
                                 {{gx - sx, yBottom, gz - sz}, {u0, vBottom}, color, light}};
            auto& out = kind == Level::Precipitation::Rain ? rainVerts_ : snowVerts_;
            for (int k : {0, 1, 2, 0, 2, 3}) out.push_back(q[k]);
        }
    if (rainVerts_.empty() && snowVerts_.empty()) return;
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    shader_.use();
    shader_.set("uViewProj", viewProj);
    shader_.set("uFogColor", fogColor);
    shader_.set("uFog", fogDistances);
    glBindVertexArray(vao_);
    for (auto [verts, tex] : {std::pair{&rainVerts_, &rain_}, std::pair{&snowVerts_, &snow_}}) {
        if (verts->empty()) continue;
        glNamedBufferData(vbo_, GLsizeiptr(verts->size() * sizeof(Vertex)), verts->data(), GL_STREAM_DRAW);
        glVertexArrayVertexBuffer(vao_, 0, vbo_, 0, sizeof(Vertex));
        tex->bind(0);
        glDrawArrays(GL_TRIANGLES, 0, GLsizei(verts->size()));
    }
    glDisable(GL_BLEND);
}

} // namespace mc
