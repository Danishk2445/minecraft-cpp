#include "resources/ModelBaker.h"

#include <cmath>
#include <format>

#include <glm/gtc/matrix_transform.hpp>

namespace mc {
namespace {

// Vanilla FaceInfo corner order; each entry picks min (0) or max (1) per axis.
constexpr std::array<std::array<glm::ivec3, 4>, 6> kCorners = {{
    {{{0, 0, 1}, {0, 0, 0}, {1, 0, 0}, {1, 0, 1}}}, // down
    {{{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}}, // up
    {{{1, 1, 0}, {1, 0, 0}, {0, 0, 0}, {0, 1, 0}}}, // north
    {{{0, 1, 1}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}}}, // south
    {{{0, 1, 0}, {0, 0, 0}, {0, 0, 1}, {0, 1, 1}}}, // west
    {{{1, 1, 1}, {1, 0, 1}, {1, 0, 0}, {1, 1, 0}}}, // east
}};

glm::mat3 snap(glm::mat3 m) {
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r) m[c][r] = std::round(m[c][r]);
    return m;
}

glm::vec3 axisVector(Axis a) {
    switch (a) {
    case Axis::X: return {1, 0, 0};
    case Axis::Y: return {0, 1, 0};
    case Axis::Z: return {0, 0, 1};
    }
    return {0, 1, 0};
}

// The two in-plane coordinates of face d (shared by d and its opposite).
glm::vec2 facePlane(Direction d, const glm::vec3& p) {
    switch (axisOf(d)) {
    case Axis::Y: return {p.x, p.z};
    case Axis::Z: return {p.x, p.y};
    case Axis::X: return {p.z, p.y};
    }
    return {p.x, p.z};
}

} // namespace

uint16_t faceCellMask(Direction d, const std::array<glm::vec3, 4>& pos, bool overlap) {
    glm::vec2 lo(1e9f), hi(-1e9f);
    for (const auto& p : pos) {
        const glm::vec2 q = facePlane(d, p);
        lo = glm::min(lo, q);
        hi = glm::max(hi, q);
    }
    constexpr float eps = 1e-4f;
    uint16_t mask = 0;
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col) {
            const float u0 = col * 0.25f, u1 = u0 + 0.25f, v0 = row * 0.25f, v1 = v0 + 0.25f;
            const bool hit = overlap ? (u1 > lo.x + eps && u0 < hi.x - eps && v1 > lo.y + eps && v0 < hi.y - eps)
                                     : (u0 >= lo.x - eps && u1 <= hi.x + eps && v0 >= lo.y - eps && v1 <= hi.y + eps);
            if (hit) mask |= uint16_t(1u << (row * 4 + col));
        }
    return mask;
}

glm::mat3 blockStateRotation(int xDegrees, int yDegrees) {
    const glm::mat4 ry = glm::rotate(glm::mat4(1.0f), glm::radians(float(-yDegrees)), glm::vec3(0, 1, 0));
    const glm::mat4 rx = glm::rotate(glm::mat4(1.0f), glm::radians(float(-xDegrees)), glm::vec3(1, 0, 0));
    return snap(glm::mat3(ry) * glm::mat3(rx));
}

glm::vec3 faceUAxis(Direction d) {
    switch (d) {
    case Direction::Down: return {1, 0, 0};
    case Direction::Up: return {1, 0, 0};
    case Direction::North: return {-1, 0, 0};
    case Direction::South: return {1, 0, 0};
    case Direction::West: return {0, 0, 1};
    case Direction::East: return {0, 0, -1};
    }
    return {1, 0, 0};
}

glm::vec3 faceVAxis(Direction d) {
    switch (d) {
    case Direction::Down: return {0, 0, -1};
    case Direction::Up: return {0, 0, 1};
    default: return {0, -1, 0};
    }
}

