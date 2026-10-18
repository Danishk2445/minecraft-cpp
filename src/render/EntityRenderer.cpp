#include "render/EntityRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <glm/gtc/matrix_transform.hpp>

#include "core/Log.h"
#include "entity/ExperienceOrb.h"
#include "entity/FallingBlockEntity.h"
#include "entity/LightningBolt.h"
#include "entity/PrimedTnt.h"
#include "entity/ItemEntity.h"
#include "entity/Player.h"
#include "entity/model/EntityModels.h"
#include "entity/model/HumanoidModel.h"
#include "gfx/GL.h"
#include "entity/model/ItemGeometry.h"
#include "render/ClientAssets.h"
#include "resources/BlockModels.h"
#include "resources/ResourcePack.h"
#include "world/block/Tints.h"
#include "world/level/Level.h"
#include "world/mesh/BlockEntityMeshes.h"
#include "world/mesh/ChunkMesher.h"

namespace mc {
namespace {

uint32_t packColor(const glm::vec3& rgb, float alpha) {
    auto c = [](float v) { return uint32_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return c(rgb.r) | (c(rgb.g) << 8) | (c(rgb.b) << 16) | (c(alpha) << 24);
}

glm::vec3 unpackRgb(uint32_t rgb) {
    return glm::vec3(float((rgb >> 16) & 255), float((rgb >> 8) & 255), float(rgb & 255)) / 255.0f;
}

// Vanilla RenderType VIEW_OFFSET_Z_LAYERING: shadows sit a hair closer to the camera
// than the block tops they lie on.
constexpr float kViewOffsetZ = 0.99975586f;

} // namespace

EntityRenderer::~EntityRenderer() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (glowVao_) glDeleteVertexArrays(1, &glowVao_);
    if (glowVbo_) glDeleteBuffers(1, &glowVbo_);
}

bool EntityRenderer::init(const ClientAssets& assets) {
    assets_ = &assets;
    if (!shader_.load("entity.vert", "entity.frag")) return false;
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
    glEnableVertexArrayAttrib(vao_, 4);
    glVertexArrayAttribFormat(vao_, 4, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(Vertex, overlay));
    glVertexArrayAttribBinding(vao_, 4, 0);
    playerRest_ = playerModel(false);
    playerPosed_ = playerRest_;
    initMobModels();
    // The charged creeper's swirl scrolls across its texture.
    const_cast<Texture&>(texture("minecraft:entity/creeper/creeper_armor")).setWrap(GL_REPEAT);
    if (!glowShader_.load("lightning.vert", "lightning.frag")) return false;
    glCreateVertexArrays(1, &glowVao_);
    glCreateBuffers(1, &glowVbo_);
    glEnableVertexArrayAttrib(glowVao_, 0);
    glVertexArrayAttribFormat(glowVao_, 0, 3, GL_FLOAT, GL_FALSE, offsetof(GlowVertex, pos));
    glVertexArrayAttribBinding(glowVao_, 0, 0);
    glEnableVertexArrayAttrib(glowVao_, 1);
    glVertexArrayAttribFormat(glowVao_, 1, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(GlowVertex, color));
    glVertexArrayAttribBinding(glowVao_, 1, 0);
    return true;
}

const Texture& EntityRenderer::texture(std::string_view id) { return assets_->texture(id); }

void EntityRenderer::begin(const glm::dvec3& cameraPos, const glm::vec3& cameraRight, const glm::vec3& cameraUp) {
    cameraPos_ = cameraPos;
    cameraRight_ = cameraRight;
    cameraUp_ = cameraUp;
    vertices_.clear();
    batches_.clear();
    glow_.clear();
}

void EntityRenderer::addBatch(const Texture* tex, size_t first, Pass pass) {
    const size_t count = vertices_.size() - first;
    if (count == 0) return;
    if (!batches_.empty() && batches_.back().texture == tex && batches_.back().pass == pass &&
        batches_.back().first + batches_.back().count == first) {
        batches_.back().count += count;
        return;
    }
    batches_.push_back({tex, first, count, pass});
}

void EntityRenderer::submitModel(const ModelPart& root, int texWidth, int texHeight, std::string_view textureId,
                                 const glm::dvec3& origin, const glm::mat4& transform, glm::vec2 light,
                                 uint32_t tint, float alpha, uint32_t overlay) {
    const Texture& tex = texture(textureId);
    quads_.clear();
    bakeModel(root, transform, texWidth, texHeight, quads_);
    const size_t first = vertices_.size();
    const glm::vec3 base(origin - cameraPos_);
    const glm::vec3 rgb = unpackRgb(tint);
    for (const ModelQuad& q : quads_) {
        // Vanilla entity lighting: the two world-space lights, by the face normal.
        const uint32_t color = packColor(rgb * entityShade(q.normal), alpha);
        Vertex v[4];
        for (size_t k = 0; k < 4; ++k) v[k] = {base + q.pos[k], q.uv[k], color, light, overlay};
        for (int i : {0, 1, 2, 0, 2, 3}) vertices_.push_back(v[i]);
    }
    addBatch(&tex, first, alpha < 1.0f ? Pass::Translucent : Pass::Cutout);
}

void EntityRenderer::submitBlock(const BlockRegistry& registry, StateId state, const BlockPos& variantPos,
                                 const glm::dvec3& origin, const glm::mat4& transform, glm::vec2 light, uint32_t overlay) {
    const StateInfo& info = registry.info(state);
    if ((info.flags & kAir) || !(info.flags & kHasGeometry) || info.layer == RenderLayer::Invisible) return;
    blockModels_.clear();
    BlockModels::collect(registry.modelChoices(state), variantPos, blockModels_);
    const Block& block = registry.blockOf(state);
    const int power = block.tint.source == TintSource::Redstone ? registry.intValue(state, "power") : 0;
    const int age = block.tint.source == TintSource::Stem ? registry.intValue(state, "age") : 0;
    const size_t first = vertices_.size();
    const glm::vec3 base(origin - cameraPos_);
    for (const BakedModel* m : blockModels_)
        for (const BakedQuad& q : m->quads) {
            const uint32_t tint = q.tint < 0 ? 0xFFFFFFu : resolveTint(block.tint, q.tint, BiomeColors{}, power, age);
            const glm::vec3 rgb = unpackRgb(tint) * (q.shade ? blockFaceShade(q.face) : 1.0f);
            const uint32_t color = packColor(rgb, 1.0f);
            const glm::vec2 l = glm::max(light, glm::vec2(float(std::max<int>(info.emission, q.emission)), 0.0f));
            Vertex v[4];
            for (size_t k = 0; k < 4; ++k)
                v[k] = {base + glm::vec3(transform * glm::vec4(q.pos[k], 1.0f)), q.uv[k], color, l, overlay};
            for (int i : {0, 1, 2, 0, 2, 3}) vertices_.push_back(v[i]);
        }
    addBatch(&assets_->blockAtlasTexture(), first, info.layer == RenderLayer::Translucent ? Pass::Translucent : Pass::Cutout);
}

void EntityRenderer::submitFallingBlock(const FallingBlockEntity& block, const Level& level, float partialTick,
                                        int skyDarken) {
    const glm::dvec3 feet = glm::mix(block.oldPos, block.pos, double(partialTick));
    const uint8_t l = level.lightAt(BlockPos::containing({feet.x, feet.y + double(block.height), feet.z}));
    submitBlock(level.registry(), block.state, block.startPos, feet, glm::translate(glm::mat4(1.0f), {-0.5f, 0.0f, -0.5f}),
                glm::vec2(float(l & 15), float(l >> 4)));
    submitShadow(level, feet, 0.5f, skyDarken);
}

void EntityRenderer::submitPrimedTnt(const PrimedTnt& tnt, const Level& level, float partialTick, int skyDarken) {
    const glm::dvec3 feet = glm::mix(tnt.oldPos, tnt.pos, double(partialTick));
    const float fuse = float(tnt.fuse) - partialTick + 1.0f;
    glm::mat4 m = glm::translate(glm::mat4(1.0f), {0.0f, 0.5f, 0.0f});
    if (fuse < 10.0f) {
        float g = std::clamp(1.0f - fuse / 10.0f, 0.0f, 1.0f);
        g *= g;
        g *= g;
        m = glm::scale(m, glm::vec3(1.0f + g * 0.3f));
    }
    m = glm::rotate(m, glm::radians(-90.0f), {0.0f, 1.0f, 0.0f});
    m = glm::translate(m, {-0.5f, -0.5f, 0.5f});
    m = glm::rotate(m, glm::radians(90.0f), {0.0f, 1.0f, 0.0f});
    const uint8_t l = level.lightAt(BlockPos::containing(feet + glm::dvec3(0.0, 0.5, 0.0)));
    const bool white = int(fuse) / 5 % 2 == 0;
    submitBlock(level.registry(), level.registry().defaultState("tnt"), BlockPos::containing(feet), feet, m,
                glm::vec2(float(l & 15), float(l >> 4)), white ? kWhiteOverlay : 0u);
    submitShadow(level, feet, 0.5f, skyDarken);
}

void EntityRenderer::submitLightning(const LightningBolt& bolt, float partialTick) {
    const glm::vec3 base(glm::mix(bolt.oldPos, bolt.pos, double(partialTick)) - cameraPos_);
    // The trunk's wander, from the top (index 7) down.
    float fx[8], fz[8];
    float x = 0.0f, z = 0.0f;
    JavaRandom trunk(bolt.seed);
    for (int i = 7; i >= 0; --i) {
        fx[i] = x;
        fz[i] = z;
        x += float(trunk.nextInt(11) - 5);
        z += float(trunk.nextInt(11) - 5);
    }
    const uint32_t color = uint32_t(0.45f * 255.0f + 0.5f) | (uint32_t(0.45f * 255.0f + 0.5f) << 8) |
                           (uint32_t(0.5f * 255.0f + 0.5f) << 16) | (uint32_t(0.3f * 255.0f + 0.5f) << 24);
    auto quad = [&](float x1, float z1, int index, float x2, float z2, float off2, float off1, bool ox1, bool oz1, bool ox2,
                    bool oz2) {
        const GlowVertex v[4] = {
            {base + glm::vec3(x1 + (ox1 ? off1 : -off1), float(index * 16), z1 + (oz1 ? off1 : -off1)), color},
            {base + glm::vec3(x2 + (ox1 ? off2 : -off2), float((index + 1) * 16), z2 + (oz1 ? off2 : -off2)), color},
            {base + glm::vec3(x2 + (ox2 ? off2 : -off2), float((index + 1) * 16), z2 + (oz2 ? off2 : -off2)), color},
            {base + glm::vec3(x1 + (ox2 ? off1 : -off1), float(index * 16), z1 + (oz2 ? off1 : -off1)), color}};
        for (int i : {0, 1, 2, 0, 2, 3}) glow_.push_back(v[i]);
    };
    // Four nested layers of the trunk and two forks, each a box of four quads.
    for (int j = 0; j < 4; ++j) {
        JavaRandom r(bolt.seed);
        for (int k = 0; k < 3; ++k) {
            const int top = k > 0 ? 7 - k : 7;
            const int bottom = k > 0 ? top - 2 : 0;
            float h = fx[top] - x, n = fz[top] - z;
            for (int o = top; o >= bottom; --o) {
                const float p = h, q = n;
                if (k == 0) {
                    h += float(r.nextInt(11) - 5);
                    n += float(r.nextInt(11) - 5);
                } else {
                    h += float(r.nextInt(31) - 15);
                    n += float(r.nextInt(31) - 15);
                }
                float v = 0.1f + float(j) * 0.2f;
                if (k == 0) v *= float(o) * 0.1f + 1.0f;
                float w = 0.1f + float(j) * 0.2f;
                if (k == 0) w *= (float(o) - 1.0f) * 0.1f + 1.0f;
                quad(h, n, o, p, q, v, w, false, false, true, false);
                quad(h, n, o, p, q, v, w, true, false, true, true);
                quad(h, n, o, p, q, v, w, true, true, false, true);
                quad(h, n, o, p, q, v, w, false, true, false, false);
            }
        }
    }
}

void EntityRenderer::submitShadow(const Level& level, const glm::dvec3& feet, float radius, int skyDarken,
                                  float shadowStrength) {
    if (!shadows) return;
    const glm::dvec3 toCamera = feet - cameraPos_;
    // Fades out at 16 blocks; items and orbs are 0.75 as dark as mobs.
    const float strength = float(1.0 - glm::dot(toCamera, toCamera) / 256.0) * shadowStrength;
    if (strength <= 0.0f) return;
    const float size = std::min(radius, 32.0f);
    const float depth = std::min(strength / 0.5f, size);
    const int x0 = int(std::floor(feet.x - size)), x1 = int(std::floor(feet.x + size));
    const int y0 = int(std::floor(feet.y - depth)), y1 = int(std::floor(feet.y));
    const int z0 = int(std::floor(feet.z - size)), z1 = int(std::floor(feet.z + size));
    const BlockRegistry& reg = level.registry();
    const Texture& tex = texture("minecraft:misc/shadow");
    const size_t first = vertices_.size();
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y) {
                // Vanilla renderBlockShadow: only on visible full-collision blocks, in light above 3.
                const BlockPos pos(x, y, z);
                const StateId below = level.getBlock(pos.below());
                const StateInfo& info = reg.info(below);
                if ((info.flags & kAir) || !(info.flags & (kHasGeometry | kCodeRendered))) continue;
                const uint8_t l = level.lightAt(pos);
                const int raw = std::max(int(l & 15), int(l >> 4) - skyDarken);
                if (raw <= 3 || info.collision != 1) continue; // shape 1 is the full cube
                const Shape& shape = reg.outlineShape(below);
                if (shape.empty()) continue;
                AABB bounds = shape[0];
                for (const AABB& b : shape) bounds = AABB(glm::min(bounds.min, b.min), glm::max(bounds.max, b.max));
                const float level01 = float(raw) / 15.0f;
                const float brightness = level01 / (4.0f - 3.0f * level01);
                const float weight = strength - float(feet.y - double(y)) * 0.5f;
                const float alpha = std::min(weight * 0.5f * brightness, 1.0f);
                if (alpha < 0.0f) continue;
                const float lx = float(double(x) + bounds.min.x - feet.x), hx = float(double(x) + bounds.max.x - feet.x);
                const float ly = float(double(y) + bounds.min.y - feet.y);
                const float lz = float(double(z) + bounds.min.z - feet.z), hz = float(double(z) + bounds.max.z - feet.z);
                const float u0 = -lx / 2.0f / size + 0.5f, u1 = -hx / 2.0f / size + 0.5f;
                const float v0 = -lz / 2.0f / size + 0.5f, v1 = -hz / 2.0f / size + 0.5f;
                const uint32_t color = packColor(glm::vec3(1.0f), alpha);
                const glm::vec3 base(toCamera);
                auto vertex = [&](float px, float pz, float u, float v) {
                    return Vertex{(base + glm::vec3(px, ly, pz)) * kViewOffsetZ, {u, v}, color, {15.0f, 15.0f}};
                };
                const Vertex q[4] = {vertex(lx, lz, u0, v0), vertex(lx, hz, u0, v1), vertex(hx, hz, u1, v1),
                                     vertex(hx, lz, u1, v0)};
                for (int i : {0, 1, 2, 0, 2, 3}) vertices_.push_back(q[i]);
            }
    addBatch(&tex, first, Pass::Shadow);
}

