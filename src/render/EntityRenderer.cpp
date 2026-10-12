#include "render/EntityRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "core/Log.h"
#include "entity/Player.h"
#include "entity/model/EntityModels.h"
#include "entity/model/HumanoidModel.h"
#include "gfx/GL.h"
#include "entity/model/ItemGeometry.h"
#include "render/ClientAssets.h"
#include "resources/ResourcePack.h"
#include "world/level/Level.h"
#include "world/mesh/BlockEntityMeshes.h"

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
    playerRest_ = playerModel(false);
    playerPosed_ = playerRest_;
    return true;
}

const Texture& EntityRenderer::texture(std::string_view id) { return assets_->texture(id); }

void EntityRenderer::begin(const glm::dvec3& cameraPos) {
    cameraPos_ = cameraPos;
    vertices_.clear();
    batches_.clear();
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
                                 uint32_t tint, float alpha) {
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
        for (size_t k = 0; k < 4; ++k) v[k] = {base + q.pos[k], q.uv[k], color, light};
        for (int i : {0, 1, 2, 0, 2, 3}) vertices_.push_back(v[i]);
    }
    addBatch(&tex, first, alpha < 1.0f ? Pass::Translucent : Pass::Cutout);
}

void EntityRenderer::submitShadow(const Level& level, const glm::dvec3& feet, float radius, int skyDarken) {
    if (!shadows) return;
    const glm::dvec3 toCamera = feet - cameraPos_;
    const float strength = float(1.0 - glm::dot(toCamera, toCamera) / 256.0); // fades out at 16 blocks
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

void EntityRenderer::submitPlayer(const Player& player, const Level& level, float partialTick, int skyDarken) {
    const bool spectator = player.mode() == GameMode::Spectator;
    HumanoidPose pose = humanoidPoseOf(player, partialTick);
    pose.crouching = player.crouching;
    pose.rightArmItem = !player.inventory.selectedStack().empty();
    pose.leftArmItem = !player.inventory.offhand.empty();
    pose.bodyHidden = spectator; // PlayerModel: spectators are a floating head
    resetPose(playerPosed_.root, playerRest_.root);
    animateHumanoid(playerPosed_.root, pose);
    const glm::dvec3 feet = glm::mix(player.oldPos, player.pos, double(partialTick));
    // PlayerRenderer: scale 0.9375, crouching sits 2/16 lower.
    const glm::mat4 m = livingModelTransform(pose.bodyYaw, 0.9375f, player.crouching ? -0.125f : 0.0f);
    // Lit at the eyes (vanilla light probe position).
    const uint8_t l = level.lightAt(BlockPos::containing(feet + glm::dvec3(0.0, player.eyeHeight(), 0.0)));
    // Spectators are invisible: their own view shows them at 15% opacity and without a shadow.
    const glm::vec2 light(float(l & 15), float(l >> 4));
    submitModel(playerPosed_.root, playerRest_.texWidth, playerRest_.texHeight, "minecraft:entity/player/wide/steve",
                feet, m, light, 0xFFFFFF, spectator ? 38.0f / 255.0f : 1.0f);
    if (spectator) return;
    // ItemInHandLayer: each held item hangs from its arm.
    for (const bool left : {false, true}) {
        const ItemStack& stack = left ? player.inventory.offhand : player.inventory.selectedStack();
        if (stack.empty()) continue;
        const ModelPart& arm = *playerPosed_.root.find(left ? "left_arm" : "right_arm");
        glm::mat4 t = m * poseMatrix(arm.pose);
        t = glm::rotate(t, glm::radians(-90.0f), glm::vec3(1, 0, 0));
        t = glm::rotate(t, glm::radians(180.0f), glm::vec3(0, 1, 0));
        t = glm::translate(t, glm::vec3((left ? -1.0f : 1.0f) / 16.0f, 0.125f, -0.625f));
        ItemModelContext ctx;
        ctx.display = left ? DisplayContext::ThirdPersonLeft : DisplayContext::ThirdPersonRight;
        ctx.count = stack.count;
        ctx.maxStack = stack.maxStack();
        ctx.holderPos = feet;
        ctx.holderYaw = player.yaw;
        layers_.clear();
        assets_->itemDefinitions().evaluate(stack.item->id, ctx, &assets_->grassColormap(), layers_);
        itemQuads_.clear();
        buildItemQuads(layers_, ctx.display, assets_->blockEntityModels(), assets_->blockAtlas(), itemQuads_);
        submitItem(itemQuads_, feet, t, light);
    }
    submitShadow(level, feet, 0.5f, skyDarken);
}

void EntityRenderer::render(const glm::mat4& viewProj, const ChunkRenderer::Fog& fog) {
    if (batches_.empty()) return;
    glNamedBufferData(vbo_, GLsizeiptr(vertices_.size() * sizeof(Vertex)), vertices_.data(), GL_STREAM_DRAW);
    glVertexArrayVertexBuffer(vao_, 0, vbo_, 0, sizeof(Vertex));
    glBindVertexArray(vao_);
    shader_.use();
    shader_.set("uViewProj", viewProj);
    shader_.set("uFogColor", fog.color);
    shader_.set("uFog", fog.distances);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    for (Pass pass : {Pass::Cutout, Pass::Shadow, Pass::Translucent}) {
        // Cutout models are drawn without culling (vanilla entityCutoutNoCull), translucent
        // ones with it (itemEntityTranslucentCull); shadows don't write depth.
        if (pass == Pass::Translucent) glEnable(GL_CULL_FACE);
        else glDisable(GL_CULL_FACE);
        if (pass == Pass::Cutout) {
            glDisable(GL_BLEND);
        } else {
            glEnable(GL_BLEND);
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        }
        glDepthMask(pass == Pass::Shadow ? GL_FALSE : GL_TRUE);
        shader_.set("uAlphaCutoff", pass == Pass::Shadow ? -1.0f : 0.1f);
        for (const Batch& b : batches_) {
            if (b.pass != pass) continue;
            b.texture->bind(0);
            glDrawArrays(GL_TRIANGLES, GLint(b.first), GLsizei(b.count));
        }
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

} // namespace mc
