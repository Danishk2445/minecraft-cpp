#include "texture.h"
#include <png.h>
#include <cstring>
#include <stdexcept>

Image loadPNG(const std::string& path) {
    png_image img;
    memset(&img, 0, sizeof img);
    img.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&img, path.c_str()))
        throw std::runtime_error("PNG open failed: " + path + " (" + img.message + ")");
    img.format = PNG_FORMAT_RGBA;
    Image out;
    out.w = (int)img.width;
    out.h = (int)img.height;
    out.px.resize(PNG_IMAGE_SIZE(img));
    if (!png_image_finish_read(&img, nullptr, out.px.data(), 0, nullptr))
        throw std::runtime_error("PNG read failed: " + path + " (" + img.message + ")");
    return out;
}

void savePNG(const std::string& path, int w, int h, const uint8_t* rgbaBottomUp) {
    std::vector<uint8_t> flipped((size_t)w * h * 4);
    for (int y = 0; y < h; y++)
        memcpy(&flipped[(size_t)y * w * 4], &rgbaBottomUp[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
    png_image img;
    memset(&img, 0, sizeof img);
    img.version = PNG_IMAGE_VERSION;
    img.width = (png_uint_32)w;
    img.height = (png_uint_32)h;
    img.format = PNG_FORMAT_RGBA;
    png_image_write_to_file(&img, path.c_str(), 0, flipped.data(), w * 4, nullptr);
}

Image tintImage(Image img, int r, int g, int b) {
    for (size_t i = 0; i < img.px.size(); i += 4) {
        img.px[i + 0] = (uint8_t)(img.px[i + 0] * r / 255);
        img.px[i + 1] = (uint8_t)(img.px[i + 1] * g / 255);
        img.px[i + 2] = (uint8_t)(img.px[i + 2] * b / 255);
    }
    return img;
}

void compositeOver(Image& dst, const Image& src) {
    for (size_t i = 0; i + 3 < dst.px.size() && i + 3 < src.px.size(); i += 4) {
        float a = src.px[i + 3] / 255.0f;
        for (int c = 0; c < 3; c++)
            dst.px[i + c] = (uint8_t)(src.px[i + c] * a + dst.px[i + c] * (1.0f - a));
    }
}

GLuint loadTexture2D(const std::string& path, int* outW, int* outH, GLenum wrap) {
    Image img = loadPNG(path);
    if (outW) *outW = img.w;
    if (outH) *outH = img.h;
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.px.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    return tex;
}

static Image crop16(const Image& img) {
    if (img.w == 16 && img.h == 16) return img;
    Image out;
    out.w = out.h = 16;
    out.px.resize(16 * 16 * 4);
    for (int y = 0; y < 16; y++)
        memcpy(&out.px[y * 16 * 4], &img.px[(size_t)y * img.w * 4], 16 * 4);
    return out;
}

Image BlockAtlas::load(const char* name) const {
    return loadPNG(blockDir + "/" + name + ".png");
}

uint16_t BlockAtlas::add(Image img) {
    layers_.push_back(crop16(img));
    return (uint16_t)(layers_.size() - 1);
}

uint16_t BlockAtlas::addAnimated(Image strip, float secondsPerFrame) {
    int n = strip.h / strip.w;
    if (n < 1) n = 1;
    Anim anim;
    anim.secondsPerFrame = secondsPerFrame;
    for (int f = 0; f < n; f++) {
        Image frame;
        frame.w = frame.h = 16;
        frame.px.assign(strip.px.begin() + (size_t)f * 16 * 16 * 4,
                        strip.px.begin() + (size_t)(f + 1) * 16 * 16 * 4);
        anim.frames.push_back(std::move(frame));
    }
    anim.layer = add(anim.frames[0]);
    anims_.push_back(std::move(anim));
    return anims_.back().layer;
}

void BlockAtlas::upload() {
    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, tex_);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 16, 16, (GLsizei)layers_.size(), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    for (size_t i = 0; i < layers_.size(); i++)
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, (GLint)i, 16, 16, 1,
                        GL_RGBA, GL_UNSIGNED_BYTE, layers_[i].px.data());
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 4);
}

void BlockAtlas::bind(int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, tex_);
}

void BlockAtlas::update(double t) {
    bool changed = false;
    glBindTexture(GL_TEXTURE_2D_ARRAY, tex_);
    for (auto& a : anims_) {
        int f = (int)(t / a.secondsPerFrame) % (int)a.frames.size();
        if (f != a.current) {
            a.current = f;
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, a.layer, 16, 16, 1,
                            GL_RGBA, GL_UNSIGNED_BYTE, a.frames[f].px.data());
            changed = true;
        }
    }
    if (changed) glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
}
