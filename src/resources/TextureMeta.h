// TextureMeta.h -- the .png.mcmeta sections the game uses.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/Json.h"

namespace mc {

struct AnimationFrame {
    int index = 0;
    int time = 1; // ticks
};

struct AnimationMeta {
    int frameTime = 1;
    bool interpolate = false;
    int width = -1;  // frame size; -1 = derive (square frames of the image width)
    int height = -1;
    std::vector<AnimationFrame> frames; // empty = every frame in order
};

struct GuiScaling {
    enum class Type { Stretch, Tile, NineSlice } type = Type::Stretch;
    int width = 0; // nine_slice / tile: the nominal size the borders refer to
    int height = 0;
    int left = 0, top = 0, right = 0, bottom = 0;
    bool stretchInner = false;
};

struct TextureMeta {
    std::optional<AnimationMeta> animation;
    std::optional<GuiScaling> gui;
    bool blur = false;
    bool clamp = false;
};

// Parses an mcmeta document. Unknown sections are ignored.
TextureMeta parseTextureMeta(const Json& j);

} // namespace mc
