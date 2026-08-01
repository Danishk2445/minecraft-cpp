#include "mobs.h"
#include "world.h"
#include "shader.h"
#include "texture.h"
#include "noise.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

// ------------------------------------------------------------- model data
// Boxes are in Minecraft texture pixels (16 px = 1 block), y up, model
// facing -Z. Box coords are relative to the part pivot; pivot is relative to
// the entity origin (feet center).

enum Role { R_BODY, R_HEAD, R_LEG0, R_LEG1, R_LEG2, R_LEG3, R_ARM_R, R_ARM_L };

// `mirror` flips the box across its part's x axis after unwrapping, matching
// vanilla's mirrored left limbs: give it the right limb's box and UVs.
struct BoxSpec { float x, y, z, w, h, d; int u, v; bool mirror = false; };
struct PartSpec {
    int role;
    float px, py, pz;   // pivot (pixels)
    float preRotX;      // constant rotation baked into vertices (degrees)
    std::vector<BoxSpec> boxes;
};
struct MobDef {
    const char* texture;
    int texW, texH;
    float halfW, height;  // hitbox (blocks)
    float maxHealth;
    std::vector<PartSpec> parts;
};

const MobDef MOB_DEFS[MOB_TYPE_COUNT] = {
    // MOB_PIG (classic quadruped layout on a 64x64 canvas)
    {"entity/pig/temperate_pig.png", 64, 64, 0.45f, 0.9f, 10, {
        {R_HEAD, 0, 12, -6, 0, {{-4, -4, -8, 8, 8, 8, 0, 0},
                                {-2, -3, -9, 4, 3, 1, 16, 16}}}, // snout
        {R_BODY, 0, 13, 2, -90, {{-5, -6, -7, 10, 16, 8, 28, 8}}},
        {R_LEG0, -3, 6, -5, 0, {{-2, -6, -2, 4, 6, 4, 0, 16}}},
        {R_LEG1, 3, 6, -5, 0, {{-2, -6, -2, 4, 6, 4, 0, 16}}},
        {R_LEG2, -3, 6, 7, 0, {{-2, -6, -2, 4, 6, 4, 0, 16}}},
        {R_LEG3, 3, 6, 7, 0, {{-2, -6, -2, 4, 6, 4, 0, 16}}},
    }},
    // MOB_COW (1.21.5 layout: snout box, horns at (22,0), udder at (52,0))
    {"entity/cow/temperate_cow.png", 64, 64, 0.45f, 1.4f, 10, {
        {R_HEAD, 0, 20, -8, 0, {{-4, -4, -6, 8, 8, 6, 0, 0},
                                {-3, -4, -8, 6, 3, 2, 0, 32},   // muzzle
                                {-5, 2, -4, 1, 3, 1, 22, 0},    // right horn
                                {4, 2, -4, 1, 3, 1, 22, 0}}},   // left horn
        {R_BODY, 0, 19, 2, -90, {{-6, -8, -7, 12, 18, 10, 18, 4},
                                 {-2, -8, -8, 4, 6, 1, 52, 0}}}, // udder
        {R_LEG0, -3, 12, -5, 0, {{-2, -12, -2, 4, 12, 4, 0, 16}}},
        {R_LEG1, 3, 12, -5, 0, {{-2, -12, -2, 4, 12, 4, 0, 16}}},
        {R_LEG2, -3, 12, 7, 0, {{-2, -12, -2, 4, 12, 4, 0, 16}}},
        {R_LEG3, 3, 12, 7, 0, {{-2, -12, -2, 4, 12, 4, 0, 16}}},
    }},
    // MOB_ZOMBIE (biped; zombie.png only paints the classic 64x32 regions, so
    // the left limbs are mirrored copies of the right ones like vanilla)
    {"entity/zombie/zombie.png", 64, 64, 0.3f, 1.95f, 20, {
        {R_HEAD, 0, 24, 0, 0, {{-4, 0, -4, 8, 8, 8, 0, 0}}},
        {R_BODY, 0, 24, 0, 0, {{-4, -12, -2, 8, 12, 4, 16, 16}}},
        {R_ARM_R, -5, 22, 0, 0, {{-3, -10, -2, 4, 12, 4, 40, 16}}},
        {R_ARM_L, 5, 22, 0, 0, {{-3, -10, -2, 4, 12, 4, 40, 16, true}}},
        {R_LEG0, -2, 12, 0, 0, {{-2, -12, -2, 4, 12, 4, 0, 16}}},
        {R_LEG1, 2, 12, 0, 0, {{-2, -12, -2, 4, 12, 4, 0, 16, true}}},
    }},
    // MOB_CREEPER (64x32)
    {"entity/creeper/creeper.png", 64, 32, 0.3f, 1.7f, 20, {
        {R_HEAD, 0, 18, 0, 0, {{-4, 0, -4, 8, 8, 8, 0, 0}}},
        {R_BODY, 0, 18, 0, 0, {{-4, -12, -2, 8, 12, 4, 16, 16}}},
        {R_LEG0, -2, 6, -4, 0, {{-2, -6, -2, 4, 6, 4, 0, 16}}},
        {R_LEG1, 2, 6, -4, 0, {{-2, -6, -2, 4, 6, 4, 0, 16}}},
        {R_LEG2, -2, 6, 4, 0, {{-2, -6, -2, 4, 6, 4, 0, 16}}},
        {R_LEG3, 2, 6, 4, 0, {{-2, -6, -2, 4, 6, 4, 0, 16}}},
    }},
};