void EntityRenderer::submitItem(const std::vector<ItemQuad>& quads, const glm::dvec3& origin, const glm::mat4& transform,
                                glm::vec2 light) {
    const size_t first = vertices_.size();
    const glm::vec3 base(origin - cameraPos_);
    const glm::mat3 nm(transform);
    for (const ItemQuad& q : quads) {
        const glm::vec3 rgb = unpackRgb(q.tint);
        const uint32_t color = packColor(rgb * entityShade(glm::normalize(nm * q.normal)), 1.0f);
        Vertex v[4];
        for (size_t k = 0; k < 4; ++k) v[k] = {base + glm::vec3(transform * glm::vec4(q.pos[k], 1.0f)), q.uv[k], color, light};
        for (int i : {0, 1, 2, 0, 2, 3}) vertices_.push_back(v[i]);
    }
    addBatch(&assets_->blockAtlasTexture(), first, Pass::Cutout);
}

void EntityRenderer::submitText(const std::vector<Font::GlyphQuad>& quads, const Texture& fontTexture,
                                const glm::dvec3& origin, const glm::mat4& transform, glm::vec2 light) {
    const size_t first = vertices_.size();
    const glm::vec3 base(origin - cameraPos_);
    for (const Font::GlyphQuad& q : quads) {
        const uint32_t c = q.argb;
        const uint32_t color = ((c >> 16) & 0xFF) | (c & 0xFF00) | ((c & 0xFF) << 16) | (c & 0xFF000000u);
        const glm::vec2 uv[4] = {q.uv0, {q.uv0.x, q.uv1.y}, q.uv1, {q.uv1.x, q.uv0.y}};
        Vertex v[4];
        for (size_t k = 0; k < 4; ++k)
            v[k] = {base + glm::vec3(transform * glm::vec4(q.corners[k], 0.0f, 1.0f)), uv[k], color, light};
        for (int i : {0, 1, 2, 0, 2, 3}) vertices_.push_back(v[i]);
    }
    addBatch(&fontTexture, first, Pass::Cutout);
}

