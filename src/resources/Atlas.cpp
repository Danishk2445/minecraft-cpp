#include "resources/Atlas.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <functional>

#include "core/Log.h"

namespace mc {
namespace {

constexpr int kMaxAtlasSize = 16384;

struct LoadedSprite {
    std::string name;
    std::vector<Image> frames; // level-0 frames
    int width = 0, height = 0;
    std::optional<AnimationMeta> animation;
    std::optional<GuiScaling> gui;
};

const std::array<float, 256>& pow22Table() {
    static const std::array<float, 256> table = [] {
        std::array<float, 256> t{};
        for (int i = 0; i < 256; ++i) t[size_t(i)] = float(std::pow(double(float(i) / 255.0f), 2.2));
        return t;
    }();
    return table;
}

uint8_t gammaBlend(const uint8_t* c0, const uint8_t* c1, const uint8_t* c2, const uint8_t* c3, int ch) {
    const auto& p = pow22Table();
    const float sum = p[c0[ch]] + p[c1[ch]] + p[c2[ch]] + p[c3[ch]];
    const float g = float(std::pow(double(sum) * 0.25, 0.45454545454545453));
    return uint8_t(std::clamp(int(double(g) * 255.0), 0, 255));
}

void alphaBlend(const uint8_t* c0, const uint8_t* c1, const uint8_t* c2, const uint8_t* c3, bool transparent,
                uint8_t* out) {
    if (!transparent) {
        for (int ch = 0; ch < 4; ++ch) out[ch] = gammaBlend(c0, c1, c2, c3, ch);
        return;
    }
    // Fully transparent texels contribute nothing, yet the sum is still divided by
    // four. This darkens and thins cutout edges at distance exactly as vanilla does.
    const auto& p = pow22Table();
    float acc[4] = {0, 0, 0, 0};
    for (const uint8_t* c : {c0, c1, c2, c3}) {
        if (c[3] == 0) continue;
        for (int ch = 0; ch < 4; ++ch) acc[ch] += p[c[ch]];
    }
    for (int ch = 0; ch < 4; ++ch) {
        const float v = acc[ch] / 4.0f;
        out[ch] = uint8_t(std::clamp(int(std::pow(double(v), 0.45454545454545453) * 255.0), 0, 255));
    }
    if (out[3] < 96) out[3] = 0;
}

std::optional<LoadedSprite> loadSprite(const ResourcePack& pack, const std::string& spriteName,
                                       const ResourceLocation& texture, std::string* error) {
    const auto path = pack.resource(texture, "textures", ".png");
    std::string err;
    auto image = Image::loadPng(path, &err);
    if (!image) {
        if (error) *error = err;
        return std::nullopt;
    }
    LoadedSprite s;
    s.name = spriteName;
    auto metaPath = path;
    metaPath += ".mcmeta";
    if (std::filesystem::exists(metaPath)) {
        if (auto j = pack.json(metaPath, &err)) {
            TextureMeta meta = parseTextureMeta(*j);
            s.animation = std::move(meta.animation);
            s.gui = meta.gui;
        } else if (error) {
            log::warn("{}", err);
        }
    }
    if (s.animation) {
        int fw = s.animation->width, fh = s.animation->height;
        if (fw < 0 && fh < 0) fw = fh = std::min(image->width, image->height);
        if (fw < 0) fw = image->width;
        if (fh < 0) fh = image->height;
        if (fw <= 0 || fh <= 0 || image->width % fw || image->height % fh) {
            if (error) *error = std::format("{}: frame size {}x{} does not divide {}x{}", path.string(), fw, fh,
                                            image->width, image->height);
            return std::nullopt;
        }
        const int cols = image->width / fw;
        const int count = cols * (image->height / fh);
        for (int i = 0; i < count; ++i) s.frames.push_back(image->region((i % cols) * fw, (i / cols) * fh, fw, fh));
        s.width = fw;
        s.height = fh;
        if (s.frames.size() <= 1) s.animation.reset();
    } else {
        s.width = image->width;
        s.height = image->height;
        s.frames.push_back(std::move(*image));
    }
    return s;
}

uint32_t nextPow2(uint32_t v) {
    uint32_t p = 1;
    while (p < v) p <<= 1;
    return p;
}

std::optional<Atlas> stitch(std::string name, std::vector<LoadedSprite> loaded, int mipLevels, AtlasStats* stats) {
    // The missing-texture sprite always exists.
    LoadedSprite missing;
    missing.name = "minecraft:missingno";
    missing.frames.push_back(missingTextureImage());
    missing.width = missing.height = 16;
    loaded.push_back(std::move(missing));

    const int align = 1 << mipLevels;
    struct Item {
        int index, w, h;
    };
    std::vector<Item> items;
    items.reserve(loaded.size());
    for (size_t i = 0; i < loaded.size(); ++i) {
        const auto& s = loaded[i];
        items.push_back({int(i), (s.width + align - 1) / align * align, (s.height + align - 1) / align * align});
    }
    std::sort(items.begin(), items.end(), [&](const Item& a, const Item& b) {
        if (a.h != b.h) return a.h > b.h;
        if (a.w != b.w) return a.w > b.w;
        return loaded[size_t(a.index)].name < loaded[size_t(b.index)].name;
    });

    // Shelf packing: the narrowest power-of-two width whose packed height fits a square.
    std::vector<std::pair<int, int>> positions(loaded.size());
    int atlasW = 0, usedH = 0;
    for (int w = 64; w <= kMaxAtlasSize; w *= 2) {
        int x = 0, y = 0, rowH = 0;
        bool fits = true;
        for (const Item& it : items) {
            if (it.w > w) {
                fits = false;
                break;
            }
            if (x + it.w > w) {
                x = 0;
                y += rowH;
                rowH = 0;
            }
            positions[size_t(it.index)] = {x, y};
            x += it.w;
            rowH = std::max(rowH, it.h);
        }
        const int h = y + rowH;
        if (fits && (h <= w || w == kMaxAtlasSize) && h <= kMaxAtlasSize) {
            atlasW = w;
            usedH = h;
            break;
        }
    }
    if (atlasW == 0) {
        if (stats) stats->errors.push_back(std::format("atlas {} does not fit in {}px", name, kMaxAtlasSize));
        return std::nullopt;
    }

    Atlas atlas;
    atlas.name = std::move(name);
    atlas.width = atlasW;
    atlas.height = int(nextPow2(uint32_t(std::max(usedH, 1))));
    atlas.mipLevels = mipLevels;
    for (int l = 0; l <= mipLevels; ++l)
        atlas.levels.emplace_back(std::max(1, atlas.width >> l), std::max(1, atlas.height >> l), 0u);

    atlas.sprites.resize(loaded.size());
    for (size_t i = 0; i < loaded.size(); ++i) {
        auto& src = loaded[i];
        SpriteInfo& s = atlas.sprites[i];
        s.name = src.name;
        s.x = positions[i].first;
        s.y = positions[i].second;
        s.width = src.width;
        s.height = src.height;
        s.gui = src.gui;
        s.u0 = float(s.x) / float(atlas.width);
        s.v0 = float(s.y) / float(atlas.height);
        s.u1 = float(s.x + s.width) / float(atlas.width);
        s.v1 = float(s.y + s.height) / float(atlas.height);
        for (const Image& f : src.frames)
            for (size_t p = 3; p < f.pixels.size(); p += 4) {
                const uint8_t a = f.pixels[p];
                if (a == 0) s.transparent = true;
                else if (a < 255) s.translucent = true;
            }

        std::vector<std::vector<Image>> frameMips;
        for (const Image& f : src.frames) frameMips.push_back(generateMipChain(f, mipLevels));
        for (int l = 0; l <= mipLevels; ++l) atlas.levels[size_t(l)].blit(frameMips[0][size_t(l)], s.x >> l, s.y >> l);

        if (src.animation) {
            SpriteAnimation anim;
            anim.sprite = int(i);
            anim.interpolate = src.animation->interpolate;
            anim.sequence = src.animation->frames;
            if (anim.sequence.empty())
                for (int f = 0; f < int(src.frames.size()); ++f)
                    anim.sequence.push_back({f, src.animation->frameTime});
            for (auto& step : anim.sequence) step.index = std::clamp(step.index, 0, int(src.frames.size()) - 1);
            anim.frameMips = std::move(frameMips);
            s.animation = int(atlas.animations.size());
            atlas.animations.push_back(std::move(anim));
        }
    }
    atlas.rebuildIndex();
    atlas.missing = atlas.find("minecraft:missingno");
    if (stats) {
        stats->sprites = int(atlas.sprites.size());
        stats->animated = int(atlas.animations.size());
    }
    return atlas;
}

} // namespace

int Atlas::find(std::string_view id) const {
    auto it = index_.find(std::string(id));
    return it == index_.end() ? missing : it->second;
}

void Atlas::rebuildIndex() {
    index_.clear();
    for (size_t i = 0; i < sprites.size(); ++i) index_.emplace(sprites[i].name, int(i));
}

Image Atlas::spriteImage(int i) const {
    const SpriteInfo& s = sprites[size_t(i)];
    if (s.animation >= 0) return animations[size_t(s.animation)].frameMips[0][0];
    return levels[0].region(s.x, s.y, s.width, s.height);
}

Image missingTextureImage() {
    Image img(16, 16);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            uint8_t* p = img.px(x, y);
            const bool magenta = (y < 8) != (x < 8);
            p[0] = magenta ? 0xF8 : 0;
            p[1] = 0;
            p[2] = magenta ? 0xF8 : 0;
            p[3] = 255;
        }
    return img;
}

