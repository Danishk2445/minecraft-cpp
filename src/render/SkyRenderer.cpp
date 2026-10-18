#include "render/SkyRenderer.h"

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "core/Log.h"
#include "core/Rng.h"
#include "gfx/GL.h"
#include "resources/ResourcePack.h"

namespace mc {
namespace {

constexpr float kPi = 3.14159265358979f;

uint32_t rgba(const glm::vec4& c) {
    auto ch = [](float v) { return uint32_t(std::clamp(int(v * 255.0f + 0.5f), 0, 255)); };
    return ch(c.r) | (ch(c.g) << 8) | (ch(c.b) << 16) | (ch(c.a) << 24);
}

void quad(std::vector<SkyVertex>& v, const SkyVertex& a, const SkyVertex& b, const SkyVertex& c, const SkyVertex& d) {
    v.insert(v.end(), {a, b, c, c, d, a});
}

bool loadTexture(Texture& tex, const ResourcePack& pack, const char* path, bool clampEdges) {
    auto img = Image::loadPng(pack.file("minecraft", path));
    if (!img) {
        log::warn("sky: missing texture {}", path);
        return false;
    }
    tex.create(img->width, img->height, 1);
    tex.upload(0, 0, 0, *img);
    tex.setFilter(GL_NEAREST, GL_NEAREST);
    tex.setWrap(clampEdges ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    return true;
}

} // namespace

SkyMesh::~SkyMesh() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
}

void SkyMesh::upload(const std::vector<SkyVertex>& vertices) {
    if (!vao_) {
        glCreateVertexArrays(1, &vao_);
        glCreateBuffers(1, &vbo_);
        glEnableVertexArrayAttrib(vao_, 0);
        glVertexArrayAttribFormat(vao_, 0, 3, GL_FLOAT, GL_FALSE, offsetof(SkyVertex, pos));
        glVertexArrayAttribBinding(vao_, 0, 0);
        glEnableVertexArrayAttrib(vao_, 1);
        glVertexArrayAttribFormat(vao_, 1, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(SkyVertex, color));
        glVertexArrayAttribBinding(vao_, 1, 0);
        glEnableVertexArrayAttrib(vao_, 2);
        glVertexArrayAttribFormat(vao_, 2, 2, GL_FLOAT, GL_FALSE, offsetof(SkyVertex, uv));
        glVertexArrayAttribBinding(vao_, 2, 0);
    }
    count_ = vertices.size();
    if (count_ > capacity_) {
        capacity_ = std::max(count_, capacity_ * 2);
        glNamedBufferData(vbo_, GLsizeiptr(capacity_ * sizeof(SkyVertex)), nullptr, GL_DYNAMIC_DRAW);
        glVertexArrayVertexBuffer(vao_, 0, vbo_, 0, sizeof(SkyVertex));
    }
    if (count_) glNamedBufferSubData(vbo_, 0, GLsizeiptr(count_ * sizeof(SkyVertex)), vertices.data());
}

void SkyMesh::draw() const {
    if (!count_) return;
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(count_));
}

bool SkyRenderer::init(const ResourcePack& pack) {
    if (!shader_.load("sky.vert", "sky.frag")) return false;
    loadTexture(sun_, pack, "textures/environment/sun.png", true);
    loadTexture(moon_, pack, "textures/environment/moon_phases.png", true);
    const uint8_t white[4] = {255, 255, 255, 255};
    white_.create(1, 1, 1);
    white_.upload(0, 0, 0, 1, 1, white);
    if (auto img = Image::loadPng(pack.file("minecraft", "textures/environment/clouds.png"))) cloudImage_ = std::move(*img);

    // Dome: vanilla buildSkyDisc(16): a fan at y=16, radius 512 (white; tinted per frame).
    std::vector<SkyVertex> v;
    const uint32_t w = 0xFFFFFFFFu;
    for (int i = -180; i < 180; i += 45) {
        const float a0 = glm::radians(float(i)), a1 = glm::radians(float(i + 45));
        v.push_back({{0, 16, 0}, w, {}});
        v.push_back({{512.0f * std::cos(a1), 16, 512.0f * std::sin(a1)}, w, {}});
        v.push_back({{512.0f * std::cos(a0), 16, 512.0f * std::sin(a0)}, w, {}});
    }
    disc_.upload(v);

    // Sun (size 30 at y=100) and moon (size 20 at y=-100; UVs set per phase).
    v.clear();
    quad(v, {{-30, 100, -30}, w, {0, 0}}, {{30, 100, -30}, w, {1, 0}}, {{30, 100, 30}, w, {1, 1}},
         {{-30, 100, 30}, w, {0, 1}});
    sunQuad_.upload(v);

    // Stars: vanilla buildStars with java.util.Random(10842).
    v.clear();
    JavaRandom r(10842);
    for (int i = 0; i < 1500; ++i) {
        const float g = r.nextFloat() * 2.0f - 1.0f, h = r.nextFloat() * 2.0f - 1.0f, j = r.nextFloat() * 2.0f - 1.0f;
        const float k = 0.15f + r.nextFloat() * 0.1f;
        const float l = g * g + h * h + j * j;
        if (l <= 0.010000001f || l >= 1.0f) continue;
        const glm::vec3 c = glm::normalize(glm::vec3(g, h, j)) * 100.0f;
        const float m = float(r.nextDouble() * kPi * 2.0);
        // Basis whose +z faces the center, spun by -m around it.
        const glm::vec3 z = glm::normalize(-c);
        glm::vec3 up(0, 1, 0);
        if (std::abs(glm::dot(up, z)) > 0.999f) up = glm::vec3(1, 0, 0);
        const glm::vec3 x0 = glm::normalize(glm::cross(up, z));
        const glm::vec3 y0 = glm::cross(z, x0);
        const glm::vec3 x = x0 * std::cos(-m) + y0 * std::sin(-m);
        const glm::vec3 y = -x0 * std::sin(-m) + y0 * std::cos(-m);
        auto at = [&](float a, float b) { return SkyVertex{c + x * a + y * b, w, {}}; };
        quad(v, at(k, -k), at(k, k), at(-k, k), at(-k, -k));
    }
    stars_.upload(v);
    return true;
}