void EntityRenderer::submitGroundItem(const ItemEntity& item, const Level& level, float partialTick, int skyDarken,
                                      const glm::dvec3* at) {
    if (item.stack.empty()) return;
    const glm::dvec3 feet = at ? *at : glm::mix(item.oldPos, item.pos, double(partialTick));
    const uint8_t l = level.lightAt(BlockPos::containing(feet + glm::dvec3(0.0, 0.125, 0.0)));
    const glm::vec2 light(float(l & 15), float(l >> 4));
    ItemModelContext ctx;
    ctx.display = DisplayContext::Ground;
    ctx.count = item.stack.count;
    ctx.maxStack = item.stack.maxStack();
    layers_.clear();
    assets_->itemDefinitions().evaluate(item.stack.item->id, ctx, &assets_->grassColormap(), layers_);
    itemQuads_.clear();
    buildItemQuads(layers_, ctx.display, assets_->blockEntityModels(), assets_->blockAtlas(), itemQuads_);
    if (itemQuads_.empty()) return;
    // The model's bounds after the ground transform: it rests 1/16 above its lowest point.
    glm::vec3 lo(1e9f), hi(-1e9f);
    for (const ItemQuad& q : itemQuads_)
        for (const glm::vec3& p : q.pos) lo = glm::min(lo, p), hi = glm::max(hi, p);
    const float age = float(item.age) + partialTick;
    const float bob = std::sin(age / 10.0f + item.bobOffset) * 0.1f + 0.1f;
    glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, bob - lo.y + 0.0625f, 0.0f));
    m = glm::rotate(m, age / 20.0f + item.bobOffset, glm::vec3(0, 1, 0)); // ItemEntity.getSpin, radians
    // renderMultipleFromCount: 1 to 5 copies by count, scattered (blocks) or stacked (flat items).
    const int n = item.stack.count > 48 ? 5 : item.stack.count > 32 ? 4 : item.stack.count > 16 ? 3 : item.stack.count > 1 ? 2 : 1;
    JavaRandom r(int64_t(item.stack.item->index) * 31 + item.stack.damage); // a fixed scatter per item
    const float depth = hi.z - lo.z;
    if (depth > 0.0625f) {
        submitItem(itemQuads_, feet, m, light);
        for (int j = 1; j < n; ++j) {
            const glm::vec3 o((r.nextFloat() * 2.0f - 1.0f) * 0.15f, (r.nextFloat() * 2.0f - 1.0f) * 0.15f,
                              (r.nextFloat() * 2.0f - 1.0f) * 0.15f);
            submitItem(itemQuads_, feet, glm::translate(m, o), light);
        }
    } else {
        const float step = depth * 1.5f;
        m = glm::translate(m, glm::vec3(0.0f, 0.0f, -(step * float(n - 1) / 2.0f)));
        submitItem(itemQuads_, feet, m, light);
        m = glm::translate(m, glm::vec3(0.0f, 0.0f, step));
        for (int j = 1; j < n; ++j) {
            const glm::vec3 o((r.nextFloat() * 2.0f - 1.0f) * 0.15f * 0.5f, (r.nextFloat() * 2.0f - 1.0f) * 0.15f * 0.5f, 0.0f);
            submitItem(itemQuads_, feet, glm::translate(m, o), light);
            m = glm::translate(m, glm::vec3(0.0f, 0.0f, step));
        }
    }
    if (!at) submitShadow(level, feet, 0.15f, skyDarken, 0.75f);
}