std::vector<Image> generateMipChain(const Image& base, int mipLevels) {
    std::vector<Image> out;
    out.push_back(base);
    bool transparent = false;
    for (size_t p = 3; p < base.pixels.size(); p += 4)
        if (base.pixels[p] == 0) {
            transparent = true;
            break;
        }
    for (int l = 1; l <= mipLevels; ++l) {
        const Image& prev = out.back();
        Image cur(std::max(1, prev.width >> 1), std::max(1, prev.height >> 1));
        for (int y = 0; y < cur.height; ++y)
            for (int x = 0; x < cur.width; ++x) {
                const int x0 = std::min(x * 2, prev.width - 1), x1 = std::min(x * 2 + 1, prev.width - 1);
                const int y0 = std::min(y * 2, prev.height - 1), y1 = std::min(y * 2 + 1, prev.height - 1);
                alphaBlend(prev.px(x0, y0), prev.px(x1, y0), prev.px(x0, y1), prev.px(x1, y1), transparent,
                           cur.px(x, y));
            }
        out.push_back(std::move(cur));
    }
    return out;
}

namespace {

// Adds the sprites of atlases/<atlasName>.json; false if the definition is missing.
bool collectSources(const ResourcePack& pack, std::string_view atlasName,
                    const std::function<void(const std::string&, const ResourceLocation&)>& add, AtlasStats* stats) {
    std::string err;
    const auto def = pack.json(pack.file("minecraft", std::format("atlases/{}.json", atlasName)), &err);
    if (!def) {
        if (stats) stats->errors.push_back(err);
        return false;
    }
    for (const auto& src : def->value("sources", Json::array())) {
        const std::string type = canonicalId(jsonGet<std::string>(src, "type", ""));
        if (type == "minecraft:directory") {
            const std::string source = jsonGet<std::string>(src, "source", "");
            const std::string prefix = jsonGet<std::string>(src, "prefix", "");
            for (const auto& rel : pack.list("minecraft", "textures/" + source, ".png")) {
                ResourceLocation tex{"minecraft", source + "/" + rel};
                add("minecraft:" + prefix + rel, tex);
            }
        } else if (type == "minecraft:single") {
            const auto res = ResourceLocation::parse(jsonGet<std::string>(src, "resource", ""));
            const std::string spriteName = canonicalId(jsonGet<std::string>(src, "sprite", res.str()));
            add(spriteName, res);
        } else if (type == "minecraft:paletted_permutations") {
            continue; // armor trims: out of scope
        } else {
            log::warn("atlas {}: unsupported source type {}", atlasName, type);
        }
    }
    return true;
}

} // namespace

