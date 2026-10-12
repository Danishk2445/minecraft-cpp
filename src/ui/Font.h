// Font.h -- draws text with the pack's default font (resources/FontSet): every
// bitmap provider, § formatting codes (colors, bold, italic, underline,
// strikethrough, obfuscated) and vanilla drop shadows.
#pragma once

#include <cmath>
#include <string_view>
#include <vector>

#include "gfx/Texture.h"
#include "resources/FontSet.h"
#include "ui/GuiBatch.h"

namespace mc {

class ResourcePack;

class Font {
public:
    static constexpr int kLineHeight = 9;

    bool load(const ResourcePack& pack);
    const FontSet& glyphs() const { return set_; }
    // Width in GUI pixels, rounded up (vanilla Font.width).
    int width(std::string_view text) const { return int(std::ceil(set_.width(text))); }
    // Draws formatted text with its top-left corner at (x, y); `argb` is the base
    // color (opaque when its alpha is 0, like vanilla). Returns the end x.
    float draw(GuiBatch& batch, std::string_view text, float x, float y, uint32_t argb, bool shadow = true) const;
    // Glyph quads for drawing text elsewhere (signs in the world): corners in text pixels
    // (y down), atlas UVs in this font's texture(), no shadow, underlines left out.
    struct GlyphQuad {
        glm::vec2 corners[4]; // top-left, bottom-left, bottom-right, top-right
        glm::vec2 uv0, uv1;
        uint32_t argb;
    };
    void glyphQuads(std::string_view text, float x, float y, uint32_t argb, std::vector<GlyphQuad>& out) const;
    const Texture& texture() const { return texture_; }
    // Centered like vanilla drawCenteredString: x - width / 2 in whole pixels.
    void drawCentered(GuiBatch& batch, std::string_view text, float centerX, float y, uint32_t argb,
                      bool shadow = true) const {
        draw(batch, text, centerX - float(width(text) / 2), y, argb, shadow);
    }
    // Splits formatted text into lines no wider than `width`, at spaces when possible;
    // each new line repeats the formatting in effect (vanilla ComponentRenderUtils.wrapComponents).
    std::vector<std::string> split(std::string_view text, float width) const { return set_.split(text, width); }

private:
    float drawPass(GuiBatch& batch, std::u32string_view text, float x, float y, uint32_t argb, bool shadowPass,
                   uint32_t random) const;
    FontSet set_;
    Texture texture_;
    mutable uint32_t random_ = 0x9E3779B9u; // obfuscated text
};

} // namespace mc