// ------------------------------------------------------------- gpu model

struct PartMesh {
    int role;
    glm::vec3 pivot;
    GLint first;
    GLsizei count;
};
struct GpuModel {
    GLuint vao = 0, vbo = 0, tex = 0;
    std::vector<PartMesh> parts;
};
GpuModel g_models[MOB_TYPE_COUNT];
Shader g_shader;

const char* ENT_VS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in float aShade;
uniform mat4 uVP;
uniform mat4 uModel;
uniform vec3 uCam;
out vec2 vUV; out float vShade; out float vDist;
void main(){
    vec4 wp = uModel * vec4(aPos, 1.0);
    gl_Position = uVP * wp;
    vUV = aUV; vShade = aShade;
    vDist = distance(wp.xyz, uCam);
})";

const char* ENT_FS = R"(#version 330 core
in vec2 vUV; in float vShade; in float vDist;
uniform sampler2D uTex;
uniform float uSky, uBlk, uSun;
uniform vec4 uTint;
uniform vec3 uFog;
uniform float uFogStart, uFogEnd;
out vec4 o;
void main(){
    vec4 c = texture(uTex, vUV);
    if (c.a < 0.5) discard;
    float bs = pow(uSky * uSun, 1.5);
    float bb = pow(uBlk, 1.2);
    vec3 lum = clamp(vec3(bs) + bb * vec3(1.15, 0.85, 0.5), vec3(0.028), vec3(1.0));
    vec3 rgb = c.rgb * lum * vShade;
    rgb = mix(rgb, uTint.rgb, uTint.a);
    float f = clamp((vDist - uFogStart) / (uFogEnd - uFogStart), 0.0, 1.0);
    o = vec4(mix(rgb, uFog, f), 1.0);
})";

float shadeForNormal(glm::vec3 n) {
    if (std::fabs(n.y) > 0.5f) return n.y > 0 ? 1.0f : 0.55f;
    if (std::fabs(n.x) > 0.5f) return 0.65f;
    return 0.82f;
}

// Emit one textured quad (two triangles): corners TL,TR,BR,BL with the
// texture rect (u0,v0,uw,vh) in pixels.
void quadPx(std::vector<float>& out, glm::vec3 TL, glm::vec3 TR, glm::vec3 BR, glm::vec3 BL,
            float u0, float v0, float uw, float vh, float tw, float th, glm::vec3 normal) {
    float s = shadeForNormal(normal);
    glm::vec3 p[6] = {TL, TR, BR, TL, BR, BL};
    float uv[6][2] = {{u0, v0}, {u0 + uw, v0}, {u0 + uw, v0 + vh},
                      {u0, v0}, {u0 + uw, v0 + vh}, {u0, v0 + vh}};
    for (int i = 0; i < 6; i++)
        out.insert(out.end(), {p[i].x, p[i].y, p[i].z, uv[i][0] / tw, uv[i][1] / th, s});
}

