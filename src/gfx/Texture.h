// Texture.h -- RAII wrappers for immutable-storage GL textures and buffers.
#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

#include "core/Image.h"

namespace mc {

class Texture {
public:
    Texture() = default;
    ~Texture() { destroy(); }
    Texture(Texture&& o) noexcept { *this = std::move(o); }
    Texture& operator=(Texture&& o) noexcept {
        if (this != &o) {
            destroy();
            id_ = std::exchange(o.id_, 0u);
            width_ = o.width_;
            height_ = o.height_;
            levels_ = o.levels_;
        }
        return *this;
    }
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    // RGBA8 2D texture with `levels` mip levels (immutable storage).
    void create(int width, int height, int levels = 1, unsigned internalFormat = 0x8058 /*GL_RGBA8*/);
    void createDepth(int width, int height); // GL_DEPTH24_STENCIL8
    void upload(int level, int x, int y, const Image& image);
    void upload(int level, int x, int y, int w, int h, const void* rgba);
    // GL_NEAREST / GL_LINEAR / GL_NEAREST_MIPMAP_LINEAR ...
    void setFilter(unsigned minFilter, unsigned magFilter);
    void setWrap(unsigned wrap);
    void bind(unsigned unit) const;
    void destroy();

    unsigned id() const { return id_; }
    int width() const { return width_; }
    int height() const { return height_; }
    int levels() const { return levels_; }
    bool valid() const { return id_ != 0; }

private:
    unsigned id_ = 0;
    int width_ = 0, height_ = 0, levels_ = 0;
};

class GpuBuffer {
public:
    GpuBuffer() = default;
    ~GpuBuffer() { destroy(); }
    GpuBuffer(GpuBuffer&& o) noexcept { *this = std::move(o); }
    GpuBuffer& operator=(GpuBuffer&& o) noexcept {
        if (this != &o) {
            destroy();
            id_ = std::exchange(o.id_, 0u);
            size_ = std::exchange(o.size_, 0u);
        }
        return *this;
    }
    GpuBuffer(const GpuBuffer&) = delete;
    GpuBuffer& operator=(const GpuBuffer&) = delete;

    // Immutable storage; pass GL_DYNAMIC_STORAGE_BIT in flags to allow update().
    void create(size_t bytes, const void* data, unsigned flags = 0);
    void update(size_t offset, size_t bytes, const void* data);
    void destroy();

    unsigned id() const { return id_; }
    size_t size() const { return size_; }
    bool valid() const { return id_ != 0; }

private:
    unsigned id_ = 0;
    size_t size_ = 0;
};

} // namespace mc
