#include "resources/FontSet.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "core/Json.h"
#include "resources/ResourcePack.h"

namespace mc {
namespace {

// One provider of a font, in priority order (the first one with a glyph wins).
struct Provider {
    std::unordered_map<char32_t, float> spaces; // type "space"
    // type "bitmap"
    Image image;
    std::vector<std::u32string> rows;
    int height = 8, ascent = 0;
    bool bitmap = false;
};

void collectProviders(const ResourcePack& pack, std::string_view id, int depth, std::vector<Provider>& out,
                      std::string& problems) {
    if (depth > 8) {
        problems += std::format("font {}: references nest too deep\n", id);
        return;
    }
    std::string err;
    const ResourceLocation loc = ResourceLocation::parse(id);
    auto doc = pack.json(pack.resource(loc, "font", ".json"), &err);
    if (!doc) {
        problems += std::format("font {}: {}\n", id, err);
        return;
    }
    for (const Json& p : doc->value("providers", Json::array())) {
        const std::string type = canonicalId(p.value("type", std::string()));
        // Providers filtered on the "uniform" or "jp" options are kept only when those are off.
        if (auto filter = p.find("filter"); filter != p.end() && filter->is_object()) {
            bool keep = true;
            for (auto& [option, wanted] : filter->items())
                if (wanted.is_boolean() && wanted.get<bool>()) keep = false; // options default to off
            if (!keep) continue;
        }
        if (type == "minecraft:reference") {
            collectProviders(pack, p.value("id", std::string()), depth + 1, out, problems);
        } else if (type == "minecraft:space") {
            Provider prov;
            const Json advances = p.value("advances", Json::object());
            for (auto& [key, value] : advances.items()) {
                const std::u32string cp = decodeUtf8(key);
                if (cp.size() == 1 && value.is_number()) prov.spaces[cp[0]] = value.get<float>();
            }
            out.push_back(std::move(prov));
        } else if (type == "minecraft:bitmap") {
            Provider prov;
            prov.bitmap = true;
            const std::string file = p.value("file", std::string());
            auto img = Image::loadPng(pack.resource(ResourceLocation::parse(file), "textures", ""), &err);
            if (!img) {
                problems += std::format("font {}: bitmap {}: {}\n", id, file, err);
                continue;
            }
            prov.image = std::move(*img);
            prov.height = p.value("height", 8);
            prov.ascent = p.value("ascent", 0);
            for (const Json& row : p.value("chars", Json::array()))
                if (row.is_string()) prov.rows.push_back(decodeUtf8(row.get<std::string>()));
            if (prov.rows.empty() || prov.rows[0].empty()) {
                problems += std::format("font {}: bitmap {} has no chars\n", id, file);
                continue;
            }
            out.push_back(std::move(prov));
        } else {
            // unihex (unifont) and ttf providers are not in the pack.
            problems += std::format("font {}: skipped provider {}\n", id, type);
        }
    }
}

// Vanilla BitmapProvider.getActualGlyphWidth: the rightmost non-empty column + 1.
int actualGlyphWidth(const Image& image, int cellX, int cellY, int cellW, int cellH) {
    for (int x = cellW - 1; x >= 0; --x)
        for (int y = 0; y < cellH; ++y)
            if (image.alpha(cellX + x, cellY + y) != 0) return x + 1;
    return 0;
}

} // namespace

std::u32string decodeUtf8(std::string_view s) {
    std::u32string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const auto b = uint8_t(s[i]);
        int len = b < 0x80 ? 1 : (b >> 5) == 6 ? 2 : (b >> 4) == 14 ? 3 : (b >> 3) == 30 ? 4 : 0;
        char32_t cp = len == 1 ? b : len == 2 ? (b & 0x1F) : len == 3 ? (b & 0x0F) : (b & 0x07);
        bool ok = len > 0 && i + size_t(len) <= s.size();
        for (int k = 1; ok && k < len; ++k) {
            const auto c = uint8_t(s[i + size_t(k)]);
            if ((c >> 6) != 2) ok = false;
            cp = (cp << 6) | (c & 0x3F);
        }
        if (!ok) {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        out.push_back(cp);
        i += size_t(len);
    }
    return out;
}

std::string encodeUtf8(std::u32string_view text) {
    std::string out;
    for (char32_t c : text) {
        if (c < 0x80) {
            out += char(c);
        } else if (c < 0x800) {
            out += char(0xC0 | (c >> 6));
            out += char(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += char(0xE0 | (c >> 12));
            out += char(0x80 | ((c >> 6) & 0x3F));
            out += char(0x80 | (c & 0x3F));
        } else {
            out += char(0xF0 | (c >> 18));
            out += char(0x80 | ((c >> 12) & 0x3F));
            out += char(0x80 | ((c >> 6) & 0x3F));
            out += char(0x80 | (c & 0x3F));
        }
    }
    return out;
}

int64_t legacyColor(char32_t code) {
    static constexpr uint32_t kColors[16] = {0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA,
                                             0xFFAA00, 0xAAAAAA, 0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
                                             0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF};
    if (code >= U'0' && code <= U'9') return kColors[code - U'0'];
    if (code >= U'a' && code <= U'f') return kColors[10 + code - U'a'];
    if (code >= U'A' && code <= U'F') return kColors[10 + code - U'A'];
    return -1;
}

std::string stripFormatting(std::string_view text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        // § is C2 A7 in UTF-8; it and the code point after it go.
        if (uint8_t(text[i]) == 0xC2 && i + 1 < text.size() && uint8_t(text[i + 1]) == 0xA7) {
            i += 2;
            if (i < text.size() && (uint8_t(text[i]) & 0xC0) == 0xC0)
                while (i + 1 < text.size() && (uint8_t(text[i + 1]) & 0xC0) == 0x80) ++i;
            continue;
        }
        out += text[i];
    }
    return out;
}

void FontSet::applyFormat(TextStyle& style, const TextStyle& base, char32_t code) {
    // Vanilla ChatFormatting codes are case-insensitive; unknown codes do nothing.
    const char32_t c = code >= U'A' && code <= U'Z' ? code + 32 : code;
    if (const int64_t rgb = legacyColor(c); rgb >= 0) {
        // A color clears the other formats (Style.applyLegacyFormat).
        style = TextStyle{};
        style.hasColor = true;
        style.color = uint32_t(rgb);
        return;
    }
    switch (c) {
    case U'k': style.obfuscated = true; break;
    case U'l': style.bold = true; break;
    case U'm': style.strikethrough = true; break;
    case U'n': style.underlined = true; break;
    case U'o': style.italic = true; break;
    case U'r': style = base; break;
    default: break;
    }
}

void FontSet::add(char32_t c, const Glyph& g) {
    if (c < 128) {
        if (hasAscii_[c]) return;
        hasAscii_[c] = true;
        ascii_[c] = g;
    } else if (!glyphs_.emplace(c, g).second) {
        return;
    }
    byAdvance_[int(std::ceil(g.advance))].push_back(c);
}

bool FontSet::load(const ResourcePack& pack, std::string_view id, std::string* error) {
    glyphs_.clear();
    byAdvance_.clear();
    hasAscii_.fill(false);
    std::string problems;
    std::vector<Provider> providers;
    collectProviders(pack, id, 0, providers, problems);

    // Sheet: the missing glyph's 8-texel band on top, then each bitmap stacked below.
    int sheetW = 8, sheetH = 8;
    for (const Provider& p : providers)
        if (p.bitmap) sheetW = std::max(sheetW, p.image.width), sheetH += p.image.height;
    sheet_ = Image(sheetW, sheetH);
    // Vanilla SpecialGlyphs.MISSING: the outline of a 5x8 box, advancing 6.
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 5; ++x)
            if (x == 0 || x == 4 || y == 0 || y == 7) {
                uint8_t* px = sheet_.px(x, y);
                px[0] = px[1] = px[2] = px[3] = 255;
            }
    missing_ = Glyph{6.0f, 0, 0, 5, 8, 1.0f, 0.0f, true};