void buildBox(std::vector<float>& out, const BoxSpec& b, float rotXdeg, float tw, float th) {
    float x0 = b.x, y0 = b.y, z0 = b.z, x1 = b.x + b.w, y1 = b.y + b.h, z1 = b.z + b.d;
    float u = (float)b.u, v = (float)b.v, w = b.w, h = b.h, d = b.d;
    std::vector<float> tmp;
    // front (-Z)
    quadPx(tmp, {x1, y1, z0}, {x0, y1, z0}, {x0, y0, z0}, {x1, y0, z0}, u + d, v + d, w, h, tw, th, {0, 0, -1});
    // back (+Z)
    quadPx(tmp, {x0, y1, z1}, {x1, y1, z1}, {x1, y0, z1}, {x0, y0, z1}, u + 2 * d + w, v + d, w, h, tw, th, {0, 0, 1});
    // -X side
    quadPx(tmp, {x0, y1, z0}, {x0, y1, z1}, {x0, y0, z1}, {x0, y0, z0}, u, v + d, d, h, tw, th, {-1, 0, 0});
    // +X side
    quadPx(tmp, {x1, y1, z1}, {x1, y1, z0}, {x1, y0, z0}, {x1, y0, z1}, u + d + w, v + d, d, h, tw, th, {1, 0, 0});
    // top (+Y): texture bottom edge = front edge
    quadPx(tmp, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}, u + d, v, w, d, tw, th, {0, 1, 0});
    // bottom (-Y)
    quadPx(tmp, {x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, u + d + w, v, w, d, tw, th, {0, -1, 0});

    // mirrored limb: reflect across the part's x axis. The unwrap then lands on
    // the correct sides (outer face outward) exactly like vanilla's mirror flag;
    // winding flips but mobs render with culling off, and ±X shade is equal.
    if (b.mirror)
        for (size_t i = 0; i + 5 < tmp.size(); i += 6) tmp[i] = -tmp[i];

    // bake the constant part rotation (quadruped bodies) into positions+shade
    if (rotXdeg != 0) {
        float a = glm::radians(rotXdeg), c = std::cos(a), s = std::sin(a);
        for (size_t i = 0; i + 5 < tmp.size(); i += 6) {
            float y = tmp[i + 1], z = tmp[i + 2];
            tmp[i + 1] = y * c - z * s;
            tmp[i + 2] = y * s + z * c;
        }
        // recompute shade per triangle-quad group of 6 verts from new normal
        for (size_t i = 0; i + 35 < tmp.size(); i += 36) {
            glm::vec3 A(tmp[i], tmp[i + 1], tmp[i + 2]);
            glm::vec3 B(tmp[i + 6], tmp[i + 7], tmp[i + 8]);
            glm::vec3 C(tmp[i + 12], tmp[i + 13], tmp[i + 14]);
            glm::vec3 n = glm::normalize(glm::cross(B - A, C - A));
            float s2 = shadeForNormal(n);
            for (int k = 0; k < 6; k++) tmp[i + k * 6 + 5] = s2;
        }
    }
    out.insert(out.end(), tmp.begin(), tmp.end());
}

uint32_t g_rng = 12345;
float frand() { g_rng = hashU32(g_rng); return (g_rng & 0xffffff) / 16777216.0f; }

// ------------------------------------------------------------- physics

