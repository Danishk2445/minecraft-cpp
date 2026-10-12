#include "render/MenuBlur.h"

#include "gfx/GL.h"

namespace mc {

MenuBlur::~MenuBlur() {
    if (fbo_[0]) glDeleteFramebuffers(2, fbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
}

bool MenuBlur::init() {
    if (!shader_.load("blur.vert", "blur.frag")) return false;
    glCreateVertexArrays(1, &vao_); // the full-screen triangle comes from gl_VertexID
    return true;
}

void MenuBlur::resize(glm::ivec2 size) {
    if (size == size_) return;
    if (fbo_[0]) glDeleteFramebuffers(2, fbo_);
    glCreateFramebuffers(2, fbo_);
    for (int i = 0; i < 2; ++i) {
        color_[i].create(size.x, size.y, 1);
        // The shader leans on bilinear filtering to read two texels per sample.
        color_[i].setFilter(GL_LINEAR, GL_LINEAR);
        color_[i].setWrap(GL_CLAMP_TO_EDGE);
        glNamedFramebufferTexture(fbo_[i], GL_COLOR_ATTACHMENT0, color_[i].id(), 0);
    }
    size_ = size;
}

void MenuBlur::apply(glm::ivec2 size, int radius) {
    if (radius < 1 || size.x <= 0 || size.y <= 0 || !vao_) return;
    resize(size);
    glBlitNamedFramebuffer(0, fbo_[0], 0, 0, size.x, size.y, 0, 0, size.x, size.y, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, size.x, size.y);
    shader_.use();
    shader_.set("uRadius", float(radius));
    glBindVertexArray(vao_);
    // main -> swap (across), swap -> main (down), three times; the last pass writes the screen.
    for (int pass = 0; pass < 6; ++pass) {
        const bool across = pass % 2 == 0;
        color_[pass % 2].bind(0);
        glBindFramebuffer(GL_FRAMEBUFFER, pass == 5 ? 0 : fbo_[(pass + 1) % 2]);
        shader_.set("uStep", across ? glm::vec2(1.0f / float(size.x), 0.0f) : glm::vec2(0.0f, 1.0f / float(size.y)));
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

} // namespace mc