void EntityRenderer::submitExperienceOrb(const ExperienceOrb& orb, const Level& level, float partialTick,
                                         int skyDarken, const glm::dvec3* at) {
    const glm::dvec3 feet = at ? *at : glm::mix(orb.oldPos, orb.pos, double(partialTick));
    // Orbs glow: block light + 7.
    const uint8_t l = level.lightAt(BlockPos::containing(feet + glm::dvec3(0.0, 0.25, 0.0)));
    const glm::vec2 light(float(std::min(15, (l & 15) + 7)), float(l >> 4));
    const int icon = orb.icon();
    const float u0 = float(icon % 4 * 16) / 64.0f, u1 = float(icon % 4 * 16 + 16) / 64.0f;
    const float v0 = float(icon / 4 * 16) / 64.0f, v1 = float(icon / 4 * 16 + 16) / 64.0f;
    const float t = (float(orb.tickCount) + partialTick) / 2.0f;
    const auto red = uint32_t((std::sin(t) + 1.0f) * 0.5f * 255.0f), blue = uint32_t((std::sin(t + 4.1887903f) + 1.0f) * 0.1f * 255.0f);
    const uint32_t color = red | (255u << 8) | (blue << 16) | (128u << 24); // RGBA8, half transparent
    // A 0.3-block quad, 0.1 up, from (-0.5, -0.25) to (0.5, 0.75) of its size.
    const glm::vec3 c = glm::vec3(feet - cameraPos_) + glm::vec3(0.0f, 0.1f, 0.0f);
    const glm::vec3 rx = cameraRight_ * 0.3f, uy = cameraUp_ * 0.3f;
    const size_t first = vertices_.size();
    const Vertex v[4] = {{c - rx * 0.5f - uy * 0.25f, {u0, v1}, color, light},
                         {c + rx * 0.5f - uy * 0.25f, {u1, v1}, color, light},
                         {c + rx * 0.5f + uy * 0.75f, {u1, v0}, color, light},
                         {c - rx * 0.5f + uy * 0.75f, {u0, v0}, color, light}};
    for (int i : {0, 1, 2, 0, 2, 3}) vertices_.push_back(v[i]);
    addBatch(&texture("minecraft:entity/experience_orb"), first, Pass::Translucent);
    if (!at) submitShadow(level, feet, 0.15f, skyDarken, 0.75f);
}