bool moveMob(World& w, glm::vec3& pos, glm::vec3& vel, float hw, float hgt, int axis, float amount) {
    const float EPS = 1e-4f;
    bool hit = false;
    int steps = std::max(1, (int)std::ceil(std::abs(amount) / 0.4f));
    float step = amount / steps;
    for (int s = 0; s < steps && !hit; s++) {
        pos[axis] += step;
        int x0 = (int)std::floor(pos.x - hw), x1 = (int)std::floor(pos.x + hw - EPS);
        int y0 = (int)std::floor(pos.y), y1 = (int)std::floor(pos.y + hgt - EPS);
        int z0 = (int)std::floor(pos.z - hw), z1 = (int)std::floor(pos.z + hw - EPS);
        for (int y = y0; y <= y1 && !hit; y++)
            for (int z = z0; z <= z1 && !hit; z++)
                for (int x = x0; x <= x1 && !hit; x++) {
                    if (!blockSolid(w.block(x, y, z))) continue;
                    hit = true;
                    if (axis == 0) pos.x = step > 0 ? x - hw - EPS : x + 1 + hw + EPS;
                    if (axis == 1) pos.y = step > 0 ? y - hgt - EPS : (float)(y + 1);
                    if (axis == 2) pos.z = step > 0 ? z - hw - EPS : z + 1 + hw + EPS;
                    vel[axis] = 0;
                }
    }
    return hit;
}

bool rayAABB(glm::vec3 o, glm::vec3 d, glm::vec3 mn, glm::vec3 mx, float& tOut) {
    float t0 = 0, t1 = 1e30f;
    for (int i = 0; i < 3; i++) {
        float inv = 1.0f / (std::fabs(d[i]) > 1e-9f ? d[i] : 1e-9f);
        float ta = (mn[i] - o[i]) * inv, tb = (mx[i] - o[i]) * inv;
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
        if (t0 > t1) return false;
    }
    tOut = t0;
    return true;
}

float angleLerp(float a, float b, float t) {
    float diff = std::fmod(b - a + 540.0f, 360.0f) - 180.0f;
    return a + diff * t;
}

} // namespace

void mobSize(int type, float& halfW, float& height) {
    halfW = MOB_DEFS[type].halfW;
    height = MOB_DEFS[type].height;
}

void Mobs::init(const std::string& texDir) {
    g_shader = Shader(ENT_VS, ENT_FS);
    for (int t = 0; t < MOB_TYPE_COUNT; t++) {
        const MobDef& def = MOB_DEFS[t];
        GpuModel& gm = g_models[t];
        gm.tex = loadTexture2D(texDir + "/" + def.texture);
        std::vector<float> verts;
        for (const PartSpec& ps : def.parts) {
            PartMesh pm;
            pm.role = ps.role;
            pm.pivot = glm::vec3(ps.px, ps.py, ps.pz);
            pm.first = (GLint)(verts.size() / 6);
            for (const BoxSpec& b : ps.boxes)
                buildBox(verts, b, ps.preRotX, (float)def.texW, (float)def.texH);
            pm.count = (GLsizei)(verts.size() / 6 - pm.first);
            gm.parts.push_back(pm);
        }
        glGenVertexArrays(1, &gm.vao);
        glGenBuffers(1, &gm.vbo);
        glBindVertexArray(gm.vao);
        glBindBuffer(GL_ARRAY_BUFFER, gm.vbo);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
        GLsizei stride = 6 * sizeof(float);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (void*)(5 * sizeof(float)));
        glBindVertexArray(0);
    }
}

void Mobs::spawnAt(int type, glm::vec3 pos) {
    Mob m;
    m.type = type;
    m.pos = pos;
    m.health = MOB_DEFS[type].maxHealth;
    m.yaw = frand() * 360.0f;
    mobs.push_back(m);
}

void Mobs::hurt(int idx, float dmg, glm::vec3 fromDir) {
    Mob& m = mobs[idx];
    if (m.dead) return;
    m.health -= dmg;
    m.hurtTime = 0.5f;
    glm::vec3 kb = glm::normalize(glm::vec3(fromDir.x, 0, fromDir.z) + glm::vec3(1e-5f));
    m.vel += kb * 6.5f + glm::vec3(0, 5.0f, 0);
    if (m.health <= 0) m.dead = true;
    // passive mobs flee
    if (m.type == MOB_PIG || m.type == MOB_COW) {
        m.state = 2;
        m.aiTimer = 3.0f;
        m.target = m.pos + kb * 10.0f;
    }
}

