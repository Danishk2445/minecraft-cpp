#include "ui/Font.h"

#include "core/Image.h"
#include "core/Log.h"
#include "gfx/GL.h"
#include "resources/ResourcePack.h"

namespace mc {

bool Font::load(const ResourcePack& pack) {
    auto img = Image::loadPng(pack.file("minecraft", "textures/font/ascii.png"));
    if (!img || img->width != 128 || img->height != 128) {
        log::error("font: textures/font/ascii.png missing or not 128x128");
        return false;
    }
    texture_.create(img->width, img->height, 1);
    texture_.upload(0, 0, 0, *img);
    texture_.setFilter(GL_NEAREST, GL_NEAREST);
    // Vanilla bitmap provider: width = rightmost opaque column + 1, advance = width + 1.
    for (int c = 0; c < 256; ++c) {
        const int cx = (c % 16) * 8, cy = (c / 16) * 8;
        int right = -1;
        for (int x = 7; x >= 0 && right < 0; --x)
            for (int y = 0; y < 8; ++y)
                if (img->alpha(cx + x, cy + y) != 0) {
                    right = x;
                    break;
                }
        advance_[size_t(c)] = right < 0 ? 0 : right + 2;
    }
    advance_[' '] = 4;
    return true;
}

int Font::width(std::string_view text) const {
    int w = 0;
    for (unsigned char c : text) w += advance_[c];
    return w;
}

void Font::drawRun(GuiBatch& batch, std::string_view text, float x, float y, uint32_t argb) const {
    for (unsigned char c : text) {
        if (c != ' ' && advance_[c] > 0) {
            const float u0 = float(c % 16) * 8.0f / 128.0f, v0 = float(c / 16) * 8.0f / 128.0f;
            batch.blit(texture_, x, y, x + 8, y + 8, u0, v0, u0 + 8.0f / 128.0f, v0 + 8.0f / 128.0f, argb);
        }
        x += float(advance_[c]);
    }
}

void Font::draw(GuiBatch& batch, std::string_view text, float x, float y, uint32_t argb, bool shadow) const {
    if (shadow) {
        const uint32_t s = ((argb & 0xFCFCFCu) >> 2) | (argb & 0xFF000000u);
        drawRun(batch, text, x + 1, y + 1, s);
    }
    drawRun(batch, text, x, y, argb);
}

} // namespace mc