    int y0 = 8;
    for (const Provider& p : providers) {
        if (!p.bitmap) {
            for (const auto& [c, advance] : p.spaces) add(c, Glyph{advance});
            continue;
        }
        sheet_.blit(p.image, 0, y0);
        const int cols = int(p.rows[0].size()), rowCount = int(p.rows.size());
        const int cellW = p.image.width / cols, cellH = p.image.height / rowCount;
        const float scale = float(p.height) / float(cellH);
        for (int r = 0; r < rowCount; ++r) {
            if (int(p.rows[size_t(r)].size()) != cols) {
                problems += std::format("font: a bitmap row has {} chars instead of {}\n", p.rows[size_t(r)].size(), cols);
                continue;
            }
            for (int col = 0; col < cols; ++col) {
                const char32_t c = p.rows[size_t(r)][size_t(col)];
                if (c == 0) continue;
                const int actual = actualGlyphWidth(p.image, col * cellW, r * cellH, cellW, cellH);
                Glyph g;
                g.advance = float(int(0.5f + float(actual) * scale) + 1);
                g.sheetX = col * cellW;
                g.sheetY = y0 + r * cellH;
                g.cellW = cellW;
                g.cellH = cellH;
                g.scale = scale;
                g.up = 7.0f - float(p.ascent);
                g.visible = true;
                add(c, g);
            }
        }
        y0 += p.image.height;
    }
    for (auto& [w, list] : byAdvance_) std::sort(list.begin(), list.end());
    if (error) *error = problems;
    return hasAscii_[U'A'];
}

const Glyph& FontSet::glyph(char32_t c) const {
    if (c < 128) return hasAscii_[c] ? ascii_[c] : missing_;
    auto it = glyphs_.find(c);
    return it == glyphs_.end() ? missing_ : it->second;
}

bool FontSet::has(char32_t c) const { return c < 128 ? hasAscii_[c] : glyphs_.contains(c); }

float FontSet::width(std::string_view text) const {
    float w = 0.0f;
    forEachStyled(decodeUtf8(text), TextStyle{}, [&](char32_t c, const TextStyle& s) { w += advance(c, s); });
    return w;
}

char32_t FontSet::obfuscated(char32_t c, uint32_t random) const {
    auto it = byAdvance_.find(int(std::ceil(glyph(c).advance)));
    if (it == byAdvance_.end() || it->second.empty()) return c;
    return it->second[random % it->second.size()];
}

} // namespace mc
