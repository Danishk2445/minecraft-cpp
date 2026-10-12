// FontSet.h -- the pack's default font (font/default.json and the fonts it
// references): bitmap glyph sheets and space advances with vanilla glyph
// metrics, plus the legacy § formatting codes. GL-free; ui/Font draws with it.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/Image.h"

namespace mc {

class ResourcePack;

struct Glyph {
    float advance = 0.0f; // GUI pixels (vanilla: (int)(0.5 + actualWidth * scale) + 1)
    // Bitmap glyphs: the cell in the sheet (texels) and how it is placed.
    int sheetX = 0, sheetY = 0, cellW = 0, cellH = 0;
    float scale = 1.0f; // GUI pixels per texel (provider height / cell height)
    float up = 0.0f;    // top of the cell below the top of the line (7 - ascent)
    bool visible = false; // has an image (space glyphs don't)
};

// Text style from § codes (vanilla Style after applyLegacyFormat).
struct TextStyle {
    bool hasColor = false;
    uint32_t color = 0xFFFFFF; // RGB when hasColor
    bool bold = false, italic = false, underlined = false, strikethrough = false, obfuscated = false;
};

// UTF-8 to code points; malformed bytes become U+FFFD.
std::u32string decodeUtf8(std::string_view text);
std::string encodeUtf8(std::u32string_view text);
// RGB of a § color code (0-9, a-f), or -1 for other codes.
int64_t legacyColor(char32_t code);
// Removes § codes (vanilla ChatFormatting.stripFormatting).
std::string stripFormatting(std::string_view text);

class FontSet {
public:
    static constexpr float kBoldOffset = 1.0f;

    // Loads font/<id>.json ("minecraft:default"). Providers the pack lacks
    // (unifont) are skipped and reported in `error` without failing.
    bool load(const ResourcePack& pack, std::string_view id = "minecraft:default", std::string* error = nullptr);

    // The first provider's glyph for `c`, or the missing-glyph box.
    const Glyph& glyph(char32_t c) const;
    bool has(char32_t c) const;
    // All glyph images, one sheet (RGBA); the missing glyph sits at the top left.
    const Image& sheet() const { return sheet_; }

    // Advance of `c` in `style` (bold adds one pixel).
    float advance(char32_t c, const TextStyle& style) const {
        return glyph(c).advance + (style.bold ? kBoldOffset : 0.0f);
    }
    // Width of formatted text in GUI pixels (vanilla StringSplitter.stringWidth).
    float width(std::string_view text) const;
    // Splits formatted text into lines no wider than `width`, at spaces when possible;
    // each new line repeats the formatting in effect (vanilla ComponentRenderUtils.wrapComponents).
    std::vector<std::string> split(std::string_view text, float width) const;
    // Calls f(codePoint, style) for every character outside § codes, which update the
    // style; §r resets it to `base` (vanilla StringDecomposer.iterateFormatted).
    template <class F>
    void forEachStyled(std::u32string_view text, const TextStyle& base, F&& f) const {
        TextStyle style = base;
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == U'§') {
                if (i + 1 >= text.size()) break;
                applyFormat(style, base, text[++i]);
                continue;
            }
            f(text[i], style);
        }
    }
    static void applyFormat(TextStyle& style, const TextStyle& base, char32_t code);

    // A glyph with the same advance as `c` for obfuscated text, picked by `random`.
    char32_t obfuscated(char32_t c, uint32_t random) const;

private:
    void add(char32_t c, const Glyph& g);

    std::unordered_map<char32_t, Glyph> glyphs_; // beyond ASCII
    std::array<Glyph, 128> ascii_{};              // ASCII fast path
    std::array<bool, 128> hasAscii_{};
    Glyph missing_;
    Image sheet_;
    // Glyphs grouped by advance, for obfuscated text.
    std::unordered_map<int, std::vector<char32_t>> byAdvance_;
};

} // namespace mc
