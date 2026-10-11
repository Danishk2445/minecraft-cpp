#include <gtest/gtest.h>

#include "resources/FontSet.h"
#include "resources/ResourcePack.h"
#include "ui/GuiScale.h"

using namespace mc;

namespace {

const FontSet& defaultFont() {
    static const FontSet font = [] {
        FontSet f;
        const ResourcePack pack(MC_DEFAULT_PACK_DIR);
        f.load(pack);
        return f;
    }();
    return font;
}

} // namespace

// Vanilla glyph metrics: advance = (int)(0.5 + actualWidth * scale) + 1, space 4.
TEST(Font, GlyphWidths) {
    const FontSet& f = defaultFont();
    EXPECT_FLOAT_EQ(f.glyph(U'i').advance, 2.0f);
    EXPECT_FLOAT_EQ(f.glyph(U'l').advance, 3.0f);
    EXPECT_FLOAT_EQ(f.glyph(U' ').advance, 4.0f);
    EXPECT_FLOAT_EQ(f.glyph(U'A').advance, 6.0f);
    EXPECT_FLOAT_EQ(f.glyph(U'!').advance, 2.0f);
    EXPECT_FLOAT_EQ(f.width("Hello World"), 6 + 6 + 3 + 3 + 6 + 4 + 6 + 6 + 6 + 3 + 6);
    // ASCII glyphs sit with their top on the line; accented ones reach 3 pixels above it.
    EXPECT_FLOAT_EQ(f.glyph(U'A').up, 0.0f);
    ASSERT_TRUE(f.has(U'É')); // É
    EXPECT_FLOAT_EQ(f.glyph(U'É').up, -3.0f);
    EXPECT_TRUE(f.has(U'Ж')); // Ж (nonlatin_european)
    // Characters no provider covers (unifont is not in the pack) draw the missing box.
    EXPECT_FALSE(f.has(U'中'));
    EXPECT_FLOAT_EQ(f.glyph(U'中').advance, 6.0f);
}

TEST(Font, FormattingCodes) {
    const FontSet& f = defaultFont();
    // Colors don't change the width; bold adds a pixel per character until a color resets it.
    EXPECT_FLOAT_EQ(f.width("§cHi"), f.width("Hi"));
    EXPECT_FLOAT_EQ(f.width("§lHi"), f.width("Hi") + 2.0f);
    EXPECT_FLOAT_EQ(f.width("§lH§ai"), f.width("Hi") + 1.0f);
    EXPECT_FLOAT_EQ(f.width("§lH§ri"), f.width("Hi") + 1.0f);
    EXPECT_FLOAT_EQ(f.width("Hi§"), f.width("Hi")); // a trailing § is dropped
    EXPECT_FLOAT_EQ(f.width("§zHi"), f.width("Hi")); // unknown codes vanish too
    EXPECT_EQ(stripFormatting("§aHello§r §lW§korld"), "Hello World");

    TextStyle style;
    FontSet::applyFormat(style, TextStyle{}, U'l');
    FontSet::applyFormat(style, TextStyle{}, U'o');
    EXPECT_TRUE(style.bold && style.italic);
    FontSet::applyFormat(style, TextStyle{}, U'E');
    EXPECT_FALSE(style.bold || style.italic);
    EXPECT_TRUE(style.hasColor);
    EXPECT_EQ(style.color, 0xFFFF55u);
    // Obfuscated characters swap for glyphs of the same width.
    for (uint32_t r = 0; r < 20; ++r) EXPECT_FLOAT_EQ(f.glyph(f.obfuscated(U'A', r)).advance, 6.0f);
}

TEST(Font, Utf8RoundTrip) {
    const std::string text = "AéЖ中\U0001F600";
    const std::u32string cps = decodeUtf8(text);
    ASSERT_EQ(cps.size(), 5u);
    EXPECT_EQ(cps[4], U'\U0001F600');
    EXPECT_EQ(encodeUtf8(cps), text);
    EXPECT_EQ(decodeUtf8("\xFF" "a"), std::u32string(U"�a"));
}

// Vanilla Window.calculateScale.
TEST(GuiScale, AutoScale) {
    EXPECT_EQ(autoGuiScale({1920, 1080}), 4);
    EXPECT_EQ(autoGuiScale({1280, 720}), 3);
    EXPECT_EQ(autoGuiScale({2560, 1440}), 6);
    EXPECT_EQ(autoGuiScale({854, 480}), 2);
    EXPECT_EQ(autoGuiScale({320, 240}), 1);
    EXPECT_EQ(autoGuiScale({1920, 1080}, 2), 2);
}
