#include "ui/Font.h"

#include "core/Log.h"
#include "gfx/GL.h"

namespace mc {

bool Font::load(const ResourcePack& pack) {
    std::string problems;
    const bool ok = set_.load(pack, "minecraft:default", &problems);
    for (size_t start = 0; start < problems.size();) {
        const size_t end = problems.find('\n', start);
        log::info("font: {}", problems.substr(start, end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (!ok) {
        log::error("font: the default font has no ASCII glyphs");
        return false;
    }
    const Image& sheet = set_.sheet();
    texture_.create(sheet.width, sheet.height, 1);
    texture_.upload(0, 0, 0, sheet);
    texture_.setFilter(GL_NEAREST, GL_NEAREST);
    return true;
}

float Font::draw(GuiBatch& batch, std::string_view text, float x, float y, uint32_t argb, bool shadow) const {
    if ((argb & 0xFC000000u) == 0) argb |= 0xFF000000u; // vanilla Font.adjustColor
    const std::u32string cps = decodeUtf8(text);
    // Obfuscated characters change every draw, the same in the shadow and the text.
    random_ = random_ * 1664525u + 1013904223u;
    if (shadow) drawPass(batch, cps, x + 1.0f, y + 1.0f, argb, true, random_);
    return drawPass(batch, cps, x, y, argb, false, random_);
}

void Font::glyphQuads(std::string_view text, float x, float y, uint32_t argb, std::vector<GlyphQuad>& out) const {
    if ((argb & 0xFC000000u) == 0) argb |= 0xFF000000u;
    const Image& sheet = set_.sheet();
    const float invW = 1.0f / float(sheet.width), invH = 1.0f / float(sheet.height);
    const std::u32string cps = decodeUtf8(text);
    uint32_t random = random_;
    set_.forEachStyled(cps, TextStyle{}, [&](char32_t c, const TextStyle& style) {
        const uint32_t color = style.hasColor ? (argb & 0xFF000000u) | style.color : argb;
        const char32_t shown = style.obfuscated ? set_.obfuscated(c, random = random * 1664525u + 1013904223u) : c;
        const Glyph& g = set_.glyph(shown);
        if (g.visible) {
            const float w = float(g.cellW) * g.scale, h = float(g.cellH) * g.scale;
            const float top = y + g.up, bottom = top + h;
            const float skewTop = style.italic ? 1.0f - 0.25f * g.up : 0.0f;
            const float skewBottom = style.italic ? 1.0f - 0.25f * (g.up + h) : 0.0f;
            const glm::vec2 uv0(float(g.sheetX) * invW, float(g.sheetY) * invH);
            const glm::vec2 uv1(float(g.sheetX + g.cellW) * invW, float(g.sheetY + g.cellH) * invH);
            for (int pass = 0; pass < (style.bold ? 2 : 1); ++pass) {
                const float gx = x + float(pass) * FontSet::kBoldOffset;
                out.push_back({{{gx + skewTop, top}, {gx + skewBottom, bottom}, {gx + skewBottom + w, bottom}, {gx + skewTop + w, top}},
                               uv0, uv1, color});
            }
        }
        x += set_.advance(c, style);
    });
}

float Font::drawPass(GuiBatch& batch, std::u32string_view text, float x, float y, uint32_t argb, bool shadowPass,
                     uint32_t random) const {
    const Image& sheet = set_.sheet();
    const float invW = 1.0f / float(sheet.width), invH = 1.0f / float(sheet.height);
    // Shadows are the color at a quarter brightness (vanilla ARGB.scaleRGB(color, 0.25)).
    auto shade = [&](uint32_t c) {
        if (!shadowPass) return c;
        const uint32_t r = ((c >> 16) & 0xFF) / 4, g = ((c >> 8) & 0xFF) / 4, b = (c & 0xFF) / 4;
        return (c & 0xFF000000u) | (r << 16) | (g << 8) | b;
    };
    set_.forEachStyled(text, TextStyle{}, [&](char32_t c, const TextStyle& style) {
        const uint32_t color = shade(style.hasColor ? (argb & 0xFF000000u) | style.color : argb);
        const char32_t shown = style.obfuscated ? set_.obfuscated(c, random = random * 1664525u + 1013904223u) : c;
        const Glyph& g = set_.glyph(shown);
        const float advance = set_.advance(c, style);
        if (g.visible) {
            const float w = float(g.cellW) * g.scale, h = float(g.cellH) * g.scale;
            const float top = y + g.up, bottom = top + h;
            // Italic shears the top right (vanilla: 1 - 0.25 * y offset).
            const float skewTop = style.italic ? 1.0f - 0.25f * g.up : 0.0f;
            const float skewBottom = style.italic ? 1.0f - 0.25f * (g.up + h) : 0.0f;
            const float u0 = float(g.sheetX) * invW, v0 = float(g.sheetY) * invH;
            const float u1 = float(g.sheetX + g.cellW) * invW, v1 = float(g.sheetY + g.cellH) * invH;
            for (int pass = 0; pass < (style.bold ? 2 : 1); ++pass) {
                const float gx = x + float(pass) * FontSet::kBoldOffset;
                const glm::vec2 corners[4] = {{gx + skewTop, top}, {gx + skewBottom, bottom},
                                              {gx + skewBottom + w, bottom}, {gx + skewTop + w, top}};
                batch.blitQuad(texture_, corners, u0, v0, u1, v1, color);
            }
        }
        if (style.strikethrough) batch.fill(x - 1.0f, y + 3.5f, x + advance, y + 4.5f, color);
        if (style.underlined) batch.fill(x - 1.0f, y + 8.0f, x + advance, y + 9.0f, color);
        x += advance;
    });
    return x;
}

} // namespace mc
