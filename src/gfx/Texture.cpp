#include "gfx/Texture.h"

#include "gfx/GL.h"

namespace mc {

void Texture::create(int width, int height, int levels, unsigned internalFormat) {
    destroy();
    glCreateTextures(GL_TEXTURE_2D, 1, &id_);
    glTextureStorage2D(id_, levels, internalFormat, width, height);
    glTextureParameteri(id_, GL_TEXTURE_MAX_LEVEL, levels - 1);
    width_ = width;
    height_ = height;
    levels_ = levels;
    setFilter(levels > 1 ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST, GL_NEAREST);
    setWrap(GL_CLAMP_TO_EDGE);
}

void Texture::createDepth(int width, int height) {
    destroy();
    glCreateTextures(GL_TEXTURE_2D, 1, &id_);
    glTextureStorage2D(id_, 1, GL_DEPTH24_STENCIL8, width, height);
    width_ = width;
    height_ = height;
    levels_ = 1;
    setFilter(GL_NEAREST, GL_NEAREST);
    setWrap(GL_CLAMP_TO_EDGE);
}

void Texture::upload(int level, int x, int y, const Image& image) {
    upload(level, x, y, image.width, image.height, image.pixels.data());
}

void Texture::upload(int level, int x, int y, int w, int h, const void* rgba) {
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTextureSubImage2D(id_, level, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

void Texture::setFilter(unsigned minFilter, unsigned magFilter) {
    glTextureParameteri(id_, GL_TEXTURE_MIN_FILTER, GLint(minFilter));
    glTextureParameteri(id_, GL_TEXTURE_MAG_FILTER, GLint(magFilter));
}

void Texture::setWrap(unsigned wrap) {
    glTextureParameteri(id_, GL_TEXTURE_WRAP_S, GLint(wrap));
    glTextureParameteri(id_, GL_TEXTURE_WRAP_T, GLint(wrap));
}

void Texture::bind(unsigned unit) const { glBindTextureUnit(unit, id_); }

void Texture::destroy() {
    if (id_) glDeleteTextures(1, &id_);
    id_ = 0;
}

void GpuBuffer::create(size_t bytes, const void* data, unsigned flags) {
    destroy();
    glCreateBuffers(1, &id_);
    glNamedBufferStorage(id_, GLsizeiptr(bytes ? bytes : 1), data, flags);
    size_ = bytes;
}

void GpuBuffer::update(size_t offset, size_t bytes, const void* data) {
    glNamedBufferSubData(id_, GLintptr(offset), GLsizeiptr(bytes), data);
}

void GpuBuffer::destroy() {
    if (id_) glDeleteBuffers(1, &id_);
    id_ = 0;
    size_ = 0;
}

} // namespace mc
