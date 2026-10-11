#include "core/Image.h"

#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>

#include <png.h>

#include "core/Fs.h"

namespace mc {

Image::Image(int w, int h, uint32_t fillRgba) : width(w), height(h), pixels(size_t(w) * size_t(h) * 4) {
    const uint8_t c[4] = {uint8_t(fillRgba >> 24), uint8_t(fillRgba >> 16), uint8_t(fillRgba >> 8),
                          uint8_t(fillRgba)};
    for (size_t i = 0; i < pixels.size(); i += 4) std::memcpy(&pixels[i], c, 4);
}

Image Image::region(int x, int y, int w, int h) const {
    Image out(w, h);
    for (int row = 0; row < h; ++row) {
        if (y + row < 0 || y + row >= height) continue;
        for (int col = 0; col < w; ++col) {
            if (x + col < 0 || x + col >= width) continue;
            std::memcpy(out.px(col, row), px(x + col, y + row), 4);
        }
    }
    return out;
}

void Image::blit(const Image& src, int dstX, int dstY) {
    for (int row = 0; row < src.height; ++row) {
        const int y = dstY + row;
        if (y < 0 || y >= height) continue;
        for (int col = 0; col < src.width; ++col) {
            const int x = dstX + col;
            if (x < 0 || x >= width) continue;
            std::memcpy(px(x, y), src.px(col, row), 4);
        }
    }
}

namespace {

struct ReadState {
    const uint8_t* data;
    size_t size;
    size_t pos;
};

void readCallback(png_structp png, png_bytep out, png_size_t count) {
    auto* s = static_cast<ReadState*>(png_get_io_ptr(png));
    if (s->pos + count > s->size) png_error(png, "unexpected end of data");
    std::memcpy(out, s->data + s->pos, count);
    s->pos += count;
}

void errorCallback(png_structp png, png_const_charp message) {
    auto* msg = static_cast<char*>(png_get_error_ptr(png));
    std::snprintf(msg, 256, "%s", message);
    png_longjmp(png, 1);
}

void warningCallback(png_structp, png_const_charp) {}

// Kept free of C++ objects with destructors: longjmp out of libpng must not skip any.
bool decodeRaw(const uint8_t* data, size_t size, int* w, int* h, uint8_t** out, char* errMsg) {
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, errMsg, errorCallback, warningCallback);
    if (!png) return false;
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        return false;
    }
    uint8_t* volatile pixels = nullptr;
    png_bytep* volatile rows = nullptr;
    if (setjmp(png_jmpbuf(png))) {
        std::free(pixels);
        std::free(rows);
        png_destroy_read_struct(&png, &info, nullptr);
        return false;
    }
    ReadState state{data, size, 0};
    png_set_read_fn(png, &state, readCallback);
    png_read_info(png, info);

    const png_byte colorType = png_get_color_type(png, info);
    const png_byte bitDepth = png_get_bit_depth(png, info);
    if (bitDepth == 16) png_set_strip_16(png);
    if (colorType == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
    if (colorType == PNG_COLOR_TYPE_RGB || colorType == PNG_COLOR_TYPE_GRAY ||
        colorType == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER); // no-op if tRNS already added alpha
    png_set_interlace_handling(png);
    png_read_update_info(png, info);

    const png_uint_32 width = png_get_image_width(png, info);
    const png_uint_32 height = png_get_image_height(png, info);
    const size_t rowBytes = png_get_rowbytes(png, info);
    if (rowBytes != size_t(width) * 4) png_error(png, "unexpected row size after expansion");

    pixels = static_cast<uint8_t*>(std::malloc(rowBytes * height));
    rows = static_cast<png_bytep*>(std::malloc(sizeof(png_bytep) * height));
    if (!pixels || !rows) png_error(png, "out of memory");
    for (png_uint_32 y = 0; y < height; ++y) rows[y] = pixels + y * rowBytes;
    png_read_image(png, rows);
    png_read_end(png, nullptr);

    std::free(rows);
    png_destroy_read_struct(&png, &info, nullptr);
    *w = int(width);
    *h = int(height);
    *out = pixels;
    return true;
}

void writeCallback(png_structp png, png_bytep data, png_size_t count) {
    auto* out = static_cast<std::vector<uint8_t>*>(png_get_io_ptr(png));
    out->insert(out->end(), data, data + count);
}

void flushCallback(png_structp) {}

bool encodeRaw(const uint8_t* rgba, int w, int h, std::vector<uint8_t>* out, char* errMsg) {
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, errMsg, errorCallback, warningCallback);
    if (!png) return false;
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, nullptr);
        return false;
    }
    png_bytep* volatile rows = nullptr;
    if (setjmp(png_jmpbuf(png))) {
        std::free(rows);
        png_destroy_write_struct(&png, &info);
        return false;
    }
    png_set_write_fn(png, out, writeCallback, flushCallback);
    png_set_IHDR(png, info, png_uint_32(w), png_uint_32(h), 8, PNG_COLOR_TYPE_RGB_ALPHA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_set_compression_level(png, 6);
    png_write_info(png, info);
    rows = static_cast<png_bytep*>(std::malloc(sizeof(png_bytep) * size_t(h)));
    if (!rows) png_error(png, "out of memory");
    for (int y = 0; y < h; ++y) rows[y] = const_cast<png_bytep>(rgba + size_t(y) * size_t(w) * 4);
    png_write_image(png, rows);
    png_write_end(png, nullptr);
    std::free(rows);
    png_destroy_write_struct(&png, &info);
    return true;
}

} // namespace

std::optional<Image> Image::decodePng(std::span<const uint8_t> data, std::string* error) {
    char msg[256] = "invalid PNG";
    int w = 0, h = 0;
    uint8_t* raw = nullptr;
    if (data.size() < 8 || png_sig_cmp(data.data(), 0, 8) != 0) {
        if (error) *error = "not a PNG file";
        return std::nullopt;
    }
    if (!decodeRaw(data.data(), data.size(), &w, &h, &raw, msg)) {
        if (error) *error = msg;
        return std::nullopt;
    }
    Image img;
    img.width = w;
    img.height = h;
    img.pixels.assign(raw, raw + size_t(w) * size_t(h) * 4);
    std::free(raw);
    return img;
}

std::optional<Image> Image::loadPng(const std::filesystem::path& path, std::string* error) {
    auto bytes = fs::readBytes(path);
    if (!bytes) {
        if (error) *error = std::format("{}: cannot read file", path.string());
        return std::nullopt;
    }
    std::string err;
    auto img = decodePng(*bytes, &err);
    if (!img && error) *error = std::format("{}: {}", path.string(), err);
    return img;
}

std::vector<uint8_t> Image::encodePng() const {
    std::vector<uint8_t> out;
    char msg[256] = "";
    if (empty() || !encodeRaw(pixels.data(), width, height, &out, msg)) out.clear();
    return out;
}

bool Image::savePng(const std::filesystem::path& path) const {
    const auto bytes = encodePng();
    return !bytes.empty() && fs::writeAtomic(path, bytes);
}

} // namespace mc