std::array<float, 4> autoFaceUV(Direction d, const glm::vec3& f, const glm::vec3& t) {
    switch (d) {
    case Direction::Down: return {f.x, 16 - t.z, t.x, 16 - f.z};
    case Direction::Up: return {f.x, f.z, t.x, t.z};
    case Direction::North: return {16 - t.x, 16 - t.y, 16 - f.x, 16 - f.y};
    case Direction::South: return {f.x, 16 - t.y, t.x, 16 - f.y};
    case Direction::West: return {f.z, 16 - t.y, t.z, 16 - f.y};
    case Direction::East: return {16 - t.z, 16 - t.y, 16 - f.z, 16 - f.y};
    }
    return {0, 0, 16, 16};
}

bool bakeFace(const ElementDef& el, Direction dir, const FaceDef& face, const SpriteInfo& sprite,
              const glm::mat3& rot, bool uvlock, BakedQuad& out, int spriteIndex) {
    const glm::vec3 lo = glm::min(el.from, el.to), hi = glm::max(el.from, el.to);
    const auto& corners = kCorners[size_t(dir)];

    // Positions in pixels, through element rotation then blockstate rotation.
    glm::mat3 elRot(1.0f);
    glm::vec3 elScale(1.0f);
    if (el.hasRotation && el.rotationAngle != 0.0f) {
        const glm::vec3 axis = axisVector(el.rotationAxis);
        elRot = glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(el.rotationAngle), axis));
        if (el.rescale) {
            const float s = 1.0f / std::cos(glm::radians(el.rotationAngle));
            elScale = glm::vec3(s) - axis * (s - 1.0f); // stretch the two non-axis components
        }
    }
    for (int i = 0; i < 4; ++i) {
        glm::vec3 p(corners[size_t(i)].x ? hi.x : lo.x, corners[size_t(i)].y ? hi.y : lo.y,
                    corners[size_t(i)].z ? hi.z : lo.z);
        if (el.hasRotation) p = (elRot * (p - el.rotationOrigin)) * elScale + el.rotationOrigin;
        p = rot * (p - glm::vec3(8.0f)) + glm::vec3(8.0f);
        out.pos[size_t(i)] = p;
    }

    // Per-vertex UVs in sprite pixels: face rect + face rotation, then uvlock.
    const auto uv = face.hasUv ? face.uv : autoFaceUV(dir, lo, hi);
    std::array<glm::vec2, 4> tex;
    for (int i = 0; i < 4; ++i) {
        const int j = (i + face.rotation / 90) % 4;
        tex[size_t(i)] = {uv[(j == 0 || j == 1) ? 0 : 2], uv[(j == 0 || j == 3) ? 1 : 3]};
    }
    if (uvlock) {
        const Direction rotated = nearestDirection(rot * glm::vec3(normal(dir)));
        const glm::vec3 ru = rot * faceUAxis(dir), rv = rot * faceVAxis(dir);
        const glm::vec3 nu = faceUAxis(rotated), nv = faceVAxis(rotated);
        const glm::mat2 q(glm::dot(nu, ru), glm::dot(nv, ru),  // column 0
                          glm::dot(nu, rv), glm::dot(nv, rv)); // column 1
        for (auto& t : tex) t = q * (t - glm::vec2(8.0f)) + glm::vec2(8.0f);
    }
    for (int i = 0; i < 4; ++i) {
        // Pull each corner 0.1% toward the opposite one so sampling never touches a neighbor sprite.
        const glm::vec2 t = tex[size_t(i)] * 0.999f + tex[size_t((i + 2) % 4)] * 0.001f;
        out.uv[size_t(i)] = {sprite.u(t.x), sprite.v(t.y)};
    }

    // Facing from the baked geometry, as vanilla's calculateFacing does.
    const glm::vec3 n = glm::cross(out.pos[2] - out.pos[1], out.pos[0] - out.pos[1]);
    const float len = glm::length(n);
    if (!(len > 1e-6f)) return false; // zero-area face (e.g. a plane's edge)
    out.face = nearestDirection(n / len);

    for (auto& p : out.pos) p /= 16.0f;
    out.cull = face.cull < 0 ? int8_t(-1) : int8_t(nearestDirection(rot * glm::vec3(normal(Direction(face.cull)))));
    // Flush: every corner sits on the block boundary the quad faces.
    {
        const int axis = int(axisOf(out.face)); // Axis is X=0, Y=1, Z=2
        const float plane = isPositive(out.face) ? 1.0f : 0.0f;
        out.flush = true;
        for (const auto& p : out.pos)
            if (std::abs(p[axis] - plane) > 1e-4f) out.flush = false;
    }
    if (out.cull >= 0) out.cullMask = faceCellMask(Direction(out.cull), out.pos, true);
    out.tint = int8_t(face.tint);
    out.shade = el.shade;
    out.emission = uint8_t(el.lightEmission);
    out.sprite = uint16_t(spriteIndex);
    return true;
}

