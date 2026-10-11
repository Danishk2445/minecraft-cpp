// Font.h -- the pack's ASCII bitmap font (textures/font/ascii.png) with vanilla
// glyph widths. The full provider chain (accented, nonlatin, § codes) comes in M8.
#pragma once

#include <array>
#include <string_view>

#include "gfx/Texture.h"
#include "ui/GuiBatch.h"

namespace mc {

class ResourcePack;

class Font {
public:
    static constexpr int kLineHeight = 9;

    bool load(const ResourcePack& pack);
    // Advance in GUI pixels (glyph width + 1; space advances 4).
    int advance(unsigned char c) const { return advance_[c]; }
    int width(std::string_view text) const;
    // Draws at (x, y) = top-left. Shadow: a quarter-brightness copy offset by 1.
    void draw(GuiBatch& batch, std::string_view text, float x, float y, uint32_t argb, bool shadow = true) const;

private:
    void drawRun(GuiBatch& batch, std::string_view text, float x, float y, uint32_t argb) const;
    Texture texture_;
    std::array<int, 256> advance_{};
};

} // namespace mc
