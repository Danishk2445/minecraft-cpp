// ItemGeometry.h -- an item's quads in one display context: its layers' baked
// quads, or the entity models of special items, through the model's display
// transform (vanilla ItemTransform.apply) and centered on the origin. GL-free;
// the GUI and world renderers light and draw them.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "resources/ItemDefinitions.h"

namespace mc {

class Atlas;
class BlockEntityModels;
struct BakedModel;

struct ItemQuad {
    std::array<glm::vec3, 4> pos; // item space, blocks
    std::array<glm::vec2, 4> uv;  // block atlas
    glm::vec3 normal{0.0f, 0.0f, 1.0f};
    uint32_t tint = 0xFFFFFFFF; // ARGB
};

// Vanilla ItemTransform.apply for `ctx`: T(translation/16)·Rx·Ry·Rz·S(scale), with x
// translation and y/z rotation mirrored for the left hand (left contexts fall back to
// the right-hand transform).
glm::mat4 itemDisplayTransform(const BakedModel* model, DisplayContext ctx);
// Whether the model is lit as a flat item in the GUI ("gui_light": "front").
bool itemGuiLightFront(const std::vector<ItemLayer>& layers);

void buildItemQuads(const std::vector<ItemLayer>& layers, DisplayContext ctx, const BlockEntityModels& specials,
                    const Atlas& atlas, std::vector<ItemQuad>& out);

} // namespace mc
