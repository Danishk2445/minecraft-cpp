// FirstPersonRenderer.h -- the hands in first person (vanilla ItemInHandRenderer):
// the held item, or the bare right arm, with the equip dip when the item changes,
// the swing, and the slight lag behind the view, drawn over the world.
#pragma once

#include <glm/glm.hpp>

#include "entity/model/ItemGeometry.h"
#include "entity/model/ModelPart.h"

namespace mc {

class ClientAssets;
class EntityRenderer;
class Player;
struct Item;

// Vanilla GameRenderer.bobView: walking sways the view side to side and dips it.
glm::mat4 viewBob(const Player& player, float partialTick);

class FirstPersonRenderer {
public:
    FirstPersonRenderer();
    // 20 Hz: the equip animation (vanilla ItemInHandRenderer.tick).
    void tick(const Player& player);
    // After the world: clears depth and draws the hands with their own projection
    // (vanilla renderItemInHand), swayed by `view` (the camera's hurt tilt and bob). The
    // lightmap must be bound to texture unit 1.
    void render(EntityRenderer& entities, const ClientAssets& assets, const Player& player, glm::ivec2 viewport,
                float partialTick, glm::vec2 light, const glm::mat4& view = glm::mat4(1.0f));

private:
    // The items shown in each hand; they change once the hand has dipped out of view.
    struct Hand {
        const Item* shown = nullptr;
        int slot = -1;
        float height = 0.0f, oldHeight = 0.0f;
    };
    Hand main_, off_;
    ModelDefinition player_;
    std::vector<ItemLayer> layers_;
    std::vector<ItemQuad> quads_;
};

} // namespace mc
