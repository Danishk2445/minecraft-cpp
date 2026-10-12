#include "render/FirstPersonRenderer.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "entity/Player.h"
#include "entity/model/EntityModels.h"
#include "gfx/GL.h"
#include "render/ClientAssets.h"
#include "render/EntityRenderer.h"

namespace mc {
namespace {

constexpr float kPi = 3.14159265358979f;

glm::mat4 rotX(const glm::mat4& m, float degrees) { return glm::rotate(m, glm::radians(degrees), glm::vec3(1, 0, 0)); }
glm::mat4 rotY(const glm::mat4& m, float degrees) { return glm::rotate(m, glm::radians(degrees), glm::vec3(0, 1, 0)); }
glm::mat4 rotZ(const glm::mat4& m, float degrees) { return glm::rotate(m, glm::radians(degrees), glm::vec3(0, 0, 1)); }

} // namespace

FirstPersonRenderer::FirstPersonRenderer() : player_(playerModel(false)) {}

void FirstPersonRenderer::tick(const Player& player) {
    // The hand rises while it shows what is held and dips (0.4 a tick) to swap in a new item;
    // the main hand only rises as far as the attack strength cubed (it dips after a swing).
    // Vanilla compares stack identity: here the same slot and item (counts may change).
    auto update = [](Hand& h, const Item* item, int slot, float full) {
        h.oldHeight = h.height;
        const bool same = h.shown == item && h.slot == slot;
        h.height += std::clamp((same ? full : 0.0f) - h.height, -0.4f, 0.4f);
        if (h.height < 0.1f) {
            h.shown = item;
            h.slot = slot;
        }
    };
    const ItemStack& main = player.inventory.selectedStack();
    const float strength = player.attackStrengthScale(1.0f);
    update(main_, main.empty() ? nullptr : main.item, player.inventory.selected, strength * strength * strength);
    update(off_, player.inventory.offhand.empty() ? nullptr : player.inventory.offhand.item, Inventory::kOffhandSlot,
           1.0f);
}

void FirstPersonRenderer::render(EntityRenderer& entities, const ClientAssets& assets, const Player& player,
                                 glm::ivec2 viewport, float pt, glm::vec2 light) {
    if (player.mode() == GameMode::Spectator) return;
    entities.begin(glm::dvec3(0.0)); // view space: the camera at the origin, looking down -z
    const float swing = player.attackAnim(pt);
    // renderHandsWithItems: the hands trail the view by a tenth of the recent turn.
    const float xBob = player.xBobO + (player.xBob - player.xBobO) * pt;
    const float yBob = player.yBobO + (player.yBob - player.yBobO) * pt;
    glm::mat4 base = rotX(glm::mat4(1.0f), (player.pitch - xBob) * 0.1f);
    base = rotY(base, (player.yaw - yBob) * 0.1f);

    for (const bool left : {false, true}) {
        const Hand& hand = left ? off_ : main_;
        const float equipped = 1.0f - (hand.oldHeight + (hand.height - hand.oldHeight) * pt);
        const float s = left == player.swingingOffhand() ? swing : 0.0f;
        const float side = left ? -1.0f : 1.0f;
        if (!hand.shown) {
            if (left) continue; // only the main hand shows a bare arm
            // renderPlayerArm
            const float g = std::sqrt(s);
            glm::mat4 m = glm::translate(base, glm::vec3(side * (-0.3f * std::sin(g * kPi) + 0.64000005f),
                                                         0.4f * std::sin(g * kPi * 2.0f) - 0.6f + equipped * -0.6f,
                                                         -0.4f * std::sin(s * kPi) - 0.71999997f));
            m = rotY(m, side * 45.0f);
            m = rotY(m, side * std::sin(g * kPi) * 70.0f);
            m = rotZ(m, side * std::sin(s * s * kPi) * -20.0f);
            m = glm::translate(m, glm::vec3(side * -1.0f, 3.6f, 3.5f));
            m = rotZ(m, side * 120.0f);
            m = rotX(m, 200.0f);
            m = rotY(m, side * -135.0f);
            m = glm::translate(m, glm::vec3(side * 5.6f, 0.0f, 0.0f));
            // PlayerRenderer.renderHand: the arm at rest, tilted 0.1 outward, with its sleeve.
            ModelPart arm = *player_.root.find("right_arm");
            arm.pose.rotation = glm::vec3(0.0f, 0.0f, 0.1f);
            entities.submitModel(arm, player_.texWidth, player_.texHeight, "minecraft:entity/player/wide/steve",
                                 glm::dvec3(0.0), m, light);
            continue;
        }
        glm::mat4 m = base;
        if (player.usingItem() && left == player.useOffhand) {
            // applyEatTransform: the item bobs to the mouth, then applyItemArmTransform.
            const float f = float(player.useItemRemaining) - pt + 1.0f;
            const float f1 = f / float(player.useItem->useTicks);
            if (f1 < 0.8f) m = glm::translate(m, glm::vec3(0.0f, std::abs(std::cos(f / 4.0f * kPi) * 0.1f), 0.0f));
            const float f3 = 1.0f - std::pow(f1, 27.0f);
            m = glm::translate(m, glm::vec3(f3 * 0.6f * side, f3 * -0.5f, 0.0f));
            m = rotY(m, side * f3 * 90.0f);
            m = rotX(m, f3 * 10.0f);
            m = rotZ(m, side * f3 * 30.0f);
            m = glm::translate(m, glm::vec3(side * 0.56f, -0.52f + equipped * -0.6f, -0.72f));
        } else {
            // renderArmWithItem: swing offsets, then applyItemArmTransform and applyItemArmAttackTransform.
            const float g = std::sqrt(s);
            m = glm::translate(m, glm::vec3(side * -0.4f * std::sin(g * kPi), 0.2f * std::sin(g * kPi * 2.0f),
                                            -0.2f * std::sin(s * kPi)));
            m = glm::translate(m, glm::vec3(side * 0.56f, -0.52f + equipped * -0.6f, -0.72f));
            m = rotY(m, side * (45.0f + std::sin(s * s * kPi) * -20.0f));
            const float a = std::sin(g * kPi);
            m = rotZ(m, side * a * -20.0f);
            m = rotX(m, a * -80.0f);
            m = rotY(m, side * -45.0f);
        }
        const ItemStack& held = left ? player.inventory.offhand : player.inventory.selectedStack();
        const ItemStack shown = held.item == hand.shown ? held : ItemStack{hand.shown, 1};
        ItemModelContext ctx;
        ctx.display = left ? DisplayContext::FirstPersonLeft : DisplayContext::FirstPersonRight;
        ctx.count = shown.count;
        ctx.maxStack = shown.maxStack();
        ctx.holderPos = player.pos;
        ctx.holderYaw = player.yaw;
        layers_.clear();
        assets.itemDefinitions().evaluate(shown.item->id, ctx, &assets.grassColormap(), layers_);
        quads_.clear();
        buildItemQuads(layers_, ctx.display, assets.blockEntityModels(), assets.blockAtlas(), quads_);
        entities.submitItem(quads_, glm::dvec3(0.0), m, light);
    }
    // GameRenderer.renderItemInHand: fresh depth and a 70-degree projection.
    glClear(GL_DEPTH_BUFFER_BIT);
    const float aspect = float(viewport.x) / float(std::max(viewport.y, 1));
    ChunkRenderer::Fog noFog;
    entities.render(glm::perspective(glm::radians(70.0f), aspect, 0.05f, 100.0f), noFog);
}

} // namespace mc
