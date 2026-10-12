#include "render/Panorama.h"

#include <cstddef>
#include <format>

#include <glm/gtc/matrix_transform.hpp>

#include "core/Image.h"
#include "gfx/Camera.h"
#include "gfx/GL.h"

namespace mc {
namespace {

struct Vertex {
    glm::vec3 dir;
    glm::vec2 uv;
};

} // namespace

glm::vec2 Panorama::faceRotation(int face) {
    switch (face) {
    case 0: return {0.0f, 0.0f};
    case 1: return {90.0f, 0.0f};
    case 2: return {180.0f, 0.0f};
    case 3: return {-90.0f, 0.0f};
    case 4: return {0.0f, -90.0f}; // up
    default: return {0.0f, 90.0f}; // down
    }
}

Camera Panorama::faceCamera(int face, const glm::dvec3& eye, float baseYaw) {
    Camera c;
    c.position = eye;
    const glm::vec2 r = faceRotation(face);
    c.yaw = baseYaw + r.x;
    c.pitch = r.y;
    c.fov = 90.0f;
    return c;
}

Panorama::~Panorama() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
}

bool Panorama::initGl() {
    if (vao_) return true;
    if (!shader_.load("panorama.vert", "panorama.frag")) return false;
    // Each face is the square its camera saw: view-space corners (+-1, +-1, -1) turned
    // back into world directions with the same Camera math the bake used.
    std::vector<Vertex> vertices;
    for (int face = 0; face < kFaces; ++face) {
        const Camera c = faceCamera(face, glm::dvec3(0.0), 0.0f);
        const glm::mat3 toWorld = glm::transpose(glm::mat3(c.view()));
        auto corner = [&](float vx, float vy) {
            return Vertex{toWorld * glm::vec3(vx, vy, -1.0f), {(vx + 1.0f) / 2.0f, (1.0f - vy) / 2.0f}};
        };
        const Vertex q[4] = {corner(-1, 1), corner(-1, -1), corner(1, -1), corner(1, 1)};
        for (int i : {0, 1, 2, 0, 2, 3}) vertices.push_back(q[i]);
    }
    glCreateVertexArrays(1, &vao_);
    glCreateBuffers(1, &vbo_);
    glNamedBufferData(vbo_, GLsizeiptr(vertices.size() * sizeof(Vertex)), vertices.data(), GL_STATIC_DRAW);
    glVertexArrayVertexBuffer(vao_, 0, vbo_, 0, sizeof(Vertex));
    glEnableVertexArrayAttrib(vao_, 0);
    glVertexArrayAttribFormat(vao_, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, dir));
    glVertexArrayAttribBinding(vao_, 0, 0);
    glEnableVertexArrayAttrib(vao_, 1);
    glVertexArrayAttribFormat(vao_, 1, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, uv));
    glVertexArrayAttribBinding(vao_, 1, 0);
    return true;
}

bool Panorama::load(const std::filesystem::path& dir) {
    loaded_ = false;
    for (int face = 0; face < kFaces; ++face) {
        auto img = Image::loadPng(dir / std::format("panorama_{}.png", face));
        if (!img || img->width < 2) return false;
        faces_[size_t(face)].create(img->width, img->height, 1);
        faces_[size_t(face)].upload(0, 0, 0, *img);
        faces_[size_t(face)].setFilter(GL_LINEAR, GL_LINEAR);
    }
    loaded_ = initGl();
    return loaded_;
}

void Panorama::render(glm::ivec2 viewport, float spin, float pitch) {
    if (!loaded_) return;
    // Vanilla turns the cube by -spin, which is the camera turning right (yaw +spin).
    Camera c;
    c.yaw = spin;
    c.pitch = pitch;
    c.fov = 85.0f;
    c.nearPlane = 0.05f;
    c.farPlane = 10.0f;
    const float aspect = float(viewport.x) / float(std::max(viewport.y, 1));
    glViewport(0, 0, viewport.x, viewport.y);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    shader_.use();
    shader_.set("uViewProj", c.projection(aspect) * c.view());
    glBindVertexArray(vao_);
    for (int face = 0; face < kFaces; ++face) {
        faces_[size_t(face)].bind(0);
        glDrawArrays(GL_TRIANGLES, face * 6, 6);
    }
}

} // namespace mc
