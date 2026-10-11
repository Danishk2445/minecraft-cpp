// BlockEntityModels.h -- geometry for blocks drawn from entity models (chests,
// beds, signs, banners, heads, shulker boxes, bell, decorated pot, conduit):
// for each state, the model parts to draw, their texture and the transform
// into the block. The mesher bakes them into chunk meshes, with the textures
// living in the block atlas.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "entity/model/ModelPart.h"
#include "world/block/BlockRegistry.h"

namespace mc {

struct BlockEntityPiece {
    const ModelDefinition* model = nullptr;
    std::string texture;       // atlas sprite id, e.g. "minecraft:entity/chest/normal"
    glm::mat4 transform{1.0f}; // model root space -> block space (blocks)
    uint32_t tint = 0xFFFFFF;  // multiplied in (banner base colors)
    bool doubleSided = false;  // vanilla draws it without backface culling (hanging-sign chains)
};

// Models that together account for a texture: the UV-island test checks
// that no opaque texel lies outside their faces when `exclusive` is set
// (mob heads share their texture with the mob's body, so they are not).
struct ModelTextureUse {
    std::string texture; // "minecraft:entity/chest/normal"
    std::vector<const ModelDefinition*> models;
    bool exclusive = true;
};

class BlockEntityModels {
public:
    BlockEntityModels();
    ~BlockEntityModels();

    // Whether `block` is drawn by these models.
    bool handles(const Block& block) const;
    // The pieces to draw for `state` (empty if none).
    void pieces(const BlockRegistry& reg, StateId state, std::vector<BlockEntityPiece>& out) const;
    // Every texture any piece may use (added to the block atlas).
    std::vector<std::string> textures() const;
    // Every model definition with the textures it reads, for the UV-island test.
    std::vector<ModelTextureUse> textureUses() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Vanilla DyeColor.getTextureDiffuseColor (banner bases), by dye name; white if unknown.
uint32_t dyeTextureColor(std::string_view dye);

} // namespace mc