bool Mobs::raycast(glm::vec3 o, glm::vec3 d, float maxDist, int& idx, float& tOut) const {
    float best = maxDist;
    idx = -1;
    for (size_t i = 0; i < mobs.size(); i++) {
        const Mob& m = mobs[i];
        if (m.dead) continue;
        float hw, h;
        mobSize(m.type, hw, h);
        glm::vec3 mn = m.pos - glm::vec3(hw, 0, hw);
        glm::vec3 mx = m.pos + glm::vec3(hw, h, hw);
        float t;
        if (rayAABB(o, d, mn, mx, t) && t < best) { best = t; idx = (int)i; }
    }
    tOut = best;
    return idx >= 0;
}

void Mobs::explode(World& w, glm::vec3 c, float radius, int sourceIdx) {
    int r = (int)std::ceil(radius) + 1;
    int cx = (int)std::floor(c.x), cy = (int)std::floor(c.y), cz = (int)std::floor(c.z);
    for (int y = cy - r; y <= cy + r; y++)
        for (int z = cz - r; z <= cz + r; z++)
            for (int x = cx - r; x <= cx + r; x++) {
                float dx = x + 0.5f - c.x, dy = y + 0.5f - c.y, dz = z + 0.5f - c.z;
                float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (dist > radius * (0.8f + 0.35f * hash3f(x, y, z, 999))) continue;
                uint8_t b = w.block(x, y, z);
                if (b == B_AIR || b == B_BEDROCK || b == B_WATER) continue;
                w.setBlock(x, y, z, B_AIR);
            }
    // damage mobs by proximity
    for (size_t i = 0; i < mobs.size(); i++) {
        if ((int)i == sourceIdx || mobs[i].dead) continue;
        float d = glm::distance(mobs[i].pos, c);
        if (d < radius * 2.0f) hurt((int)i, 24.0f * (1.0f - d / (radius * 2.0f)), mobs[i].pos - c);
    }
    if (explosionAtPlayer) explosionAtPlayer(c, radius);
}

