#include "entity/model/ModelPart.h"

#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>

namespace mc {

ModelPart* ModelPart::find(std::string_view path) {
    ModelPart* p = this;
    while (!path.empty() && p) {
        const size_t slash = path.find('/');
        const std::string_view head = path.substr(0, slash);
        ModelPart* next = nullptr;
        for (ModelPart& c : p->children)
            if (c.name == head) next = &c;
        p = next;
        path = slash == std::string_view::npos ? std::string_view() : path.substr(slash + 1);
    }
    return p;
}

const ModelPart* ModelPart::find(std::string_view path) const { return const_cast<ModelPart*>(this)->find(path); }

ModelPart& ModelPart::addChild(std::string childName, PartPose childPose) {
    for (ModelPart& c : children)
        if (c.name == childName) {
            c = ModelPart{};
            c.name = std::move(childName);
            c.pose = childPose;
            return c;
        }
    ModelPart& c = children.emplace_back();
    c.name = std::move(childName);
    c.pose = childPose;
    return c;
}

ModelPart& ModelPart::box(glm::vec2 texOffs, glm::vec3 from, glm::vec3 size, float grow, bool mirror) {
    cubes.push_back({from, size, texOffs, grow, mirror, 0x3F});
    return *this;
}

ModelPart& ModelPart::onlyFaces(uint8_t mask) {
    if (!cubes.empty()) cubes.back().faces = mask;
    return *this;
}

glm::mat4 poseMatrix(const PartPose& pose) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), pose.offset / 16.0f);
    if (pose.rotation.z != 0.0f) m = glm::rotate(m, pose.rotation.z, glm::vec3(0, 0, 1));
    if (pose.rotation.y != 0.0f) m = glm::rotate(m, pose.rotation.y, glm::vec3(0, 1, 0));
    if (pose.rotation.x != 0.0f) m = glm::rotate(m, pose.rotation.x, glm::vec3(1, 0, 0));
    if (pose.scale != glm::vec3(1.0f)) m = glm::scale(m, pose.scale);
    return m;
}

namespace {

// Vanilla ModelPart.Cube: the six polygons of a box with the box-UV layout
//     u: [l = u0 | m = l + d | n = m + w | o = n + w], [n | p = n + d | q = p + w]
//     v: [r = v0 | s = r + d | t = s + h]
// Down reads (m,r)-(n,s), up (n,s)-(o,r), west (l,s)-(m,t), north (m,s)-(n,t),
// east (n,s)-(p,t), south (p,s)-(q,t).
void bakeCube(const ModelCube& c, const glm::mat4& m, int texW, int texH, std::vector<ModelQuad>& out) {
    glm::vec3 lo = c.from - c.grow, hi = c.from + c.size + c.grow;
    if (c.mirror) std::swap(lo.x, hi.x);
    const glm::vec3 v[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
                            {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
    const float w = c.size.x, h = c.size.y, d = c.size.z;
    const float l = c.texOffs.x, mm = l + d, n = mm + w, o = n + w, p = n + d, q = p + w;
    const float r = c.texOffs.y, s = r + d, t = s + h;
    struct Face {
        int dir; // Direction index
        int vi[4];
        float u1, v1, u2, v2;
        glm::vec3 normal;
    };
    const Face faces[6] = {
        {0, {5, 4, 0, 1}, mm, r, n, s, {0, -1, 0}}, // down
        {1, {2, 3, 7, 6}, n, s, o, r, {0, 1, 0}},   // up
        {4, {0, 4, 7, 3}, l, s, mm, t, {-1, 0, 0}}, // west
        {2, {1, 0, 3, 2}, mm, s, n, t, {0, 0, -1}}, // north
        {5, {5, 1, 2, 6}, n, s, p, t, {1, 0, 0}},   // east
        {3, {4, 5, 6, 7}, p, s, q, t, {0, 0, 1}},   // south
    };
    const glm::mat3 nm = glm::mat3(m);
    for (const Face& f : faces) {
        if (!((c.faces >> f.dir) & 1u)) continue;
        if (f.u1 == f.u2 || f.v1 == f.v2) continue; // zero-area face of a flat cube
        ModelQuad quad;
        const glm::vec2 uvs[4] = {{f.u2, f.v1}, {f.u1, f.v1}, {f.u1, f.v2}, {f.u2, f.v2}};
        for (int k = 0; k < 4; ++k) {
            quad.pos[size_t(k)] = glm::vec3(m * glm::vec4(v[f.vi[k]] / 16.0f, 1.0f));
            quad.uv[size_t(k)] = uvs[k] / glm::vec2(float(texW), float(texH));
        }
        glm::vec3 normal = f.normal;
        if (c.mirror) {
            // Mirrored boxes reverse the winding and the x normal.
            std::reverse(quad.pos.begin(), quad.pos.end());
            std::reverse(quad.uv.begin(), quad.uv.end());
            normal.x = -normal.x;
        }
        quad.normal = glm::normalize(nm * normal);
        out.push_back(quad);
    }
}

void collectRects(const ModelPart& part, std::vector<glm::ivec4>& out) {
    for (const ModelCube& c : part.cubes) {
        const int w = int(c.size.x), h = int(c.size.y), d = int(c.size.z);
        const int u = int(c.texOffs.x), v = int(c.texOffs.y);
        auto add = [&](int dir, int u0, int v0, int u1, int v1) {
            if ((c.faces >> dir) & 1u && u1 > u0 && v1 > v0) out.push_back({u0, v0, u1, v1});
        };
        add(0, u + d, v, u + d + w, v + d);                 // down
        add(1, u + d + w, v, u + d + 2 * w, v + d);         // up
        add(4, u, v + d, u + d, v + d + h);                 // west
        add(2, u + d, v + d, u + d + w, v + d + h);         // north
        add(5, u + d + w, v + d, u + 2 * d + w, v + d + h); // east
        add(3, u + 2 * d + w, v + d, u + 2 * d + 2 * w, v + d + h); // south
    }
    for (const ModelPart& child : part.children) collectRects(child, out);
}

} // namespace

void bakeModel(const ModelPart& part, const glm::mat4& transform, int texWidth, int texHeight,
               std::vector<ModelQuad>& out) {
    if (!part.visible) return;
    const glm::mat4 m = transform * poseMatrix(part.pose);
    for (const ModelCube& c : part.cubes) bakeCube(c, m, texWidth, texHeight, out);
    for (const ModelPart& child : part.children) bakeModel(child, m, texWidth, texHeight, out);
}

std::vector<glm::ivec4> modelUvRects(const ModelDefinition& model) {
    std::vector<glm::ivec4> out;
    collectRects(model.root, out);
    return out;
}

} // namespace mc
