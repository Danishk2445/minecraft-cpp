// Image.h -- RGBA8 images: PNG decode (every color type in the pack) and encode.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mc {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels; // RGBA8, rows top to bottom

    Image() = default;
    Image(int w, int h, uint32_t fillRgba = 0);

    bool empty() const { return width <= 0 || height <= 0; }
    uint8_t* px(int x, int y) { return pixels.data() + (size_t(y) * size_t(width) + size_t(x)) * 4; }
    const uint8_t* px(int x, int y) const {
        return pixels.data() + (size_t(y) * size_t(width) + size_t(x)) * 4;
    }
    uint8_t alpha(int x, int y) const { return px(x, y)[3]; }

    Image region(int x, int y, int w, int h) const;
    void blit(const Image& src, int dstX, int dstY);

    // Palette, gray, gray+alpha, tRNS and 16-bit inputs all expand to RGBA8.
    // No gamma conversion is applied (the game treats texels as raw sRGB values).
    static std::optional<Image> decodePng(std::span<const uint8_t> data, std::string* error = nullptr);
    static std::optional<Image> loadPng(const std::filesystem::path& path, std::string* error = nullptr);

    std::vector<uint8_t> encodePng() const;
    bool savePng(const std::filesystem::path& path) const;
};

} // namespace mc