void Mobs::updateOne(World& w, Mob& m, glm::vec3 playerPos, float dt, float sunF) {
    const MobDef& def = MOB_DEFS[m.type];
    if (m.dead) { m.deathTime += dt; return; }
    m.hurtTime = std::max(0.0f, m.hurtTime - dt);
    m.attackCooldown = std::max(0.0f, m.attackCooldown - dt);
    if (m.frozen) return;

    // freeze in unloaded chunks
    Chunk* c = w.chunk((int)std::floor(m.pos.x) >> 4, (int)std::floor(m.pos.z) >> 4);
    if (!c || !c->generated) return;

    glm::vec3 toPlayer = playerPos - m.pos;
    float distP = glm::length(glm::vec2(toPlayer.x, toPlayer.z));
    bool inWater = g_blocks[w.block((int)std::floor(m.pos.x), (int)std::floor(m.pos.y + 0.3f),
                                    (int)std::floor(m.pos.z))].fluid;

    float wantSpeed = 0;
    float wantYaw = m.yaw;

    m.aiTimer -= dt;
    if (m.type == MOB_PIG || m.type == MOB_COW) {
        if (m.state == 0) { // idle
            if (m.aiTimer <= 0) {
                m.state = 1;
                m.aiTimer = 4.0f + frand() * 4.0f;
                float a = frand() * 6.2831853f;
                m.target = m.pos + glm::vec3(std::cos(a), 0, std::sin(a)) * (3.0f + frand() * 6.0f);
            }
        } else { // wander / flee
            glm::vec3 to = m.target - m.pos;
            wantYaw = glm::degrees(std::atan2(to.z, to.x));
            wantSpeed = m.state == 2 ? 3.2f : 1.3f;
            if (glm::length(glm::vec2(to.x, to.z)) < 0.8f || m.aiTimer <= 0) {
                m.state = 0;
                m.aiTimer = 2.0f + frand() * 4.0f;
            }
        }
        // occasional head wobble
        m.headYaw = angleLerp(m.headYaw, wantSpeed > 0 ? 0 : std::sin((float)m.aiTimer * 0.7f) * 30.0f, 1 - std::exp(-3 * dt));
    } else if (m.type == MOB_ZOMBIE) {
        // burn in direct sunlight
        int hy = (int)std::floor(m.pos.y + def.height * 0.9f);
        if (sunF > 0.6f && w.skyLight((int)std::floor(m.pos.x), hy, (int)std::floor(m.pos.z)) >= 14) {
            m.burnAccum += dt;
            if (m.burnAccum > 0.7f) { m.burnAccum = 0; m.hurtTime = 0.4f; m.health -= 3; if (m.health <= 0) m.dead = true; }
        }
        if (distP < 20.0f) {
            wantYaw = glm::degrees(std::atan2(toPlayer.z, toPlayer.x));
            wantSpeed = 2.2f;
            float d3 = glm::length(toPlayer + glm::vec3(0, -1.0f, 0));
            if (d3 < 1.7f && m.attackCooldown <= 0) {
                m.attackCooldown = 1.2f;
                if (damagePlayer) damagePlayer(3.0f, glm::normalize(glm::vec3(toPlayer.x, 0, toPlayer.z) + glm::vec3(1e-5f)) * 7.0f + glm::vec3(0, 4.0f, 0));
            }
            m.headPitch = glm::degrees(std::atan2(toPlayer.y, distP)) * 0.5f;
        } else if (m.aiTimer <= 0) {
            m.aiTimer = 3.0f + frand() * 3.0f;
            m.yaw += (frand() - 0.5f) * 180.0f;
        } else if (m.aiTimer > 1.5f) {
            wantSpeed = 0.8f;
        }
    } else if (m.type == MOB_CREEPER) {
        float d3 = glm::length(toPlayer);
        if (d3 < 2.8f) {
            m.fuse += dt;
            wantSpeed = 0;
            if (m.fuse >= 1.5f) {
                m.dead = true;
                m.deathTime = 99; // no corpse
                explode(w, m.pos + glm::vec3(0, 0.6f, 0), 2.8f, (int)(&m - mobs.data()));
                return;
            }
        } else {
            m.fuse = std::max(0.0f, m.fuse - dt * 1.5f);
            if (distP < 14.0f) {
                wantYaw = glm::degrees(std::atan2(toPlayer.z, toPlayer.x));
                wantSpeed = 2.0f;
            } else if (m.aiTimer <= 0) {
                m.aiTimer = 4.0f + frand() * 3.0f;
                m.yaw += (frand() - 0.5f) * 160.0f;
            }
        }
        if (distP < 14.0f) wantYaw = glm::degrees(std::atan2(toPlayer.z, toPlayer.x));
    }

    // hostile mobs aim their head at the player
    if ((m.type == MOB_ZOMBIE || m.type == MOB_CREEPER) && distP < 20.0f) {
        float desired = glm::degrees(std::atan2(toPlayer.z, toPlayer.x));
        m.headYaw = std::clamp(std::fmod(desired - m.yaw + 540.0f, 360.0f) - 180.0f, -70.0f, 70.0f);
    }

    // steering + locomotion
    m.yaw = angleLerp(m.yaw, wantYaw, 1 - std::exp(-6 * dt));
    glm::vec3 fwd(std::cos(glm::radians(m.yaw)), 0, std::sin(glm::radians(m.yaw)));
    float sp = wantSpeed * (inWater ? 0.6f : 1.0f);
    float k = 1 - std::exp(-(m.onGround ? 12.0f : 3.0f) * dt);
    m.vel.x += (fwd.x * sp - m.vel.x) * k;
    m.vel.z += (fwd.z * sp - m.vel.z) * k;

    if (inWater) {
        m.vel.y += (1.5f - m.vel.y) * (1 - std::exp(-4 * dt));
    } else {
        m.vel.y -= 32.0f * dt;
        m.vel.y = std::max(m.vel.y, -60.0f);
    }

    glm::vec3 before = m.pos;
    bool falling = m.vel.y <= 0;
    m.onGround = false;
    if (moveMob(w, m.pos, m.vel, def.halfW, def.height, 1, m.vel.y * dt) && falling)
        m.onGround = true;
    bool hitX = moveMob(w, m.pos, m.vel, def.halfW, def.height, 0, m.vel.x * dt);
    bool hitZ = moveMob(w, m.pos, m.vel, def.halfW, def.height, 2, m.vel.z * dt);

    // jump up 1-block steps when walking into a wall
    if ((hitX || hitZ) && m.onGround && wantSpeed > 0.1f) m.vel.y = 8.0f;

    // walk animation
    float moved = glm::length(glm::vec2(m.pos.x - before.x, m.pos.z - before.z));
    float speedNow = moved / std::max(dt, 1e-5f);
    m.legAmp += (std::min(speedNow / 3.0f, 1.0f) - m.legAmp) * (1 - std::exp(-8 * dt));
    m.legPhase += speedNow * dt * 2.4f;

    if (m.pos.y < -10) m.dead = true; // fell out of the world
}

