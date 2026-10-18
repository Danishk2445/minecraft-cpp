// DyeColor.h -- vanilla's 16 dye colors in their id order (white 0 .. black 15):
// names and texture colors (DyeColor.getTextureDiffuseColor), for sheep wool and
// other tinted things.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace mc {

constexpr std::array<std::string_view, 16> kDyeColors = {
    "white", "orange", "magenta", "light_blue", "yellow", "lime",  "pink",  "gray",
    "light_gray", "cyan", "purple", "blue", "brown", "green", "red", "black"};

constexpr std::array<uint32_t, 16> kDyeTextureColors = {
    0xF9FFFE, 0xF9801D, 0xC74EBD, 0x3AB3DA, 0xFED83D, 0x80C71F, 0xF38BAA, 0x474F52,
    0x9D9D97, 0x169C9C, 0x8932B8, 0x3C44AA, 0x835432, 0x5E7C16, 0xB02E26, 0x1D1D21};

inline std::optional<int> dyeColorIndex(std::string_view name) {
    for (size_t i = 0; i < kDyeColors.size(); ++i)
        if (kDyeColors[i] == name) return int(i);
    return std::nullopt;
}

// The dye color of a dye item ("minecraft:red_dye"), if it is one.
inline std::optional<int> dyeOfItem(std::string_view itemId) {
    if (itemId.starts_with("minecraft:")) itemId.remove_prefix(10);
    if (!itemId.ends_with("_dye")) return std::nullopt;
    return dyeColorIndex(itemId.substr(0, itemId.size() - 4));
}

} // namespace mc