std::optional<Atlas> buildAtlas(const ResourcePack& pack, std::string_view atlasName, int mipLevels,
                                AtlasStats* stats) {
    const std::string_view names[1] = {atlasName};
    return buildAtlas(pack, names, {}, mipLevels, stats);
}

std::optional<Atlas> buildAtlas(const ResourcePack& pack, std::span<const std::string_view> atlasNames,
                                const std::vector<std::string>& extraTextures, int mipLevels, AtlasStats* stats) {
    std::vector<LoadedSprite> loaded;
    std::unordered_map<std::string, size_t> seen;
    auto add = [&](const std::string& spriteName, const ResourceLocation& tex) {
        std::string e;
        auto s = loadSprite(pack, spriteName, tex, &e);
        if (!s) {
            if (stats) stats->errors.push_back(e);
            return;
        }
        if (auto it = seen.find(spriteName); it != seen.end()) {
            loaded[it->second] = std::move(*s); // later sources override earlier ones
        } else {
            seen.emplace(spriteName, loaded.size());
            loaded.push_back(std::move(*s));
        }
    };
    for (size_t i = 0; i < atlasNames.size(); ++i)
        if (!collectSources(pack, atlasNames[i], add, stats) && i == 0) return std::nullopt;
    for (const std::string& id : extraTextures)
        if (!seen.contains(canonicalId(id))) add(canonicalId(id), ResourceLocation::parse(id));
    return stitch(std::string(atlasNames.empty() ? std::string_view("atlas") : atlasNames[0]), std::move(loaded),
                  mipLevels, stats);
}

