#include "ui/GuiBatch.h"

#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>

#include "gfx/GL.h"

namespace mc {

int autoGuiScale(glm::ivec2 fb, int maxScale) {
    int s = 1;
    while ((maxScale <= 0 || s < maxScale) && fb.x / (s + 1) >= 320 && fb.y / (s + 1) >= 240) ++s;
    return s;
}

GuiBatch::~GuiBatch() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
}

bool GuiBatch::init() {
    if (!shader_.load("gui.vert", "gui.frag")) return false;
    const uint8_t white[4] = {255, 255, 255, 255};
    white_.create(1, 1, 1);
    white_.upload(0, 0, 0, 1, 1, white);
    glCreateVertexArrays(1, &vao_);
    glCreateBuffers(1, &vbo_);
    glEnableVertexArrayAttrib(vao_, 0);
    glVertexArrayAttribFormat(vao_, 0, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, pos));
    glVertexArrayAttribBinding(vao_, 0, 0);
    glEnableVertexArrayAttrib(vao_, 1);
    glVertexArrayAttribFormat(vao_, 1, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, uv));
    glVertexArrayAttribBinding(vao_, 1, 0);
    glEnableVertexArrayAttrib(vao_, 2);
    glVertexArrayAttribFormat(vao_, 2, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(Vertex, color));
    glVertexArrayAttribBinding(vao_, 2, 0);
    return true;
}

void GuiBatch::begin(glm::ivec2 fb, int scale) {
    scale_ = std::max(scale, 1);
    size_ = glm::vec2(fb) / float(scale_);
    glViewport(0, 0, fb.x, fb.y);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    shader_.use();
    shader_.set("uProj", glm::ortho(0.0f, size_.x, size_.y, 0.0f, -1.0f, 1.0f));
    texture_ = nullptr;
    vertices_.clear();
}

void GuiBatch::quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32_t argb) {
    const uint32_t c = ((argb >> 16) & 0xFF) | (argb & 0xFF00) | ((argb & 0xFF) << 16) | (argb & 0xFF000000u);
    vertices_.insert(vertices_.end(), {{{x0, y0}, {u0, v0}, c},
                                       {{x0, y1}, {u0, v1}, c},
                                       {{x1, y1}, {u1, v1}, c},
                                       {{x1, y1}, {u1, v1}, c},
                                       {{x1, y0}, {u1, v0}, c},
                                       {{x0, y0}, {u0, v0}, c}});
}

void GuiBatch::fill(float x0, float y0, float x1, float y1, uint32_t argb) {
    if (texture_ != &white_) {
        flush();
        texture_ = &white_;
    }
    quad(x0, y0, x1, y1, 0, 0, 1, 1, argb);
}

void GuiBatch::blit(const Texture& texture, float x0, float y0, float x1, float y1, float u0, float v0, float u1,
                    float v1, uint32_t argb) {
    if (texture_ != &texture) {
        flush();
        texture_ = &texture;
    }
    quad(x0, y0, x1, y1, u0, v0, u1, v1, argb);
}

void GuiBatch::flush() {
    if (vertices_.empty() || !texture_) {
        vertices_.clear();
        return;
    }
    if (vertices_.size() > capacity_) {
        capacity_ = std::max(vertices_.size(), capacity_ * 2);
        glNamedBufferData(vbo_, GLsizeiptr(capacity_ * sizeof(Vertex)), nullptr, GL_STREAM_DRAW);
        glVertexArrayVertexBuffer(vao_, 0, vbo_, 0, sizeof(Vertex));
    }
    glNamedBufferSubData(vbo_, 0, GLsizeiptr(vertices_.size() * sizeof(Vertex)), vertices_.data());
    shader_.use();
    texture_->bind(0);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(vertices_.size()));
    vertices_.clear();
}

void GuiBatch::end() {
    flush();
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

} // namespace mc