void SkyRenderer::setFog(const EnvironmentState& env, float end) {
    shader_.set("uFogColor", glm::vec4(env.fogColor, 1.0f));
    shader_.set("uFog", glm::vec4(0.0f, end, end, end));
}

void SkyRenderer::renderSky(const Camera& camera, float aspect, const EnvironmentState& env) {
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    shader_.use();
    Camera skyCam = camera;
    skyCam.farPlane = 2048.0f;
    shader_.set("uProjView", skyCam.projection(aspect) * camera.view());
    white_.bind(0);

    // 1. Dome in the sky color, fading to fog at the horizon.
    glDisable(GL_BLEND);
    shader_.set("uModel", glm::mat4(1.0f));
    shader_.set("uTextured", 0);
    shader_.set("uColor", glm::vec4(env.skyColor, 1.0f));
    setFog(env, env.fogSkyEnd);
    disc_.draw();

    // Everything else in the sky ignores fog.
    shader_.set("uFog", glm::vec4(1e9f, 1e9f + 1, 1e9f, 1e9f + 1));
    glEnable(GL_BLEND);

    // 2. Sunrise / sunset glow.
    if (env.sunrise) {
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
        glm::mat4 m = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0));
        m = glm::rotate(m, glm::radians(std::sin(env.timeOfDay * 2.0f * kPi) < 0.0f ? 180.0f : 0.0f), glm::vec3(0, 0, 1));
        m = glm::rotate(m, glm::radians(90.0f), glm::vec3(0, 0, 1));
        scratch_.clear();
        const uint32_t center = rgba(env.sunriseColor);
        const uint32_t edge = rgba(glm::vec4(glm::vec3(env.sunriseColor), 0.0f));
        for (int l = 0; l < 16; ++l) {
            const float a0 = float(l) * kPi * 2.0f / 16.0f, a1 = float(l + 1) * kPi * 2.0f / 16.0f;
            scratch_.push_back({{0, 100, 0}, center, {}});
            scratch_.push_back({{std::sin(a0) * 120.0f, std::cos(a0) * 120.0f, -std::cos(a0) * 40.0f * env.sunriseColor.a},
                                edge, {}});
            scratch_.push_back({{std::sin(a1) * 120.0f, std::cos(a1) * 120.0f, -std::cos(a1) * 40.0f * env.sunriseColor.a},
                                edge, {}});
        }
        sunrise_.upload(scratch_);
        shader_.set("uModel", m);
        shader_.set("uColor", glm::vec4(1.0f));
        sunrise_.draw();
    }

    // 3. Sun, moon and stars turn with the celestial angle (additive).
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ZERO);
    glm::mat4 cel = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0, 1, 0));
    cel = glm::rotate(cel, glm::radians(env.timeOfDay * 360.0f), glm::vec3(1, 0, 0));
    shader_.set("uModel", cel);
    const float visible = 1.0f - env.rain;
    shader_.set("uTextured", 1);
    shader_.set("uColor", glm::vec4(1, 1, 1, visible));
    sun_.bind(0);
    sunQuad_.draw();

    const int phase = env.moonPhase;
    const float u0 = float(phase % 4) / 4.0f, v0 = float(phase / 4 % 2) / 2.0f;
    const float u1 = u0 + 0.25f, v1 = v0 + 0.5f;
    const uint32_t w = 0xFFFFFFFFu;
    scratch_.clear();
    quad(scratch_, {{-20, -100, 20}, w, {u1, v1}}, {{20, -100, 20}, w, {u0, v1}}, {{20, -100, -20}, w, {u0, v0}},
         {{-20, -100, -20}, w, {u1, v0}});
    moonQuad_.upload(scratch_);
    moon_.bind(0);
    moonQuad_.draw();

    const float stars = env.starBrightness * visible;
    if (stars > 0.0f) {
        shader_.set("uTextured", 0);
        shader_.set("uColor", glm::vec4(stars, stars, stars, stars));
        white_.bind(0);
        stars_.draw();
    }

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void SkyRenderer::renderClouds(const Camera& camera, float aspect, const EnvironmentState& env, double ticks,
                               bool fancy) {
    if (cloudImage_.empty()) return;
    const int w = cloudImage_.width, h = cloudImage_.height;
    auto cloudAt = [&](int x, int z) {
        x = ((x % w) + w) % w;
        z = ((z % h) + h) % h;
        return cloudImage_.alpha(x, z) > 0;
    };

    const double drift = ticks * 0.03;
    const double cx = (camera.position.x + drift) / kCloudCell;
    const double cz = (camera.position.z + 0.33) / kCloudCell;
    const int cellX = int(std::floor(cx)), cellZ = int(std::floor(cz));
    const int radius = 16; // cells (~192 blocks)
    const float y0 = float(kCloudHeight - camera.position.y), y1 = y0 + 4.0f;

    // Vanilla cloud color: white scaled by daylight.
    const float g = env.sunBrightness;
    glm::vec3 color(g * 0.9f + 0.1f, g * 0.9f + 0.1f, g * 0.85f + 0.15f);
    if (env.rain > 0) {
        const float grey = (color.r * 0.3f + color.g * 0.59f + color.b * 0.11f) * 0.6f;
        color = glm::mix(color, glm::vec3(grey), env.rain * 0.95f);
    }
    auto shaded = [&](float s) { return rgba(glm::vec4(color * s, 0.8f)); };
    const uint32_t top = shaded(1.0f), bottom = shaded(0.7f), ns = shaded(0.8f), we = shaded(0.9f);

    scratch_.clear();
    const bool below = y0 > 0.0f, above = y1 < 0.0f;
    for (int dz = -radius; dz <= radius; ++dz)
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dz * dz > radius * radius) continue;
            const int x = cellX + dx, z = cellZ + dz;
            if (!cloudAt(x, z)) continue;
            const float x0 = float(x * kCloudCell - camera.position.x - drift), x1 = x0 + float(kCloudCell);
            const float z0 = float(z * kCloudCell - camera.position.z - 0.33), z1 = z0 + float(kCloudCell);
            auto v = [&](float px, float py, float pz, uint32_t c) { return SkyVertex{{px, py, pz}, c, {}}; };
            if (!fancy) { // one face toward the camera
                if (y0 > 0.0f) quad(scratch_, v(x0, y0, z1, top), v(x0, y0, z0, top), v(x1, y0, z0, top), v(x1, y0, z1, top));
                else quad(scratch_, v(x0, y0, z0, top), v(x0, y0, z1, top), v(x1, y0, z1, top), v(x1, y0, z0, top));
                continue;
            }
            if (!below) quad(scratch_, v(x0, y1, z0, top), v(x0, y1, z1, top), v(x1, y1, z1, top), v(x1, y1, z0, top));
            if (!above) quad(scratch_, v(x0, y0, z1, bottom), v(x0, y0, z0, bottom), v(x1, y0, z0, bottom), v(x1, y0, z1, bottom));
            if (!cloudAt(x, z - 1)) quad(scratch_, v(x1, y1, z0, ns), v(x1, y0, z0, ns), v(x0, y0, z0, ns), v(x0, y1, z0, ns));
            if (!cloudAt(x, z + 1)) quad(scratch_, v(x0, y1, z1, ns), v(x0, y0, z1, ns), v(x1, y0, z1, ns), v(x1, y1, z1, ns));
            if (!cloudAt(x - 1, z)) quad(scratch_, v(x0, y1, z0, we), v(x0, y0, z0, we), v(x0, y0, z1, we), v(x0, y1, z1, we));
            if (!cloudAt(x + 1, z)) quad(scratch_, v(x1, y1, z1, we), v(x1, y0, z1, we), v(x1, y0, z0, we), v(x1, y1, z0, we));
        }
    clouds_.upload(scratch_);
    if (!clouds_.size()) return;

    shader_.use();
    Camera c = camera;
    c.farPlane = 2048.0f;
    shader_.set("uProjView", c.projection(aspect) * camera.view());
    shader_.set("uModel", glm::mat4(1.0f));
    shader_.set("uTextured", 0);
    shader_.set("uColor", glm::vec4(1.0f));
    shader_.set("uFogColor", glm::vec4(env.fogColor, 1.0f));
    const float cloudFog = std::min(float(radius) * 12.0f, env.fogCloudsEnd);
    shader_.set("uFog", glm::vec4(cloudFog * 0.6f, cloudFog, 1e9f, 1e9f + 1));
    white_.bind(0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CCW);
    // Depth prepass, then color: overlapping faces of the translucent layer don't stack.
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_TRUE);
    clouds_.draw();
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    clouds_.draw();
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
}

} // namespace mc