ModelBaker::ModelBaker(ModelStore& models, const Atlas& atlas) : models_(models), atlas_(atlas) {}

uint16_t ModelBaker::spriteFor(const ResolvedModel& model, const std::string& variable) {
    const std::string id = model.resolveTexture(variable);
    if (id.empty()) {
        ++missingTextures_;
        errors_.push_back(std::format("{}: texture variable #{} is unbound", model.name, variable));
        return uint16_t(atlas_.missing);
    }
    if (!atlas_.has(id)) {
        ++missingTextures_;
        errors_.push_back(std::format("{}: texture {} is not in the {} atlas", model.name, id, atlas_.name));
        return uint16_t(atlas_.missing);
    }
    return uint16_t(atlas_.find(id));
}

const BakedModel* ModelBaker::bake(const ModelVariant& v) {
    const std::string key = std::format("{}|{}|{}|{}", v.model, v.x, v.y, v.uvlock ? 1 : 0);
    if (auto it = cache_.find(key); it != cache_.end()) return it->second.get();
    auto baked = bakeUncached(v);
    const BakedModel* raw = baked.get();
    cache_.emplace(key, std::move(baked));
    return raw;
}

std::unique_ptr<BakedModel> ModelBaker::bakeUncached(const ModelVariant& v) {
    auto out = std::make_unique<BakedModel>();
    const auto model = models_.resolve(v.model);
    if (!model) {
        errors_.push_back(std::format("missing model {}", v.model));
        // Magenta cube so the problem is visible in-game.
        ElementDef cube;
        FaceDef f;
        const SpriteInfo& missing = atlas_.sprite(atlas_.missing);
        for (Direction d : kDirections) {
            f.cull = int(d);
            BakedQuad q;
            if (bakeFace(cube, d, f, missing, glm::mat3(1.0f), false, q, atlas_.missing)) out->quads.push_back(q);
        }
        out->particleSprite = uint16_t(atlas_.missing);
        return out;
    }
    out->ambientOcclusion = model->ambientOcclusion;
    out->guiLightFront = model->guiLightFront;
    out->builtinGenerated = model->builtinGenerated;
    out->display = model->display;
    out->hasDisplay = model->hasDisplay;
    out->particleSprite = model->textures.contains("particle") ? spriteFor(*model, "particle") : uint16_t(atlas_.missing);
    if (!model->elements) return out;

    const glm::mat3 rot = blockStateRotation(v.x, v.y);
    for (const ElementDef& el : *model->elements) {
        for (Direction d : kDirections) {
            const auto& face = el.faces[size_t(d)];
            if (!face) continue;
            const uint16_t sprite = spriteFor(*model, face->texture);
            BakedQuad q;
            if (bakeFace(el, d, *face, atlas_.sprite(sprite), rot, v.uvlock, q, sprite)) out->quads.push_back(q);
        }
    }
    return out;
}

} // namespace mc