std::optional<Atlas> buildAtlasFromList(const ResourcePack& pack, std::string name,
                                        const std::vector<std::string>& textureIds, int mipLevels,
                                        AtlasStats* stats) {
    std::vector<LoadedSprite> loaded;
    for (const auto& id : textureIds) {
        std::string e;
        auto s = loadSprite(pack, canonicalId(id), ResourceLocation::parse(id), &e);
        if (s) loaded.push_back(std::move(*s));
        else if (stats) stats->errors.push_back(e);
    }
    return stitch(std::move(name), std::move(loaded), mipLevels, stats);
}

AtlasAnimator::AtlasAnimator(const Atlas& atlas) : atlas_(atlas), states_(atlas.animations.size()) {}

void AtlasAnimator::tick(const UploadFn& upload) {
    for (size_t i = 0; i < atlas_.animations.size(); ++i) {
        const SpriteAnimation& anim = atlas_.animations[i];
        State& st = states_[i];
        const int len = int(anim.sequence.size());
        bool advanced = false;
        if (++st.subTick >= anim.sequence[size_t(st.step)].time) {
            st.subTick = 0;
            st.step = (st.step + 1) % len;
            advanced = true;
        }
        const SpriteInfo& sprite = atlas_.sprites[size_t(anim.sprite)];
        const int cur = anim.sequence[size_t(st.step)].index;
        if (!anim.interpolate) {
            if (!advanced) continue;
            for (int l = 0; l <= atlas_.mipLevels; ++l) upload(sprite, l, anim.frameMips[size_t(cur)][size_t(l)]);
            continue;
        }
        // Interpolated: blend toward the next step's frame every tick (vanilla mixes in ARGB space).
        const int next = anim.sequence[size_t((st.step + 1) % len)].index;
        const double t = 1.0 - double(st.subTick) / double(anim.sequence[size_t(st.step)].time);
        for (int l = 0; l <= atlas_.mipLevels; ++l) {
            const Image& a = anim.frameMips[size_t(cur)][size_t(l)];
            const Image& b = anim.frameMips[size_t(next)][size_t(l)];
            Image mixed(a.width, a.height);
            for (size_t p = 0; p < mixed.pixels.size(); ++p)
                mixed.pixels[p] = uint8_t(int(t * a.pixels[p] + (1.0 - t) * b.pixels[p]));
            upload(sprite, l, mixed);
        }
    }
}

} // namespace mc
