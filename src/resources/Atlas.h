// Atlas.h -- CPU-side texture atlas: sprite collection from atlases/<name>.json,
// packing, vanilla mipmaps and tick-driven animation. GL upload lives in gfx/.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/Image.h"
#include "resources/ResourcePack.h"
#include "resources/TextureMeta.h"

namespace mc {

struct SpriteInfo {
    std::string name; // canonical id, e.g. "minecraft:block/stone"
    int x = 0, y = 0;  // level-0 position
    int width = 0, height = 0; // frame size in texels
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    bool transparent = false; // some texel has alpha 0 (any frame)
    bool translucent = false; // some texel has 0 < alpha < 255 (any frame)
    int animation = -1;       // index into Atlas::animations

    // Texel coordinates 0..16 (model units) -> atlas UV.
    float u(float px) const { return u0 + (u1 - u0) * px / 16.0f; }
    float v(float px) const { return v0 + (v1 - v0) * px / 16.0f; }
};

struct SpriteAnimation {
    int sprite = 0;
    std::vector<std::vector<Image>> frameMips; // [frame][level]
    std::vector<AnimationFrame> sequence;      // expanded playback order
    bool interpolate = false;
};

class Atlas {
public:
    std::string name;
    int width = 0;
    int height = 0;
    int mipLevels = 0;
    std::vector<SpriteInfo> sprites;
    std::vector<SpriteAnimation> animations;
    std::vector<Image> levels; // whole-atlas images for levels 0..mipLevels
    int missing = 0;           // the magenta/black checker sprite

    // Index of a sprite by canonical id; `missing` when absent.
    int find(std::string_view id) const;
    bool has(std::string_view id) const { return index_.contains(std::string(id)); }
    const SpriteInfo& sprite(int i) const { return sprites[size_t(i)]; }
    const SpriteInfo& sprite(std::string_view id) const { return sprites[size_t(find(id))]; }
    // Level-0 texels of a sprite's first frame.
    Image spriteImage(int i) const;

    void rebuildIndex();

private:
    std::unordered_map<std::string, int> index_;
};

struct AtlasStats {
    int sprites = 0;
    int animated = 0;
    std::vector<std::string> errors;
};

// Stitches atlases/<atlasName>.json from the pack. `directory` and `single` sources
// are supported; `paletted_permutations` (armor trims) is skipped by design.
std::optional<Atlas> buildAtlas(const ResourcePack& pack, std::string_view atlasName, int mipLevels,
                                AtlasStats* stats = nullptr);

// Stitches an explicit list of textures ("minecraft:block/stone" -> textures/block/stone.png).
std::optional<Atlas> buildAtlasFromList(const ResourcePack& pack, std::string name,
                                        const std::vector<std::string>& textureIds, int mipLevels,
                                        AtlasStats* stats = nullptr);

// Vanilla MipmapGenerator: levels 1..N from `base` (alpha-aware gamma blend).
std::vector<Image> generateMipChain(const Image& base, int mipLevels);

// The 16x16 magenta/black missing-texture checker.
Image missingTextureImage();

// Advances animated sprites one game tick at a time.
class AtlasAnimator {
public:
    using UploadFn = std::function<void(const SpriteInfo& sprite, int level, const Image& texels)>;

    explicit AtlasAnimator(const Atlas& atlas);
    // Calls `upload` for every sprite level whose texels changed this tick.
    void tick(const UploadFn& upload);

private:
    struct State {
        int step = 0;    // position in the sequence
        int subTick = 0; // ticks spent in the current step
    };
    const Atlas& atlas_;
    std::vector<State> states_;
};

} // namespace mc