void EntityRenderer::submitPlayer(const Player& player, const Level& level, float partialTick, int skyDarken) {
    const bool spectator = player.mode() == GameMode::Spectator;
    HumanoidPose pose = humanoidPoseOf(player, partialTick);
    pose.crouching = player.crouching;
    pose.rightArmItem = !player.inventory.selectedStack().empty();
    pose.leftArmItem = !player.inventory.offhand.empty();
    pose.bowDrawn = player.usingItem() && player.useItem->id == "minecraft:bow" && !player.useOffhand;
    pose.bodyHidden = spectator; // PlayerModel: spectators are a floating head
    resetPose(playerPosed_.root, playerRest_.root);
    animateHumanoid(playerPosed_.root, pose);
    const glm::dvec3 feet = glm::mix(player.oldPos, player.pos, double(partialTick));
    // PlayerRenderer: scale 0.9375, crouching sits 2/16 lower; the dead fall over.
    const float deathTime = player.deathTime > 0 ? float(player.deathTime) + partialTick : 0.0f;
    glm::mat4 m = livingModelTransform(pose.bodyYaw, 0.9375f, player.crouching ? -0.125f : 0.0f,
                                       deathFlipDegrees(deathTime));
    if (player.sleeping()) {
        // Vanilla setupRotations for Pose.SLEEPING: laid along the bed, head on the pillow.
        // The feet (the model's origin) sit at the head block, so the body is drawn the
        // standing eye height less 0.1 toward the foot.
        const BlockRegistry& reg = level.registry();
        const Direction facing = directionFromName(reg.value(level.getBlock(*player.sleepingPos), "facing")).value_or(Direction::South);
        const float rot = facing == Direction::South ? 90.0f : facing == Direction::West ? 0.0f
                          : facing == Direction::North ? 270.0f : 180.0f;
        const glm::vec3 n(normal(facing));
        m = glm::translate(glm::mat4(1.0f), glm::vec3(-n.x * 1.52f, 0.0f, -n.z * 1.52f));
        m = glm::rotate(m, glm::radians(rot), glm::vec3(0, 1, 0));
        m = glm::rotate(m, glm::radians(90.0f), glm::vec3(0, 0, 1));
        m = glm::rotate(m, glm::radians(270.0f), glm::vec3(0, 1, 0));
        m = glm::scale(m, glm::vec3(-0.9375f, -0.9375f, 0.9375f));
        m = glm::translate(m, glm::vec3(0.0f, -1.501f, 0.0f));
    }
    // Lit at the eyes (vanilla light probe position).
    const uint8_t l = level.lightAt(BlockPos::containing(feet + glm::dvec3(0.0, player.eyeHeight(), 0.0)));
    // Spectators are invisible: their own view shows them at 15% opacity and without a shadow.
    const glm::vec2 light(float(l & 15), float(l >> 4));
    submitModel(playerPosed_.root, playerRest_.texWidth, playerRest_.texHeight, "minecraft:entity/player/wide/steve",
                feet, m, light, 0xFFFFFF, spectator ? 38.0f / 255.0f : 1.0f,
                (player.hurtTime > 0 || player.deathTime > 0) ? kHurtOverlay : 0u);
    if (spectator) return;
    // HumanoidArmorLayer, then ItemInHandLayer: each held item hangs from its arm (a bow
    // being drawn shows its pull).
    submitArmor(player.inventory.armor, playerPosed_.root, false, feet, m, light,
                (player.hurtTime > 0 || player.deathTime > 0) ? kHurtOverlay : 0u);
    for (const bool left : {false, true}) {
        const ItemStack& stack = left ? player.inventory.offhand : player.inventory.selectedStack();
        const bool using_ = player.usingItem() && player.useOffhand == left;
        submitHeldItem(stack, *playerPosed_.root.find(left ? "left_arm" : "right_arm"), left, 0.0f, feet, m, light,
                       player.yaw, using_ ? player.useItem->useTicks > 0 && player.useItem->id == "minecraft:bow"
                                                ? 72000 - player.useItemRemaining
                                                : 0
                                          : 0);
    }
    if (player.remainingFireTicks > 0 && !player.invulnerable()) submitFlame(feet, player.width, player.height);
    submitShadow(level, feet, 0.5f, skyDarken);
}

