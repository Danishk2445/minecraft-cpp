#include "entity/model/ItemGeometry.h"

#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>

#include "entity/model/BlockEntityModels.h"
#include "resources/Atlas.h"
#include "resources/ModelBaker.h"

namespace mc {
namespace {

bool leftHanded(DisplayContext c) {
    return c == DisplayContext::ThirdPersonLeft || c == DisplayContext::FirstPersonLeft;
}

} // namespace

glm::mat4 itemDisplayTransform(const BakedModel* model, DisplayContext ctx) {
    if (!model) return glm::mat4(1.0f);
    size_t i = size_t(ctx);
    if (!model->hasDisplay[i]) {
        // Vanilla ItemTransforms: missing left-hand transforms copy the right hand's.
        if (ctx == DisplayContext::ThirdPersonLeft) i = size_t(DisplayContext::ThirdPersonRight);
        if (ctx == DisplayContext::FirstPersonLeft) i = size_t(DisplayContext::FirstPersonRight);
        if (!model->hasDisplay[i]) return glm::mat4(1.0f);
    }
    const ItemTransform& t = model->display[i];
    const bool left = leftHanded(ctx);
    glm::vec3 rot = t.rotation;
    if (left) rot.y = -rot.y, rot.z = -rot.z;
    // Translations are in pixels, clamped to +-80; scales to +-4.
    glm::vec3 tr = glm::clamp(t.translation, glm::vec3(-80.0f), glm::vec3(80.0f)) / 16.0f;
    if (left) tr.x = -tr.x;
    glm::mat4 m = glm::translate(glm::mat4(1.0f), tr);
    m = glm::rotate(m, glm::radians(rot.x), glm::vec3(1, 0, 0));
    m = glm::rotate(m, glm::radians(rot.y), glm::vec3(0, 1, 0));
    m = glm::rotate(m, glm::radians(rot.z), glm::vec3(0, 0, 1));
    return glm::scale(m, glm::clamp(t.scale, glm::vec3(-4.0f), glm::vec3(4.0f)));
}

bool itemGuiLightFront(const std::vector<ItemLayer>& layers) {
    // Vanilla takes the lighting of the first layer's model.
    return !layers.empty() && layers[0].transforms && layers[0].transforms->guiLightFront;
}

void buildItemQuads(const std::vector<ItemLayer>& layers, DisplayContext ctx, const BlockEntityModels& specials,
                    const Atlas& atlas, std::vector<ItemQuad>& out) {
    std::vector<BlockEntityPiece> pieces;
    std::vector<ModelQuad> modelQuads;
    for (const ItemLayer& layer : layers) {
        // Display transform, then the model's 0..1 block centered on the origin.
        const glm::mat4 m = glm::translate(itemDisplayTransform(layer.transforms, ctx), glm::vec3(-0.5f));
        const glm::mat3 nm = glm::mat3(m);
        if (layer.model) {
            for (const BakedQuad& q : layer.model->quads) {
                ItemQuad iq;
                for (size_t k = 0; k < 4; ++k) {
                    iq.pos[k] = glm::vec3(m * glm::vec4(q.pos[k], 1.0f));
                    iq.uv[k] = q.uv[k];
                }
                // Vanilla lights item quads by their face direction, not their exact normal.
                iq.normal = glm::normalize(nm * glm::vec3(normal(q.face)));
                iq.tint = layer.tint(q.tint);
                out.push_back(iq);
            }
        }
        if (layer.special) {
            pieces.clear();
            specials.itemPieces(*layer.special, pieces);
            for (const BlockEntityPiece& piece : pieces) {
                const SpriteInfo& sprite = atlas.sprite(piece.texture);
                modelQuads.clear();
                bakeModel(piece.model->root, m * piece.transform, piece.model->texWidth, piece.model->texHeight,
                          modelQuads);
                for (const ModelQuad& q : modelQuads) {
                    ItemQuad iq;
                    for (size_t k = 0; k < 4; ++k) {
                        iq.pos[k] = q.pos[k];
                        iq.uv[k] = {sprite.u0 + (sprite.u1 - sprite.u0) * q.uv[k].x,
                                    sprite.v0 + (sprite.v1 - sprite.v0) * q.uv[k].y};
                    }
                    iq.normal = q.normal;
                    iq.tint = 0xFF000000u | piece.tint;
                    out.push_back(iq);
                    if (piece.doubleSided) {
                        std::reverse(iq.pos.begin(), iq.pos.end());
                        std::reverse(iq.uv.begin(), iq.uv.end());
                        iq.normal = -iq.normal;
                        out.push_back(iq);
                    }
                }
            }
        }
    }
}

} // namespace mc