void Mobs::trySpawn(World& w, glm::vec3 p, float sunF) {
    int passive = 0, hostile = 0;
    for (const Mob& m : mobs) {
        if (m.dead) continue;
        if (m.type == MOB_PIG || m.type == MOB_COW) passive++;
        else hostile++;
    }
    float nightF = 1.0f - std::clamp((sunF - 0.14f) / 0.86f, 0.0f, 1.0f);
    for (int attempt = 0; attempt < 3; attempt++) {
        bool wantHostile = frand() < (0.25f + 0.55f * nightF);
        if (wantHostile && hostile >= 8) continue;
        if (!wantHostile && passive >= 10) continue;
        float a = frand() * 6.2831853f;
        float dist = 24.0f + frand() * 20.0f;
        int wx = (int)std::floor(p.x + std::cos(a) * dist);
        int wz = (int)std::floor(p.z + std::sin(a) * dist);
        int top = w.topSolidY(wx, wz);
        if (top < 1) continue;

        int type;
        int sy;
        if (wantHostile) {
            type = frand() < 0.72f ? MOB_ZOMBIE : MOB_CREEPER;
            sy = (frand() < 0.5f) ? top + 1 : 5 + (int)(frand() * std::max(1, top - 8));
        } else {
            type = frand() < 0.5f ? MOB_PIG : MOB_COW;
            sy = top + 1;
        }
        float hw, h;
        mobSize(type, hw, h);
        uint8_t below = w.block(wx, sy - 1, wz);
        uint8_t at = w.block(wx, sy, wz);
        uint8_t above = w.block(wx, sy + 1, wz);
        if (!blockSolid(below) || blockSolid(at) || blockSolid(above)) continue;
        if (g_blocks[at].fluid || g_blocks[above].fluid) continue;
        if (wantHostile) {
            // only spawn in darkness (block light + sky light scaled by daylight)
            float eff = std::max((float)w.blockLight(wx, sy, wz),
                                 w.skyLight(wx, sy, wz) * (1.0f - nightF));
            if (eff > 3.0f) continue;
        } else {
            if (below != B_GRASS && below != B_SNOWY_GRASS) continue;
            if (w.skyLight(wx, sy, wz) < 8) continue;
        }
        int group = wantHostile ? 1 : 1 + (int)(frand() * 2.99f);
        for (int g = 0; g < group; g++)
            spawnAt(type, glm::vec3(wx + 0.5f + (frand() - 0.5f) * 2 * g, (float)sy + 0.01f, wz + 0.5f + (frand() - 0.5f) * 2 * g));
        return;
    }
}

void Mobs::update(World& w, glm::vec3 playerPos, float dt, float sunF, bool naturalSpawns) {
    if (naturalSpawns) {
        spawnTimer_ -= dt;
        if (spawnTimer_ <= 0 && mobs.size() < 40) {
            spawnTimer_ = 0.5f;
            trySpawn(w, playerPos, sunF);
        }
    }
    for (Mob& m : mobs) updateOne(w, m, playerPos, dt, sunF);
    // remove finished corpses and far-away mobs
    for (size_t i = 0; i < mobs.size();) {
        bool remove = (mobs[i].dead && mobs[i].deathTime > 0.9f) ||
                      glm::distance(glm::vec2(mobs[i].pos.x, mobs[i].pos.z),
                                    glm::vec2(playerPos.x, playerPos.z)) > 56.0f;
        if (remove) { mobs[i] = mobs.back(); mobs.pop_back(); }
        else i++;
    }
}