void EntityRenderer::render(const glm::mat4& viewProj, const ChunkRenderer::Fog& fog) {
    if (batches_.empty() && glow_.empty()) return;
    glNamedBufferData(vbo_, GLsizeiptr(vertices_.size() * sizeof(Vertex)), vertices_.data(), GL_STREAM_DRAW);
    glVertexArrayVertexBuffer(vao_, 0, vbo_, 0, sizeof(Vertex));
    glBindVertexArray(vao_);
    shader_.use();
    shader_.set("uViewProj", viewProj);
    shader_.set("uFogColor", fog.color);
    shader_.set("uFog", fog.distances);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    for (Pass pass : {Pass::Cutout, Pass::Shadow, Pass::Translucent, Pass::Additive}) {
        // Cutout models are drawn without culling (vanilla entityCutoutNoCull), translucent
        // ones with it (itemEntityTranslucentCull); shadows don't write depth. Additive ones
        // (vanilla eyes, energy swirls) add their color, fading to nothing in the fog.
        if (pass == Pass::Translucent || pass == Pass::Additive) glEnable(GL_CULL_FACE);
        else glDisable(GL_CULL_FACE);
        if (pass == Pass::Cutout) {
            glDisable(GL_BLEND);
        } else if (pass == Pass::Additive) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
            shader_.set("uFogColor", glm::vec4(0.0f, 0.0f, 0.0f, fog.color.a));
        } else {
            glEnable(GL_BLEND);
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        }
        glDepthMask(pass == Pass::Shadow || pass == Pass::Additive ? GL_FALSE : GL_TRUE);
        shader_.set("uAlphaCutoff", pass == Pass::Shadow ? -1.0f : pass == Pass::Additive ? 0.0f : 0.1f);
        for (const Batch& b : batches_) {
            if (b.pass != pass) continue;
            b.texture->bind(0);
            glDrawArrays(GL_TRIANGLES, GLint(b.first), GLsizei(b.count));
        }
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    if (!glow_.empty()) {
        // Vanilla RenderType.lightning: additive (src alpha, one), depth tested, writing depth.
        glNamedBufferData(glowVbo_, GLsizeiptr(glow_.size() * sizeof(GlowVertex)), glow_.data(), GL_STREAM_DRAW);
        glVertexArrayVertexBuffer(glowVao_, 0, glowVbo_, 0, sizeof(GlowVertex));
        glBindVertexArray(glowVao_);
        glowShader_.use();
        glowShader_.set("uViewProj", viewProj);
        glowShader_.set("uFog", fog.distances);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDrawArrays(GL_TRIANGLES, 0, GLsizei(glow_.size()));
        glDisable(GL_BLEND);
    }
}

} // namespace mc
