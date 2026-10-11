// ClientAssets.h -- resources loaded from the pack for rendering: the block atlas
// (CPU + GPU), baked block models and colormaps.
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "gfx/Texture.h"
#include "entity/model/BlockEntityModels.h"
#include "resources/Atlas.h"
#include "resources/BlockModels.h"
#include "resources/ItemDefinitions.h"
#include "resources/Model.h"
#include "resources/ModelBaker.h"
#include "resources/ResourcePack.h"
#include "world/block/Tints.h"

namespace mc {

class ClientAssets {
public:
    static constexpr int kBlockMipLevels = 4;

    // Loads and bakes everything; needs a current GL context for the atlas upload.
    bool load(const std::filesystem::path& packRoot);
    // Advances texture animations by one game tick.
    void tick();

    const ResourcePack& pack() const { return *pack_; }
    const Atlas& blockAtlas() const { return blockAtlas_; }
    const Texture& blockAtlasTexture() const { return blockAtlasTex_; }
    const BlockModels& blockModels() const { return blockModels_; }
    ModelStore& modelStore() { return *models_; }
    ModelBaker& baker() { return *baker_; }

    // GUI sprites (textures/gui/sprites, atlases/gui.json) with their scaling metadata.
    const Atlas& guiAtlas() const { return guiAtlas_; }
    const Texture& guiAtlasTexture() const { return guiAtlasTex_; }
    // A standalone texture by id ("minecraft:gui/container/inventory" ->
    // textures/gui/container/inventory.png), loaded on first use; the missing
    // checker when absent.
    const Texture& texture(std::string_view id) const;

    // Item model definitions (items/*.json), baked into the block atlas.
    const ItemDefinitions& itemDefinitions() const { return itemDefinitions_; }

    // Entity models drawn in place of block models (chests, beds, signs, ...).
    const BlockEntityModels& blockEntityModels() const { return *blockEntityModels_; }

    const Colormap& grassColormap() const { return grass_; }
    const Colormap& foliageColormap() const { return foliage_; }
    const Colormap& dryFoliageColormap() const { return dryFoliage_; }
    // Plains colors, for places without a biome (gallery, item icons).
    BiomeColors defaultBiomeColors() const;

private:
    std::unique_ptr<ResourcePack> pack_;
    std::unique_ptr<BlockEntityModels> blockEntityModels_;
    Atlas blockAtlas_;
    Texture blockAtlasTex_;
    Atlas guiAtlas_;
    Texture guiAtlasTex_;
    mutable std::unordered_map<std::string, std::unique_ptr<Texture>> textures_;
    mutable Texture missingTex_;
    std::unique_ptr<AtlasAnimator> animator_;
    std::unique_ptr<ModelStore> models_;
    std::unique_ptr<ModelBaker> baker_;
    BlockModels blockModels_;
    ItemDefinitions itemDefinitions_;
    Colormap grass_, foliage_, dryFoliage_;
};

} // namespace mc