void Mobs::render(const glm::mat4& vp, const glm::vec3& eye, World& w, float sunF,
                  const glm::vec3& fog, float fogStart, float fogEnd) {
    glDisable(GL_CULL_FACE);
    g_shader.use();
    g_shader.set("uVP", vp);
    g_shader.set("uCam", eye);
    g_shader.set("uTex", 0);
    g_shader.set("uSun", sunF);
    g_shader.set("uFog", fog);
    g_shader.set("uFogStart", fogStart);
    g_shader.set("uFogEnd", fogEnd);
    glActiveTexture(GL_TEXTURE0);

    for (const Mob& m : mobs) {
        const MobDef& def = MOB_DEFS[m.type];
        const GpuModel& gm = g_models[m.type];
        int lx = (int)std::floor(m.pos.x), lz = (int)std::floor(m.pos.z);
        int ly = (int)std::floor(m.pos.y + def.height * 0.5f);
        g_shader.set("uSky", w.skyLight(lx, ly, lz) / 15.0f);
        g_shader.set("uBlk", w.blockLight(lx, ly, lz) / 15.0f);

        glm::vec4 tint(0);
        if (m.hurtTime > 0) tint = glm::vec4(1, 0.25f, 0.25f, 0.45f);
        if (m.type == MOB_CREEPER && m.fuse > 0 && ((int)(m.fuse / 0.12f)) % 2 == 0)
            tint = glm::vec4(1, 1, 1, 0.6f);
        g_shader.set("uTint", tint);

        float roll = m.dead ? std::min(m.deathTime / 0.4f, 1.0f) * 90.0f : 0.0f;
        glm::mat4 ent = glm::translate(glm::mat4(1), m.pos);
        ent = glm::rotate(ent, glm::radians(-(m.yaw + 90.0f)), glm::vec3(0, 1, 0));
        if (roll > 0) ent = glm::rotate(ent, glm::radians(roll), glm::vec3(0, 0, 1));
        ent = glm::scale(ent, glm::vec3(1.0f / 16.0f));

        glBindTexture(GL_TEXTURE_2D, gm.tex);
        glBindVertexArray(gm.vao);
        for (const PartMesh& pm : gm.parts) {
            glm::mat4 part = glm::translate(glm::mat4(1), pm.pivot);
            float swing = std::sin(m.legPhase) * 0.7f * m.legAmp;
            switch (pm.role) {
                case R_HEAD:
                    part = glm::rotate(part, glm::radians(m.headYaw), glm::vec3(0, -1, 0));
                    part = glm::rotate(part, glm::radians(m.headPitch), glm::vec3(1, 0, 0));
                    break;
                case R_LEG0: case R_LEG3:
                    part = glm::rotate(part, swing, glm::vec3(1, 0, 0));
                    break;
                case R_LEG1: case R_LEG2:
                    part = glm::rotate(part, -swing, glm::vec3(1, 0, 0));
                    break;
                // +X rotation swings a hanging limb toward -Z, i.e. forward
                case R_ARM_R:
                    part = glm::rotate(part, glm::radians(80.0f) - swing * 0.15f, glm::vec3(1, 0, 0));
                    part = glm::rotate(part, glm::radians(-6.0f), glm::vec3(0, 1, 0));
                    break;
                case R_ARM_L:
                    part = glm::rotate(part, glm::radians(80.0f) + swing * 0.15f, glm::vec3(1, 0, 0));
                    part = glm::rotate(part, glm::radians(6.0f), glm::vec3(0, 1, 0));
                    break;
                default: break;
            }
            g_shader.set("uModel", ent * part);
            glDrawArrays(GL_TRIANGLES, pm.first, pm.count);
        }
    }
    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
}
